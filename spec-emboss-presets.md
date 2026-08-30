## Problem Statement

Embossing is currently embedded in a Sub-material, so every surface using that Sub-material receives identical relief. Authors cannot use one Sub-material on a Primitive's floor and ceiling while embossing only its floor, and cannot reuse one relief definition across multiple ProcMaterial catalogs.

## Solution

Provide one global Embossing catalog containing named, stable-id Emboss presets. A Primitive property set independently assigns a Sub-material id and an optional Emboss-preset id to each floor, ceiling, and wall. The editor's selected-surface panel provides an Embossing collapsible header to select a preset, edit it, update it, or save it as a new preset. An empty preset assignment renders no embossing.

## User Stories

1. As a world author, I want one reusable Emboss preset catalog, so that relief definitions are shared across ProcMaterial catalogs.
2. As a world author, I want to assign an Emboss preset independently to a floor, ceiling, or wall, so that one Primitive can use the same Sub-material with different relief per surface.
3. As a world author, I want to leave a surface's Emboss preset empty, so that it renders without embossing.
4. As a world author, I want to select an existing preset from the selected 3D-preview surface, so that I can apply relief to only that surface.
5. As a world author, I want to edit a selected preset in the Embossing collapsible header, so that I can tune its pattern and parameters.
6. As a world author, I want to update an existing preset without changing its stable id, so that every assigned surface keeps its reference.
7. As a world author, I want to save edited settings as a new preset, so that I can create a variant without changing existing surfaces.
8. As a world author, I want the preview to immediately show the selected surface's assigned preset, so that material and relief choices can be evaluated together.
9. As a world author, I want a floor and ceiling that share a Sub-material to remain separately bucketed when only one has an Emboss preset, so that rendering is correct.
10. As a world author, I want renamed presets to retain stable references, so that renaming is safe.
11. As a world author, I want invalid preset values rejected by both editor and loader, so that authored data is renderable.
12. As a world author, I want `world-test-1.yaml` migrated once, so that its existing appearance is retained.
13. As a world author, I want new World YAML and binary data to require explicit per-surface preset references, so that the old embedded-Embossing model is completely retired.
14. As a runtime user, I want missing preset ids to render as no embossing, so that unresolved optional authoring data is safe.
15. As a maintainer, I want Chips to remain owned by Sub-materials, so that this change affects only Embossing ownership.

## Implementation Decisions

- Add an Emboss preset value object: stable id, display name, and `EmbossData`; its id is unique in the sole global Embossing catalog.
- Add a single `EmbossingCatalog` resource backed by one YAML payload and register/load it alongside ProcMaterial resources. It is not nested in or owned by a ProcMaterial catalog.
- Remove authored Embossing from Sub-material serialization, validation, editing, and resolver output. Chip settings remain on Sub-materials.
- Extend `PrimitivePropertySet` with floor, ceiling, and wall Emboss-preset ids. New YAML and binary serializers require these fields; no compatibility read path is retained.
- Resolve render material definitions from a pair of ids: the surface Sub-material id supplies Technique, parameters, colour, and Chips; the surface Emboss-preset id supplies relief. The pair remains part of mesh-bucket identity.
- Extend all floor, ceiling, wall, detail, and preview render paths to pass the matching pair; wall Chip eligibility continues to resolve from its wall Sub-material only.
- Add editor-side direct-file loading/saving, snapshots, undoable actions, id creation, rename, and deletion/reference checks for the one catalog, analogous to existing Sub-material authoring but independent of ProcMaterial libraries.
- The selected-surface panel owns an `Embossing` collapsible header containing preset selection, editable preset fields, `Save existing`, `Save as new Emboss preset`, and `Revert`. Selecting/assigning a preset changes only the selected Primitive surface.
- Perform a one-off source migration of `world-test-1.yaml`: extract existing built-in Sub-material emboss definitions into the global catalog and assign matching ids to the fixture's affected surfaces. No general legacy World migration exists.
- Supersede ADR-0023 only for Embossing ownership, as recorded in ADR-0035.

## Testing Decisions

- Prefer existing high seams: core serialization tests for the hard-break World property shape; resolver/render-data tests for `(Sub-material id, Emboss-preset id)` composition; editor interaction tests for assignment, undo/redo, save/update/save-as-new, and deletion protection; preview smoke tests for independently embossed surfaces sharing one Sub-material.
- Tests assert observable persisted data, resolved render definitions, mesh-bucket distinction, and selected-surface behavior rather than helper internals.
- Extend the existing ProcMaterial, Sub-material resolver, primitive property serialization, editor picker, preview-surface, and world-renderer smoke test families rather than creating low-level duplicate seams.
- Test the one-off fixture migration as checked-in data: it must contain per-surface preset references and preserve each prior relief definition in the new global catalog.

## Out of Scope

- Multiple Embossing catalogs, per-ProcMaterial Emboss preset catalogs, and per-surface numeric overrides.
- Migration of arbitrary existing World YAML or binary files.
- Moving Chip generation, wall normal-map overrides, or Technique parameters out of Sub-materials.
- New Emboss patterns or changes to shader relief mathematics.

## Further Notes

An empty Emboss-preset id is the canonical no-relief state. The hard format break applies to both World YAML and binary serialization; only the checked-in `world-test-1.yaml` receives a one-off migration.
