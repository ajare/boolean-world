# PERF-8 per-mesh renderer-buffer evidence

Measured on 2026-08-17 in an MSVC x64 Release build. The repeatable fixture is
`boolean_world_triangle_data_provider_benchmark`: a 4,096-triangle world split
evenly across 64 material meshes. This isolates the audited case where the
former renderer allocated every mesh's vertex and index buffers for all world
triangles, rather than the triangles assigned to that mesh.

The benchmark reconstructs the former world-sized allocation, then invokes the
production per-mesh allocation and checks that its total capacity equals the
sum of the mesh triangle counts.

Run with:

```text
cmake --build build-cmake --config Release --target boolean_world_triangle_data_provider_benchmark
build-cmake/bin/Release/boolean_world_triangle_data_provider_benchmark/boolean_world_triangle_data_provider_benchmark.exe
```

| Renderer buffer allocation | Capacity for 64 meshes / 4,096 triangles |
| --- | ---: |
| Before: world-sized buffer for every mesh | 30.00 MiB |
| After: buffer sized for each mesh | 0.47 MiB |

The per-mesh counting pass reduces this evenly distributed case by **64x**
while preserving the same floor, ceiling, and wall triangle assignments. The
focused data-provider test covers distinct mesh capacities and an unused mesh
that must allocate no buffers.
