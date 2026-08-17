# PERF-9 exact primitive-picking evidence

Measured on 2026-08-17 in an MSVC x64 Release build. The repeatable fixture is
`boolean_world_world_primitive_picking_benchmark`: 2,304 small primitives laid
out sparsely in a 48x48 grid, with one exact centre query per primitive. This
models repeated editor picking while isolating the audited linear candidate
scan and per-query triangulation.

The benchmark runs the former linear lookup with a newly built triangulation
for each bounds candidate and the production primitive-grid lookup with cached,
invalidation-safe triangulations over identical queries. It verifies identical
result checksums and reports the median of five runs.

Run with:

```text
cmake --build build-cmake --config Release --target boolean_world_world_primitive_picking_benchmark
build-cmake/bin/Release/boolean_world_world_primitive_picking_benchmark/boolean_world_world_primitive_picking_benchmark.exe
```

| Exact primitive-picking lookup | Median batch of 2,304 queries |
| --- | ---: |
| Before: linear scan and per-query triangulation | 260.293 ms |
| After: primitive grid and cached triangulations | 2.973 ms |

The accelerated lookup was **87.5x faster** on the sparse fixture while
producing the same result checksum. The focused test also covers stable index
ordering, ignored indices passed by const reference, multi-picking, cache
invalidation after a primitive geometry change, and picking a primitive outside
the configured grid without weakening the previous fallback behavior.
