#!/usr/bin/env bash
# Validates that an open GitHub issue frontier is ready for ralph-loop.sh.
# Requires Bash 4+, gh, and jq.

set -uo pipefail

usage() {
    cat <<'EOF'
Usage: pre_ralph_validate.sh LABEL

Validates the executable open tickets carrying LABEL in the current GitHub
repository. LABEL is typically a feature label such as feature:portals.
Parent specification issues referenced by a ticket's "## Parent" section are
ignored, matching ralph-loop.sh.

Checks:
  1. At least one executable open ticket has LABEL.
  2. Every ticket has ready-for-agent.
  3. At least one ticket has native GitHub blocked_by metadata.
  4. Every ticket has difficulty and priority labels.
  5. Every difficulty label has a supported value.
  6. Every priority label has a supported value.

Supported difficulty values:
  trivial, small, low, medium, large, high, hard

Supported priority values:
  critical, urgent, p0, high, p1, medium, normal, p2, low, p3,
  or a non-negative integer

This repository's bare priority labels (critical, high, medium, and low) are
accepted, as are namespaced priorities. Both ':' and '/' namespace separators
are accepted, for example difficulty:large and priority:4.
EOF
}

fail_usage() {
    printf 'Error: %s\n\n' "$1" >&2
    usage >&2
    exit 2
}

die() {
    printf 'Error: %s\n' "$*" >&2
    exit 2
}

(($# == 1)) || fail_usage "Exactly one label is required."
[[ $1 != -h && $1 != --help ]] || { usage; exit 0; }
label=$1
[[ -n $label ]] || fail_usage "The label cannot be empty."

for command_name in gh jq; do
    command -v "$command_name" >/dev/null 2>&1 || die "$command_name is required but was not found on PATH."
done

repo=$(gh repo view --json nameWithOwner --jq .nameWithOwner 2>/dev/null) ||
    die "Could not infer the GitHub repository. Run this script inside a GitHub checkout."

issues=$(gh issue list --repo "$repo" --state open --label "$label" --limit 1000 \
    --json number,title,body,labels,url 2>/dev/null) || die "Could not list tickets carrying '$label'."
all_issue_count=$(jq 'length' <<<"$issues")

if ((all_issue_count == 0)); then
    printf "FAIL: No open tickets carry label '%s' in %s.\n" "$label" "$repo" >&2
    exit 1
fi

# A feature's parent specification commonly carries the feature and ready
# labels too, but ralph-loop deliberately does not execute it. Derive the same
# parent set from child tickets before validating the executable frontier.
parent_numbers=$(jq '[.[] | (.body // "") |
    capture("(?im)^## Parent\\s*\\r?\\n+\\s*#(?<number>[0-9]+)")?.number // empty |
    tonumber] | unique' <<<"$issues")
issues=$(jq --argjson parents "$parent_numbers" \
    '[.[] | select(.number as $number | ($parents | index($number) | not))]' <<<"$issues")
issue_count=$(jq 'length' <<<"$issues")
ignored_count=$((all_issue_count - issue_count))

if ((issue_count == 0)); then
    printf "FAIL: No executable open tickets carry label '%s' in %s.\n" "$label" "$repo" >&2
    exit 1
fi

printf "Validating %d executable open ticket(s) carrying '%s' in %s" "$issue_count" "$label" "$repo"
((ignored_count == 0)) || printf ' (%d parent specification issue(s) ignored)' "$ignored_count"
printf '.\n'

failures=0
blocked_ticket_count=0

while IFS= read -r issue; do
    number=$(jq -r '.number' <<<"$issue")
    title=$(jq -r '.title' <<<"$issue")
    ticket_failures=()
    difficulty_count=0
    priority_count=0

    if ! jq -e '[.labels[].name | ascii_downcase] | index("ready-for-agent") != null' <<<"$issue" >/dev/null; then
        ticket_failures+=("missing ready-for-agent")
    fi

    while IFS= read -r label_name; do
        normalized=$(printf '%s' "$label_name" | tr '[:upper:]' '[:lower:]')
        if [[ $normalized =~ ^difficulty[/:][[:space:]]*(.*)$ ]]; then
            ((++difficulty_count))
            value=${BASH_REMATCH[1]}
            if [[ ! $value =~ ^(trivial|small|low|medium|large|high|hard)$ ]]; then
                ticket_failures+=("unsupported difficulty label '$label_name'")
            fi
        fi

        if [[ $normalized =~ ^(critical|urgent|p0|high|p1|medium|normal|p2|low|p3)$ ]]; then
            ((++priority_count))
        elif [[ $normalized =~ ^priority[/:][[:space:]]*(.*)$ ]]; then
            ((++priority_count))
            value=${BASH_REMATCH[1]}
            if [[ ! $value =~ ^(critical|urgent|p0|high|p1|medium|normal|p2|low|p3|[0-9]+)$ ]]; then
                ticket_failures+=("unsupported priority label '$label_name'")
            fi
        fi
    done < <(jq -r '.labels[].name' <<<"$issue")

    ((difficulty_count > 0)) || ticket_failures+=("missing difficulty label")
    ((priority_count > 0)) || ticket_failures+=("missing priority label")

    blocked_by=$(gh api "repos/$repo/issues/$number" --jq '.issue_dependencies_summary.blocked_by // 0' 2>/dev/null) ||
        die "Could not inspect dependency metadata for #$number."
    if ((blocked_by > 0)); then
        ((++blocked_ticket_count))
    fi

    if ((${#ticket_failures[@]} == 0)); then
        printf 'PASS #%s: %s (blocked by %s)\n' "$number" "$title" "$blocked_by"
    else
        printf 'FAIL #%s: %s\n' "$number" "$title" >&2
        for failure in "${ticket_failures[@]}"; do
            printf '  - %s\n' "$failure" >&2
        done
        ((failures += ${#ticket_failures[@]}))
    fi
done < <(jq -c '.[]' <<<"$issues")

if ((blocked_ticket_count == 0)); then
    printf 'FAIL: At least one matching ticket must have native GitHub blocked_by metadata.\n' >&2
    ((++failures))
fi

if ((failures > 0)); then
    printf 'Validation failed with %d problem(s).\n' "$failures" >&2
    exit 1
fi

printf 'Validation passed: %d ticket(s), %d blocked ticket(s).\n' "$issue_count" "$blocked_ticket_count"
