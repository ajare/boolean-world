# PERF-3 flat acceleration-grid cell evidence

Measured on 2026-08-17 in an MSVC x64 Release build. The repeatable fixture is
`willpower_acceleration_grid_benchmark`: a 128x128 `AccelerationGrid` containing
8,192 items, followed by 10,000 deterministic queries spanning 1x1 through
17x17 cells. Ten repetitions are averaged.

The benchmark runs both representations over identical cell contents and
checks an identical result checksum. Its "before" path reconstructs the
previous implementation exactly for the audited operations: `std::set` per
cell and a fresh `std::set` result populated for every query. Its "after" path
uses the production flat sorted-vector cells and reuses one caller-owned result
vector.

Run with:

```text
cmake --build build-cmake --config Release --target willpower_acceleration_grid_benchmark
build-cmake/bin/Release/willpower_acceleration_grid_benchmark/willpower_acceleration_grid_benchmark.exe
```

| Cell/query representation | Mean query batch |
| --- | ---: |
| Before: `std::set` cells and returned tree | 92.54 ms |
| After: sorted-vector cells and reused vector | 28.72 ms |

The flat implementation was **3.22x faster** and produced the same 469,633-item
checksum. Unlike the old path, repeated production queries retain the caller's
vector capacity; `willpower_acceleration_grid_set_operations_tests` verifies
that contract for both `AccelerationGrid` and `ExtendedAccelerationGrid`.
