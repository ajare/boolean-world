# BW-11 arrangement broad-phase evidence

Measured on 2026-08-16 in an MSVC x64 Release build. The repeatable fixture is
`boolean_world_arrangement_broad_phase_benchmark`: 2,000 disjoint square
contours (8,000 segments) spread over the world. This isolates the audited
sparse case where exhaustive tests do no useful work.

Run with:

```text
cmake --build build --config Release --target boolean_world_arrangement_broad_phase_benchmark
build/bin/Release/boolean_world_arrangement_broad_phase_benchmark/boolean_world_arrangement_broad_phase_benchmark.exe
```

| BuildPSLG implementation | Median of 5 | Exact segment-pair tests | Exact point-segment tests |
| --- | ---: | ---: | ---: |
| Before: exhaustive loops at `b85b249` | 276.6 ms | 31,996,000 | 192,000,000 |
| After: grid broad phase | 8.49 ms | 11,560 | 36,320 |

The broad phase was **32.6x faster** on this fixture while rejecting 99.96% of
segment pairs and 99.98% of snapped-point re-checks before exact predicates.
`boolean_world_arrangement_broad_phase_tests` also checks that the fixture's
PSLG is unchanged and that crossing segments still create and split at their
exact intersection.

As an end-to-end check, the existing `profiler` target generated
`src/BooleanWorld/app/resources/stress-test-1.yaml` ten times after five warm-up
runs. Median generation time moved from 2.95 ms before to 2.78 ms after. This
world is already small enough that arrangement construction is not its dominant
cost, but it shows no regression outside the isolated audited case.
