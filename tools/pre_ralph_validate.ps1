<#
.SYNOPSIS
Validates that a labelled GitHub issue frontier is ready for ralph-loop.ps1.

.DESCRIPTION
Validates all open tickets carrying one supplied label in the current GitHub
repository. The script requires at least one matching ticket, ready-for-agent
on every ticket, at least one ticket with native GitHub blocked_by metadata,
and supported difficulty and priority labels on every ticket.

Difficulty and priority namespaces may use ':' or '/'. Supported difficulty
values are trivial, small, low, medium, large, high, and hard. Supported
priority values are critical, urgent, p0, high, p1, medium, normal, p2, low,
p3, and non-negative integers.

.PARAMETER Label
The single label used to select open tickets, for example feature/my-feature.

.EXAMPLE
.\tools\pre_ralph_validate.ps1 -Label feature/resource-validation

.EXAMPLE
.\tools\pre_ralph_validate.ps1 feature/resource-validation
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateNotNullOrEmpty()]
    [string]$Label
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

function Add-TicketFailure {
    param(
        [Parameter(Mandatory = $true)]
        [System.Collections.Generic.List[string]]$Failures,
        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    $Failures.Add($Message)
}

try {
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
        throw "gh is required but was not found on PATH."
    }

    $repository = (Invoke-Gh @("repo", "view", "--json", "nameWithOwner", "--jq", ".nameWithOwner")).Trim()
    $json = Invoke-Gh @(
        "issue", "list", "--repo", $repository,
        "--state", "open", "--label", $Label,
        "--limit", "1000",
        "--json", "number,title,labels,url"
    )
    $parsedIssues = $json | ConvertFrom-Json
    $issues = [object[]]$parsedIssues

    if ($issues.Count -eq 0) {
        Write-Error "No open tickets carry label '$Label' in $repository." -ErrorAction Continue
        exit 1
    }

    Write-Host "Validating $($issues.Count) open ticket(s) carrying '$Label' in $repository."

    $supportedDifficulties = @("trivial", "small", "low", "medium", "large", "high", "hard")
    $supportedPriorities = @("critical", "urgent", "p0", "high", "p1", "medium", "normal", "p2", "low", "p3")
    $failureCount = 0
    $blockedTicketCount = 0

    foreach ($issue in $issues) {
        $ticketFailures = [System.Collections.Generic.List[string]]::new()
        $labelNames = @($issue.labels | ForEach-Object { ([string]$_.name).Trim().ToLowerInvariant() })

        if ($labelNames -notcontains "ready-for-agent") {
            Add-TicketFailure $ticketFailures "missing ready-for-agent"
        }

        $difficultyCount = 0
        $priorityCount = 0
        foreach ($issueLabel in @($issue.labels)) {
            $name = ([string]$issueLabel.name).Trim()
            if ($name -match '^(?i:difficulty)[/:]\s*(.*)$') {
                ++$difficultyCount
                $value = $Matches[1].ToLowerInvariant()
                if ($value -notin $supportedDifficulties) {
                    Add-TicketFailure $ticketFailures "unsupported difficulty label '$name'"
                }
            }

            if ($name -match '^(?i:priority)[/:]\s*(.*)$') {
                ++$priorityCount
                $value = $Matches[1].ToLowerInvariant()
                if ($value -notin $supportedPriorities -and $value -notmatch '^\d+$') {
                    Add-TicketFailure $ticketFailures "unsupported priority label '$name'"
                }
            }
        }

        if ($difficultyCount -eq 0) {
            Add-TicketFailure $ticketFailures "missing difficulty label"
        }
        if ($priorityCount -eq 0) {
            Add-TicketFailure $ticketFailures "missing priority label"
        }

        $blockedByText = Invoke-Gh @(
            "api", "repos/$repository/issues/$($issue.number)",
            "--jq", ".issue_dependencies_summary.blocked_by // 0"
        )
        $blockedBy = [int]$blockedByText.Trim()
        if ($blockedBy -gt 0) {
            ++$blockedTicketCount
        }

        if ($ticketFailures.Count -eq 0) {
            Write-Host "PASS #$($issue.number): $($issue.title) (blocked by $blockedBy)"
        } else {
            Write-Error "FAIL #$($issue.number): $($issue.title)" -ErrorAction Continue
            foreach ($failure in $ticketFailures) {
                Write-Error "  - $failure" -ErrorAction Continue
            }
            $failureCount += $ticketFailures.Count
        }
    }

    if ($blockedTicketCount -eq 0) {
        Write-Error "At least one matching ticket must have native GitHub blocked_by metadata." -ErrorAction Continue
        ++$failureCount
    }

    if ($failureCount -gt 0) {
        Write-Error "Validation failed with $failureCount problem(s)." -ErrorAction Continue
        exit 1
    }

    Write-Host "Validation passed: $($issues.Count) ticket(s), $blockedTicketCount blocked ticket(s)."
    exit 0
} catch {
    Write-Error $_.Exception.Message -ErrorAction Continue
    exit 2
}
