# PERF-12 primitive-removal membership evidence

Measured on 2026-08-17 in an MSVC x64 Release build. The repeatable fixture is
`boolean_world_world_primitive_removal_benchmark`: 20,000 primitives with
10,000 unordered selected indices. This isolates `World::removePrimitives`'
audited membership check while modelling a large editor multi-selection.

The benchmark compares the former `std::find` membership scan for every
primitive with the production sorted two-pointer sweep. It verifies identical
retained-primitive checksums and reports the median of five runs.

Run with:

```text
cmake --build build-cmake --config Release --target boolean_world_world_primitive_removal_benchmark
build-cmake/bin/Release/boolean_world_world_primitive_removal_benchmark/boolean_world_world_primitive_removal_benchmark.exe
```

| Primitive-removal membership check | Median batch of 20,000 primitives |
| --- | ---: |
| Before: linear membership scan per primitive | 9.437 ms |
| After: sorted two-pointer sweep | 0.148 ms |

The two-pointer sweep was **63.6x faster** on the unordered multi-selection
fixture while retaining the same primitives. The focused test also verifies
that unordered indices preserve the remaining primitive order and compacted
primitive IDs.
