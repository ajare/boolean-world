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
backoff; usage-limit failures poll at ten-minute intervals by default. After
each completed ticket the loop reports that run's token usage and, where the
provider exposes it, current-window and weekly usage. Logs and sessions are
written below the system temporary directory in pi-ralph-loop.

Use `pi --list-models` to see models available for the providers configured on
this machine. Model names use pi's provider/model form. Thinking support and
available effort levels vary by model; the script accepts off, minimal, low,
medium, high, xhigh, and max and passes the selected value to pi.

PowerShell's common -Verbose switch also passes --verbose to pi and enables
extra loop diagnostics.

.PARAMETER Model
Pi model in provider/model form. Defaults to openai-codex/gpt-5.6-sol.

.PARAMETER Effort
Thinking effort passed to pi. Defaults to medium. Valid values are off,
minimal, low, medium, high, xhigh, and max.

.PARAMETER Repo
GitHub repository in owner/name form. When omitted, the repository is inferred
from the current checkout's origin remote.

.PARAMETER ReadyLabel
Label used to identify executable tickets. Defaults to ready-for-agent.

.PARAMETER InitialRetryIntervalSeconds
Initial delay after a retryable provider or server failure. Defaults to 30.

.PARAMETER MaxRetryIntervalSeconds
Maximum exponential-backoff delay for provider or server failures. Defaults to
900.

.PARAMETER UsagePollSeconds
Delay between retries after a provider usage-limit response. Defaults to 600.

.PARAMETER Once
Processes at most one ticket, then exits.

.PARAMETER DryRun
Prints the next eligible ticket without claiming it or starting pi.

.PARAMETER Quiet
Suppresses routine loop and agent output while retaining warnings, errors, and
the required end-of-ticket usage summary.

.EXAMPLE
.\tools\ralph-loop.ps1

Runs the full frontier with the defaults: GPT-5.6 Sol at medium effort.

.EXAMPLE
.\tools\ralph-loop.ps1 -Model openai-codex/gpt-5.6-terra -Effort high

Uses GPT-5.6 Terra with high reasoning effort for every eligible ticket.

.EXAMPLE
.\tools\ralph-loop.ps1 -Model openai-codex/gpt-5.6-luna -Effort low -Once

Uses GPT-5.6 Luna at low effort and stops after one ticket.

.EXAMPLE
.\tools\ralph-loop.ps1 -Model openai-codex/gpt-5.4-mini -Effort minimal -Quiet

Uses a smaller model at minimal effort and shows only essential output and the
usage summary.

.EXAMPLE
.\tools\ralph-loop.ps1 -Model anthropic/claude-opus-4-6 -Effort high -Once -Verbose

Uses an Anthropic model, when that provider and model are configured in pi,
for one ticket with verbose pi and loop diagnostics.

.EXAMPLE
.\tools\ralph-loop.ps1 -Repo ajare/boolean-world -ReadyLabel ready-for-agent -DryRun

Shows the next eligible ticket in an explicit repository without claiming or
running it.

.EXAMPLE
.\tools\ralph-loop.ps1 -InitialRetryIntervalSeconds 15 -MaxRetryIntervalSeconds 300 -UsagePollSeconds 900

Overrides provider-failure backoff and usage-limit polling intervals.

.EXAMPLE
Get-Help .\tools\ralph-loop.ps1 -Detailed

Shows parameter descriptions and these examples in PowerShell help.
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
    [switch]$DryRun,
    [switch]$Quiet
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

function Write-Status {
    param([Parameter(Mandatory = $true)][string]$Message)

    if (-not $Quiet) {
        Write-Host $Message
    }
}

function Get-NumericProperty {
    param(
        [object]$Object,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if ($null -eq $Object) {
        return [double]0
    }
    $property = $Object.psobject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return [double]0
    }
    return [double]$property.Value
}

function Get-SessionUsage {
    param([Parameter(Mandatory = $true)][string]$SessionDirectory)

    $sessionFile = Get-ChildItem -Path $SessionDirectory -Filter "*.jsonl" -File -Recurse -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($null -eq $sessionFile) {
        return $null
    }

    $usage = [ordered]@{
        Provider = ""
        Model = ""
        Input = [int64]0
        Output = [int64]0
        CacheRead = [int64]0
        CacheWrite = [int64]0
        Reasoning = [int64]0
        TotalTokens = [int64]0
        Cost = [double]0
    }

    foreach ($line in [System.IO.File]::ReadLines($sessionFile.FullName)) {
        try {
            $entry = $line | ConvertFrom-Json
        } catch {
            continue
        }
        if ($entry.type -ne "message" -or $entry.message.role -ne "assistant" -or $null -eq $entry.message.usage) {
            continue
        }

        $message = $entry.message
        $messageUsage = $message.usage
        $usage.Provider = [string]$message.provider
        $usage.Model = [string]$message.model
        $usage.Input += [int64](Get-NumericProperty $messageUsage "input")
        $usage.Output += [int64](Get-NumericProperty $messageUsage "output")
        $usage.CacheRead += [int64](Get-NumericProperty $messageUsage "cacheRead")
        $usage.CacheWrite += [int64](Get-NumericProperty $messageUsage "cacheWrite")
        $usage.Reasoning += [int64](Get-NumericProperty $messageUsage "reasoning")
        $usage.TotalTokens += [int64](Get-NumericProperty $messageUsage "totalTokens")
        $costProperty = $messageUsage.psobject.Properties["cost"]
        if ($null -ne $costProperty) {
            $usage.Cost += Get-NumericProperty $costProperty.Value "total"
        }
    }

    return [pscustomobject]$usage
}

function Format-ResetDuration {
    param([object]$Window)

    if ($null -eq $Window) {
        return "reset unknown"
    }
    $resetAfterProperty = $Window.psobject.Properties["reset_after_seconds"]
    $resetAtProperty = $Window.psobject.Properties["resets_at"]
    if ($null -ne $resetAfterProperty -and $null -ne $resetAfterProperty.Value) {
        $duration = [TimeSpan]::FromSeconds([double]$resetAfterProperty.Value)
    } elseif ($null -ne $resetAtProperty -and $resetAtProperty.Value) {
        $resetAt = [DateTimeOffset]::Parse([string]$resetAtProperty.Value)
        $duration = $resetAt - [DateTimeOffset]::UtcNow
        if ($duration.TotalSeconds -lt 0) {
            $duration = [TimeSpan]::Zero
        }
    } else {
        return "reset unknown"
    }
    if ($duration.TotalDays -ge 1) {
        return "resets in $([Math]::Floor($duration.TotalDays))d $($duration.Hours)h"
    }
    if ($duration.TotalHours -ge 1) {
        return "resets in $([Math]::Floor($duration.TotalHours))h $($duration.Minutes)m"
    }
    return "resets in $([Math]::Max(0, [Math]::Ceiling($duration.TotalMinutes)))m"
}

function Write-UsageWindow {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [object]$Window
    )

    if ($null -eq $Window) {
        Write-Host "  ${Name}: not reported"
        return
    }
    $usedProperty = $Window.psobject.Properties["used_percent"]
    if ($null -eq $usedProperty) {
        $usedProperty = $Window.psobject.Properties["utilization"]
    }
    if ($null -eq $usedProperty -or $null -eq $usedProperty.Value) {
        Write-Host "  ${Name}: usage not reported; $(Format-ResetDuration $Window)"
        return
    }
    $used = [double]$usedProperty.Value
    $remaining = [Math]::Max([double]0, 100.0 - $used)
    Write-Host ("  {0}: {1:N1}% used, {2:N1}% remaining; {3}" -f $Name, $used, $remaining, (Format-ResetDuration $Window))
}

function Get-OAuthCredential {
    param([Parameter(Mandatory = $true)][string]$Provider)

    $configDirectory = if ($env:PI_CODING_AGENT_DIR) { $env:PI_CODING_AGENT_DIR } else { Join-Path $HOME ".pi/agent" }
    $authPath = Join-Path $configDirectory "auth.json"
    $authFile = Get-Content -Path $authPath -Raw | ConvertFrom-Json
    $credentialProperty = $authFile.psobject.Properties[$Provider]
    if ($null -eq $credentialProperty -or $credentialProperty.Value.type -ne "oauth") {
        throw "No OAuth credential is configured for $Provider."
    }
    return $credentialProperty.Value
}

function Show-ProviderUsage {
    param([Parameter(Mandatory = $true)][object]$SessionUsage)

    $provider = if ($SessionUsage.Provider) { $SessionUsage.Provider } else { ($Model -split '/', 2)[0] }
    Write-Host "Usage ($provider/$($SessionUsage.Model))"
    Write-Host ('  Current ticket: {0:N0} tokens (input {1:N0}, output {2:N0}, reasoning {3:N0}, cache read {4:N0}, cache write {5:N0}); cost ${6:N4}' -f
        $SessionUsage.TotalTokens, $SessionUsage.Input, $SessionUsage.Output, $SessionUsage.Reasoning,
        $SessionUsage.CacheRead, $SessionUsage.CacheWrite, $SessionUsage.Cost)

    if ($provider -notin @("openai-codex", "anthropic")) {
        Write-Host "  Current window: not available from this provider"
        Write-Host "  Weekly: not available from this provider"
        return
    }

    try {
        $credential = Get-OAuthCredential $provider
        if ($provider -eq "anthropic") {
            $headers = @{
                Authorization = "Bearer $($credential.access)"
                "anthropic-beta" = "oauth-2025-04-20"
            }
            $providerUsage = Invoke-RestMethod -Method Get -Uri "https://api.anthropic.com/api/oauth/usage" -Headers $headers
            Write-UsageWindow -Name "Current window" -Window $providerUsage.five_hour
            Write-UsageWindow -Name "Weekly" -Window $providerUsage.seven_day
            return
        }

        $headers = @{
            Authorization = "Bearer $($credential.access)"
            "ChatGPT-Account-Id" = [string]$credential.accountId
        }
        $providerUsage = Invoke-RestMethod -Method Get -Uri "https://chatgpt.com/backend-api/wham/usage" -Headers $headers
        $primary = $providerUsage.rate_limit.primary_window
        $secondary = $providerUsage.rate_limit.secondary_window

        # Codex normally reports a short rolling primary window and a weekly
        # secondary window. Some plans expose only one seven-day primary window.
        $currentWindow = $primary
        $weeklyWindow = $secondary
        if ($null -ne $primary -and [double]$primary.limit_window_seconds -ge 518400 -and $null -eq $secondary) {
            $currentWindow = $null
            $weeklyWindow = $primary
        }
        Write-UsageWindow -Name "Current window" -Window $currentWindow
        Write-UsageWindow -Name "Weekly" -Window $weeklyWindow
    } catch {
        Write-Warning "Could not read provider usage: $($_.Exception.Message)"
        Write-Host "  Current window: unavailable"
        Write-Host "  Weekly: unavailable"
    }
}

function Test-UsageLimitError {
    param([Parameter(Mandatory = $true)][string]$Text)

    return $Text -match '(?is)(usage limit|usage_limit_reached|usage cap|quota exceeded|insufficient_quota|out of credits|credit balance|billing limit|subscription limit|weekly limit|monthly limit|weighted tokens|token limit.*reset|rate limit.*reset|limit resets? at)'
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
if ($Quiet -and $VerbosePreference -eq "Continue") {
    throw "-Quiet and -Verbose cannot be used together."
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
        Write-Status "No unblocked, unclaimed '$ReadyLabel' tickets are available."
        break
    }

    $number = [int]$ticket.number
    Write-Status "Selected #${number}: $($ticket.title)"
    if ($DryRun) {
        Write-Status "Dry run: would start pi with model '$Model' and effort '$Effort'."
        break
    }

    if (@($ticket.assignees).Count -eq 0) {
        Invoke-Gh @("issue", "edit", [string]$number, "--repo", $Repo, "--add-assignee", "@me") | Out-Null
        Write-Status "Claimed #$number as $currentUser."
    }

    $startingHead = (& git rev-parse HEAD).Trim()
    $prompt = Get-TicketPrompt -Repository $Repo -Number $number
    $retryInterval = $InitialRetryIntervalSeconds
    $attempt = 0
    $successfulSessionDirectory = $null

    while ($true) {
        ++$attempt
        $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $logPath = Join-Path $logDirectory "issue-$number-$timestamp-attempt-$attempt.log"
        $sessionDirectory = Join-Path $logDirectory "issue-$number-$timestamp-attempt-$attempt-session"
        New-Item -ItemType Directory -Force -Path $sessionDirectory | Out-Null
        Write-Status "Starting pi for #$number (attempt $attempt). Log: $logPath"
        Write-Verbose "Session directory: $sessionDirectory"

        $piArguments = @(
            "--print", "--approve", "--model", $Model, "--thinking", $Effort,
            "--name", "ralph-$number", "--session-dir", $sessionDirectory
        )
        if ($VerbosePreference -eq "Continue") {
            $piArguments += "--verbose"
        }
        $piArguments += $prompt

        $previousErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            $output = @(& pi @piArguments 2>&1 | Tee-Object -FilePath $logPath)
            $piExitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $previousErrorActionPreference
        }
        $outputText = $output -join [Environment]::NewLine
        if (-not $Quiet -and $outputText) {
            Write-Host $outputText
        }

        # A provider can fail after the agent has already committed and closed.
        if (Test-TicketComplete -Repository $Repo -Number $number -StartingHead $startingHead) {
            Write-Status "Ticket #$number completed successfully."
            $successfulSessionDirectory = $sessionDirectory
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

    $sessionUsage = Get-SessionUsage -SessionDirectory $successfulSessionDirectory
    if ($null -eq $sessionUsage) {
        Write-Warning "Could not read current-ticket usage from $successfulSessionDirectory."
    } else {
        Show-ProviderUsage -SessionUsage $sessionUsage
    }

    if ($Once) {
        break
    }
}
