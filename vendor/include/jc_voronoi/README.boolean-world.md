# jc_voronoi vendoring record

Boolean World vendors `src/jc_voronoi.h` from
[JCash/voronoi](https://github.com/JCash/voronoi) release **v0.10.2**, commit
`02a1639ef33d0558e959ee9fa5431e9679e739eb` (2026-07-25).

- Header SHA-256: `a4152e4b9d7b539b83f9caa7d744505caa11541d409be9e5b6ee15f0b279773e`
- License: MIT; the unmodified upstream `LICENSE` is retained beside the header.
- Local modifications: none.

The reviewed integration surface is the box-clipped `jcv_diagram_generate` API,
input-index retention, per-site counter-clockwise edge iteration, duplicate-site
pruning behavior, and diagram lifetime. `PrimitiveFieldLayout.cpp` validates
inputs before calling the library, checks that no site was pruned, validates and
canonicalizes every bounded cell, and frees the diagram through an RAII guard.
No `jcv_*` type appears in a public Boolean World header.
