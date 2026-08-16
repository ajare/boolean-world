# Triage Labels

The skills speak in terms of five canonical triage roles. This file maps those roles to the actual label strings used in this repo's issue tracker.

| Label in mattpocock/skills | Label in our tracker | Meaning                                  |
| -------------------------- | -------------------- | ---------------------------------------- |
| `needs-triage`             | `needs-triage`       | Maintainer needs to evaluate this issue  |
| `needs-info`               | `needs-info`         | Waiting on reporter for more information |
| `ready-for-agent`          | `ready-for-agent`    | Fully specified, ready for an AFK agent  |
| `ready-for-human`          | `ready-for-human`    | Requires human implementation            |
| `wontfix`                  | `wontfix`            | Will not be actioned                     |

When a skill mentions a role (e.g. "apply the AFK-ready triage label"), use the corresponding label string from this table.

Edit the right-hand column to match whatever vocabulary you actually use.

## Which of these exist on the repo

Snapshot of `ajare/boolean-world` taken 2026-08-16 — re-check with
`gh label list` rather than trusting this list.

- **`wontfix`** exists already (a GitHub default label on new repos).
- **`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`** do not
  exist yet and must be created before `/triage` can apply them:

  ```sh
  gh label create needs-triage    --description "Maintainer needs to evaluate this issue"  --color d4c5f9
  gh label create needs-info      --description "Waiting on reporter for more information" --color fef2c0
  gh label create ready-for-agent --description "Fully specified, ready for an AFK agent"  --color 0e8a16
  gh label create ready-for-human --description "Requires human implementation"            --color 1d76db
  ```

The repo's other labels — `bug`, `enhancement`, `documentation`, `duplicate`,
`help wanted`, `good first issue`, `invalid`, `question`, `accessibility` — are
unrelated to triage roles and are left alone.
