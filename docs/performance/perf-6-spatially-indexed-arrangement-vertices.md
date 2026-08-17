# PERF-6 spatially indexed arrangement-vertex evidence

Measured on 2026-08-17 in an MSVC x64 Release build. The repeatable fixture is
`boolean_world_arrangement_vertex_lookup_benchmark`: 16,384 arrangement
vertices laid out as a sparse 128x128 grid, with one radius-0.25 nearest-vertex
query beside each vertex. This models the editor's repeated hover lookup while
isolating the audited case where each lookup formerly scanned every arrangement
vertex.

The benchmark runs the former linear lookup and the production vertex-grid
lookup over identical queries, checks identical result checksums, and reports
the median of five runs.

Run with:

```text
cmake --build build-cmake --config Release --target boolean_world_arrangement_vertex_lookup_benchmark
build-cmake/bin/Release/boolean_world_arrangement_vertex_lookup_benchmark/boolean_world_arrangement_vertex_lookup_benchmark.exe
```

| Nearest arrangement-vertex lookup | Median batch of 16,384 queries |
| --- | ---: |
| Before: linear scan | 1053.88 ms |
| After: vertex-grid index | 1.766 ms |

The indexed lookup was **596.8x faster** on the sparse fixture while producing
the same result checksum. The focused lookup test also covers an exact hit, a
radius miss, the former later-index tie break, and an arrangement vertex outside
the supplied world extents.
