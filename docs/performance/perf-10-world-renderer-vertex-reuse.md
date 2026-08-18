# PERF-10 world-renderer vertex/index representation evidence

Measured on 2026-08-17 in an MSVC x64 Release build. The repeatable fixture is
`boolean_world_triangle_data_provider_benchmark`: an 8,192-triangle tiled floor
partitioned across four material meshes. Shared vertices are reusable only
inside a material mesh and only when the complete GPU vertex (position, normal,
UV, and colour) is bit-identical.

Run with:

```text
cmake --build build-cmake --config Release --target boolean_world_triangle_data_provider_benchmark
build-cmake/bin/Release/boolean_world_triangle_data_provider_benchmark/boolean_world_triangle_data_provider_benchmark.exe
```

| Representation | Persistent allocation / upload |
| --- | ---: |
| Current: three vertices plus three sequential 32-bit indices per triangle | 0.94 MiB |
| Non-indexed: three vertices per triangle | 0.84 MiB |
| Indexed with safe complete-vertex reuse | 0.25 MiB |

Non-indexed drawing removes the redundant sequential index stream and saves
10%, but safe reuse saves 73% on this fixture while preserving indexed drawing.
The implemented approach therefore retains 32-bit indices and reuses vertices
by their complete attributes within each material mesh. The temporary lookup is
discarded and the vertex allocation compacted before upload, so its build-time
state is not retained by the renderer.

A wall quad independently improves from six vertices and six indices (240
bytes) to four vertices and six indices (168 bytes), while normal and UV seams
remain distinct. Floors, ceilings, border walls, and step walls all use the
same complete-vertex identity rule.
