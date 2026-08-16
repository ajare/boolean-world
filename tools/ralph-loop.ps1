<#
.SYNOPSIS
Runs ready GitHub tickets through pi until no dependency-free work remains.

.DESCRIPTION
Selects open issues carrying the ready-for-agent label, excludes issues with
open native GitHub blockers, and resumes tickets already assigned to the
current user before claiming new work. New work is ordered by priority labels
(critical/P0, high/P1, medium/P2, low/P3, or priority:<number>) and then issue
number. Issues referenced by another ticket's "## Parent" section are treated
as specs/maps rather than executable tickets.

Pi runs in non-interactive print mode. Provider failures use capped exponential
backoff; usage-limit failures poll at ten-minute intervals by default. Logs are
written below the system temporary directory in pi-ralph-loop.

.EXAMPLE
./tools/ralph-loop.ps1

.EXAMPLE
./tools/ralph-loop.ps1 -Model anthropic/claude-opus-4-6 -Effort high -Once

.EXAMPLE
./tools/ralph-loop.ps1 -DryRun
#>
[CmdletBinding()]
param(
    [string]$Model = "openai-codex/gpt-5.6-sol",

    [ValidateSet("off", "minimal", "low", "medium", "high", "xhigh", "max")]
    [string]$Effort = "medium",

    [string]$Repo = "",
    [string]$ReadyLabel = "ready-for-agent",
    [int]$InitialRetryIntervalSeconds = 30,
    [int]$MaxRetryIntervalSeconds = 900,
    [int]$UsagePollSeconds = 600,
    [switch]$Once,
    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-Gh {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    $output = & gh @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "gh $($Arguments -join ' ') failed:`n$($output -join [Environment]::NewLine)"
    }
    return ($output -join [Environment]::NewLine)
}

function Get-Priority {
    param([object[]]$Labels)

    $rank = 100
    foreach ($label in $Labels) {
        $name = ([string]$label.name).ToLowerInvariant().Trim()
        switch -Regex ($name) {
            '^(priority:\s*)?(critical|urgent|p0)$' { $rank = [Math]::Min($rank, 0); continue }
            '^(priority:\s*)?(high|p1)$'            { $rank = [Math]::Min($rank, 1); continue }
            '^(priority:\s*)?(medium|normal|p2)$'  { $rank = [Math]::Min($rank, 2); continue }
            '^(priority:\s*)?(low|p3)$'            { $rank = [Math]::Min($rank, 3); continue }
            '^priority:\s*(\d+)$'                  { $rank = [Math]::Min($rank, [int]$Matches[1]); continue }
        }
    }
    return $rank
}

function Get-NextTicket {
    param(
        [Parameter(Mandatory = $true)][string]$Repository,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$CurrentUser
    )

    $json = Invoke-Gh @(
        "issue", "list", "--repo", $Repository,
        "--state", "open", "--label", $Label, "--limit", "100",
        "--json", "number,title,body,labels,assignees,url"
    )
    $issues = $json | ConvertFrom-Json
    if ($issues.Count -eq 0) {
        return $null
    }

    # Specs/maps can carry the ready label too. A ready issue referenced by the
    # ticket convention's "## Parent" section is not itself executable work.
    $parentNumbers = @{}
    foreach ($issue in $issues) {
        if ([string]$issue.body -match '(?im)^## Parent\s*\r?\n+\s*#(\d+)') {
            $parentNumbers[[int]($Matches[1])] = $true
        }
    }

    $candidates = @()
    foreach ($issue in $issues) {
        if ($parentNumbers.ContainsKey([int]($issue.number))) {
            continue
        }

        $assignees = @($issue.assignees)
        $assignedToCurrentUser = @($assignees | Where-Object { $_.login -eq $CurrentUser }).Count -gt 0
        if ($assignees.Count -gt 0 -and -not $assignedToCurrentUser) {
            continue
        }

        $detailJson = Invoke-Gh @("api", "repos/$Repository/issues/$($issue.number)")
        $detail = $detailJson | ConvertFrom-Json
        if ([int]($detail.issue_dependencies_summary.blocked_by) -gt 0) {
            continue
        }

        $candidates += [pscustomobject]@{
            Issue = $issue
            ResumeRank = if ($assignedToCurrentUser) { 0 } else { 1 }
            Priority = Get-Priority @($issue.labels)
        }
    }

    if ($candidates.Count -eq 0) {
        return $null
    }

    return ($candidates |
        Sort-Object ResumeRank, Priority, @{ Expression = { [int]($_.Issue.number) } } |
        Select-Object -First 1).Issue
}

function Get-TicketPrompt {
    param(
        [Parameter(Mandatory = $true)][string]$Repository,
        [Parameter(Mandatory = $true)][int]$Number
    )

    $json = Invoke-Gh @(
        "issue", "view", [string]$Number, "--repo", $Repository,
        "--json", "number,title,body,comments,url"
    )
    $issue = $json | ConvertFrom-Json
    $comments = @($issue.comments | ForEach-Object { $_.body })
    $commentText = if ($comments.Count -eq 0) {
        "(No comments.)"
    } else {
        ($comments -join "`n`n---`n`n")
    }

    return @"
Implement GitHub ticket #$($issue.number): $($issue.title)
$($issue.url)

You are running non-interactively. Work autonomously through implementation; do not stop at a plan and do not ask the user questions. Read and follow the repository instructions and domain documentation. Inspect the current worktree first because this may be a retry after a provider failure.

Only implement this ticket, not its parent or blocked follow-up tickets. Use the ticket's acceptance criteria as the contract. Run focused tests while developing, then the relevant builds, formatting checks, and tests before completion. Preserve unrelated and pre-existing untracked files.

When the ticket is fully implemented and verified:
1. Commit all tracked changes on the current branch with a message referencing #$($issue.number).
2. Close #$($issue.number) with a concise comment containing the commit hash and validation performed.
3. Finish with a concise implementation summary.

If implementation cannot be completed for a code, test, or specification reason, leave the issue open, do not commit partial work merely to satisfy this prompt, and explain the blocker in your final response.

## Ticket body

$($issue.body)

## Ticket comments

$commentText
"@
}

function Test-UsageLimitError {
    param([Parameter(Mandatory = $true)][string]$Text)

    return $Text -match '(?is)(usage limit|usage_limit_reached|usage cap|quota exceeded|insufficient_quota|out of credits|credit balance|billing limit|subscription limit|weekly limit|monthly limit|weighted tokens|token limit.*reset|rate limit.*reset|limit resets? at)'},{
}

function Test-ServerOrApiError {
    param([Parameter(Mandatory = $true)][string]$Text)

    return $Text -match '(?is)(HTTP\s*(408|409|425|429|5\d\d)|status\s*(408|409|425|429|5\d\d)|server error|internal server error|service unavailable|bad gateway|gateway timeout|overloaded|temporarily unavailable|request timeout|timed out|ECONNRESET|ECONNREFUSED|ENETUNREACH|EAI_AGAIN|socket hang up|connection reset|connection closed|fetch failed|network error|server_error|stream.*(closed|terminated))'
}

function Test-TicketComplete {
    param(
        [Parameter(Mandatory = $true)][string]$Repository,
        [Parameter(Mandatory = $true)][int]$Number,
        [Parameter(Mandatory = $true)][string]$StartingHead
    )

    $state = (Invoke-Gh @("issue", "view", [string]$Number, "--repo", $Repository, "--json", "state", "--jq", ".state")).Trim()
    $currentHead = (& git rev-parse HEAD).Trim()
    $trackedChanges = @(& git status --porcelain --untracked-files=no)
    return $state -eq "CLOSED" -and $currentHead -ne $StartingHead -and $trackedChanges.Count -eq 0
}

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw "gh is required but was not found on PATH."
}
if (-not (Get-Command pi -ErrorAction SilentlyContinue)) {
    throw "pi is required but was not found on PATH."
}
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "git is required but was not found on PATH."
}
if ($InitialRetryIntervalSeconds -lt 1 -or $MaxRetryIntervalSeconds -lt $InitialRetryIntervalSeconds) {
    throw "Retry intervals must be positive and MaxRetryIntervalSeconds must be at least InitialRetryIntervalSeconds."
}
if ($UsagePollSeconds -lt 1) {
    throw "UsagePollSeconds must be positive."
}

$repoRoot = (& git rev-parse --show-toplevel 2>$null).Trim()
if ($LASTEXITCODE -ne 0 -or -not $repoRoot) {
    throw "Run this script from inside a Git repository."
}
Set-Location $repoRoot
$initialTrackedChanges = @(& git status --porcelain --untracked-files=no)
if ($initialTrackedChanges.Count -gt 0) {
    throw "The tracked worktree is not clean. Commit or restore tracked changes before starting the loop."
}

if (-not $Repo) {
    $Repo = (Invoke-Gh @("repo", "view", "--json", "nameWithOwner", "--jq", ".nameWithOwner")).Trim()
}
$currentUser = (Invoke-Gh @("api", "user", "--jq", ".login")).Trim()
$logDirectory = Join-Path ([System.IO.Path]::GetTempPath()) "pi-ralph-loop"
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null

while ($true) {
    $ticket = Get-NextTicket -Repository $Repo -Label $ReadyLabel -CurrentUser $currentUser
    if ($null -eq $ticket) {
        Write-Host "No unblocked, unclaimed '$ReadyLabel' tickets are available."
        break
    }

    $number = [int]$ticket.number
    Write-Host "Selected #${number}: $($ticket.title)"
    if ($DryRun) {
        Write-Host "Dry run: would start pi with model '$Model' and effort '$Effort'."
        break
    }

    if (@($ticket.assignees).Count -eq 0) {
        Invoke-Gh @("issue", "edit", [string]$number, "--repo", $Repo, "--add-assignee", "@me") | Out-Null
        Write-Host "Claimed #$number as $currentUser."
    }

    $startingHead = (& git rev-parse HEAD).Trim()
    $prompt = Get-TicketPrompt -Repository $Repo -Number $number
    $retryInterval = $InitialRetryIntervalSeconds
    $attempt = 0

    while ($true) {
        ++$attempt
        $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $logPath = Join-Path $logDirectory "issue-$number-$timestamp-attempt-$attempt.log"
        Write-Host "Starting pi for #$number (attempt $attempt). Log: $logPath"

        $previousErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            $output = @(& pi --print --approve --model $Model --thinking $Effort --name "ralph-$number" $prompt 2>&1 |
                Tee-Object -FilePath $logPath)
            $piExitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $previousErrorActionPreference
        }
        $outputText = $output -join [Environment]::NewLine

        # A provider can fail after the agent has already committed and closed.
        if (Test-TicketComplete -Repository $Repo -Number $number -StartingHead $startingHead) {
            Write-Host "Ticket #$number completed successfully."
            break
        }

        if ($piExitCode -eq 0) {
            throw "Pi exited successfully, but #$number was not closed with a new commit and clean tracked worktree. Inspect $logPath."
        }

        if (Test-UsageLimitError $outputText) {
            Write-Warning "Usage limit detected for #$number. Retrying in $UsagePollSeconds seconds."
            Start-Sleep -Seconds $UsagePollSeconds
            continue
        }

        if (Test-ServerOrApiError $outputText) {
            Write-Warning "Server/API failure detected for #$number. Retrying in $retryInterval seconds."
            Start-Sleep -Seconds $retryInterval
            $retryInterval = [Math]::Min($retryInterval * 2, $MaxRetryIntervalSeconds)
            continue
        }

        throw "Pi failed for a non-retryable implementation reason on #$number. The issue remains assigned and open. Inspect $logPath."
    }

    if ($Once) {
        break
    }
}
