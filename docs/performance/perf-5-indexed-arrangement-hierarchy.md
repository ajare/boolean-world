# PERF-5 indexed arrangement-hierarchy evidence

Measured on 2026-08-17 in an MSVC x64 Release build. The repeatable fixture is
`boolean_world_arrangement_hierarchy_benchmark`: 8,000 disjoint square cycles
spread over a 100x80 grid. This isolates the audited sparse case where the
former hierarchy loop checked every other cycle's bounding box even though no
cycle can be a parent.

The benchmark runs the former linear candidate scan and the production indexed
implementation over identical cycles, checks identical hierarchy checksums,
and reports the median of five runs. The before implementation uses the
fixture's square bounds directly; omitting the old sample-point triangulation
makes its timing conservative.

Run with:

```text
cmake --build build-cmake --config Release --target boolean_world_arrangement_hierarchy_benchmark
build-cmake/bin/Release/boolean_world_arrangement_hierarchy_benchmark/boolean_world_arrangement_hierarchy_benchmark.exe
```

| Hierarchy candidate search | Median of 5 | Candidate-box checks | Point-in-cycle tests |
| --- | ---: | ---: | ---: |
| Before: linear scan | 147.38 ms | 63,992,000 | 0 |
| After: acceleration-grid index, area-ordered candidates | 6.91 ms | 8,000 | 0 |

The indexed implementation was **21.3x faster** and rejected 99.987% of
candidate-box checks on this fixture. Candidates returned by the index are
sorted by increasing cycle area, so the first exact containing cycle is the
same smallest parent selected by the former full scan. The focused hierarchy
test also covers nested cycles, equal bounding-box bounds, and sparse candidate
rejection.
