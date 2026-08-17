# Domain Docs

How the engineering skills should consume this repo's domain documentation when
exploring the codebase.

This is a **single-context** repo: one `CONTEXT.md` and one `docs/adr/` at the
root.

## Before exploring, read these

- **`CONTEXT.md`** at the repo root.
- **`docs/glossary.md`** — this repo keeps its domain vocabulary in a separate
  glossary rather than inside `CONTEXT.md`. Read it wherever these rules refer
  to "the glossary".
- **`docs/adr/`** — read ADRs that touch the area you're about to work in.
  `docs/adr/README.md` indexes them and records the scope of the current
  geometry rewrite.

If any of these files don't exist, **proceed silently**. Don't flag their
absence; don't suggest creating them upfront. The `/domain-modeling` skill
(reached via `/grill-with-docs`) creates them lazily when terms or decisions
actually get resolved.

`CONTEXT.md` does not exist yet. That is expected, not a gap to fix on sight.

## File structure

```
/
├── CONTEXT.md
├── docs/
│   ├── glossary.md            ← domain vocabulary
│   └── adr/
│       ├── README.md          ← index, scope, defects, consumers
│       ├── 0001-preserve-priority-ordered-fold.md
│       ├── 0002-single-arrangement-replaces-sequential-booleans.md
│       └── …
└── src/AppLib/, src/BooleanWorld/, src/Launcher/, ext/willpower/
```

## Use the glossary's vocabulary

When your output names a domain concept (in an issue title, a refactor
proposal, a hypothesis, a test name), use the term as defined in
`docs/glossary.md`. Don't drift to synonyms the glossary explicitly avoids.

The glossary distinguishes terms that mean different things before and after
the geometry rewrite — for example `ClippedPolygon` and `WorldVertexData` are
recorded as being removed, and `face`, `membership` and `step edge` as their
replacements. Use the sense that matches the code you are actually touching,
and say which you mean when it could be read either way.

If the concept you need isn't in the glossary yet, that's a signal — either
you're inventing language the project doesn't use (reconsider) or there's a
real gap (note it for `/domain-modeling`).

## Flag ADR conflicts

If your output contradicts an existing ADR, surface it explicitly rather than
silently overriding:

> _Contradicts ADR-0007 (remove culling) — but worth reopening because…_

This repo has nine accepted ADRs covering the World geometry rewrite, several
of which rule *out* approaches that would otherwise look reasonable —
reintroducing culling (0007), diffing polygons to validate (0008), or adding a
compatibility shim for the old output contract (0004). Check before proposing
one of those.
