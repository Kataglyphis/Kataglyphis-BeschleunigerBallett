# Model loading

How a model file becomes drawable geometry in the Vulkan engine: the two
loaders, the async parse/upload split that keeps the frame loop responsive, and
the multi-mesh flow that turns one file into per-mesh draws. This is the
architecture reference; `docs/cpp-renderer-improvements.md` carries the
chronological change log.

## Two loaders, one shape

`ObjLoader` (`scene/ObjLoader.{ixx,cpp}`) and `GltfLoader`
(`scene/GltfLoader.{ixx,cpp}`) load `.obj` and `.gltf`/`.glb` respectively into
the SAME CPU-side arrays, so everything downstream is loader-agnostic. Both
expose the same three-part interface:

- `parseCpu(path)` — reads and decodes the file into flat arrays
  (`vertices`, `indices`, per-triangle `materialIndex`, `materials`, plus a
  per-mesh `meshRanges` table, see below). Touches **no Vulkan**, so it can run
  on any thread.
- `uploadParsed()` — builds the Vulkan-side `Model` from the last `parseCpu`
  result (buffers, textures, meshes). **Must** run on the thread that owns the
  device.
- `adoptParsed(other&&)` — moves another loader's parse results in, so a
  device-owning loader can upload what a device-free worker produced without
  copying the (tens-of-MB) arrays.

`loadModel(path)` is just `parseCpu` + `uploadParsed` for the synchronous path.

### Which nodes a glTF load walks

`GltfLoader::parseCpu` walks a single scene, not every node in the document:
the document's default scene (`data->scene`) if it names one, else the first
entry of `data->scenes`. Only nodes reachable from that scene's roots are
loaded; a node no scene references is skipped. A document with no `scenes`
array at all (`cgltf_validate` permits this) falls back to every node, with a
warning, since that is the only geometry available. This mirrors the WebGPU
Rust loader's `default_scene().or_else(|| scenes().next())` rule
(`crates/webgpu_renderer/src/asset/gltf_loader.rs`) — the two must be kept in
sync.

## The async parse/upload split

The parse dominates load time — a measured 2802 ms of a 2818 ms load on the
bundled 27 MB `dinosaurs.obj` is device-free work — so it moves off the render
thread:

1. `AsyncModelParse` (`scene/AsyncModelParse.ixx`) runs `parseCpu` on a worker
   thread against a fresh device-free loader (`ObjLoader{}` / `GltfLoader{}`),
   dispatched by file extension via `isGltfModelPath` (`scene/ModelFileKind.ixx`).
   `Scene::loadModel` routes through the same predicate.
2. When the worker finishes, `Scene::pollModelLoad` (`scene/Scene.cpp`, the
   `pendingModelParse.parsedGltf()` branch) takes the worker's loader, hands it
   to a **device-owning** uploader via `adoptParsed`, and calls `uploadParsed` —
   the only ~15 ms the frame loop pays. Both arms (glTF, OBJ) are symmetric.

So `adoptParsed` must move every field `uploadParsed` reads, including
`meshRanges` — miss one and the async path silently loads different geometry
than the synchronous one.

```mermaid
flowchart LR
    subgraph worker["Worker thread (no device)"]
        P["parseCpu(path)\nflat arrays + meshRanges"]
    end
    subgraph main["Render thread (owns device)"]
        Poll["Scene::pollModelLoad\nwhen worker isFinished()"]
        A["uploader.adoptParsed(move)\ntakes the arrays, no copy"]
        U["uploader.uploadParsed()\nbuffers, textures, one Mesh per range"]
        Add["Scene::add_model\none ObjectDescription per mesh"]
    end
    Start["AsyncModelParse.start(path)"] --> P
    P -->|arrays moved| Poll --> A --> U --> Add
```

The synchronous `loadModel(path)` is the same minus the thread hop: `parseCpu`
then `uploadParsed` back-to-back on the calling (device-owning) thread.

## One file, many meshes

A `Model` holds `std::vector<Mesh>` (`scene/Model.ixx`). A single file becomes a
multi-mesh model through a per-mesh **`MeshRange`** — a slice of the flat arrays:

```
struct MeshRange {
    vertexBase, vertexCount, indexStart, indexCount, triStart, triCount, doubleSided
};
```

`doubleSided` carries glTF `material.doubleSided` per range, so `uploadParsed`
can hand it to `add_new_mesh` and the raster pass can disable back-face culling
for that mesh alone; it is always `false` for OBJ, which has no such concept.

The type lives in its own module, `kataglyphis.vulkan.mesh_range`
(`scene/MeshRange.ixx`), `export import`ed by both loaders. It exports
`MeshRange`, the `MeshSlice` return type, and `sliceMeshRange` — the single
shared implementation of "sub-vertices copied, sub-indices re-based by
`-vertexBase`, per-range material subset" that both loaders' `uploadParsed`
call into rather than each doing the slicing themselves.

`parseCpu` records one range per sub-object while keeping the flat arrays intact
(so the `ObjParseUnit`/`GltfParseUnit` tests still assert on the flat getters),
and `uploadParsed` slices each range via `sliceMeshRange` into its own
`add_new_mesh`. What a "sub-object" is differs by format:

- **glTF** — one range per **primitive**. Primitives already parse into disjoint
  contiguous vertex ranges, so recording the range is direct.
- **OBJ** — one range per **shape** (`o`/`g` group). OBJ shapes share one
  attribute pool and vertices are deduplicated, so `loadVertices` resets the
  dedup map **per shape** to keep each shape's vertices in a contiguous block the
  slice can address. The cost is duplicating any vertex shared across distinct
  shapes (rare between separate objects, and pixel-identical either way).

A single-primitive glTF or single-shape OBJ yields exactly one range spanning
everything — behaviour-identical to the pre-split single-mesh path.

## What downstream gets for free

Because object identity is per **mesh**, not per model, splitting a file into
meshes automatically feeds the systems that were already made mesh-aware:

- **Object descriptions** — `Scene::add_model` flattens one `ObjectDescription`
  per mesh; `objectIndex` is the flat mesh index (sum of prior models' mesh
  counts + local index), pushed per draw in the forward/deferred/shadow record
  loops.
- **Culling** — the record loops iterate meshes with per-mesh AABBs, so a
  multi-mesh model culls at mesh granularity instead of all-or-nothing.
- **Acceleration structures** — one BLAS per model with one **geometry** per
  mesh; RT/PT kernels fetch the per-mesh material with
  `instanceCustomIndex (= the model's first-mesh flat index) + gl_GeometryIndexEXT`.

## `map_Kd` texture path resolution

Both loaders resolve an OBJ material's `map_Kd` the same way:

**Normalise `\` to `/` in the `map_Kd` value; resolve the result relative to
the directory containing the `.mtl`; if that file does not exist, retry under
a `textures/` subdirectory of the same directory; if that misses too, warn
and fall back to the default texture.**

The `\`-to-`/` normalisation is a deliberate deviation from "as written in
the `.mtl`": a Windows-authored relative path (`textures\wood.png`) must
still resolve when the `.mtl` is loaded on a platform where `\` is just
another filename character. Relative-to-the-`.mtl` first is what the OBJ/MTL
format actually specifies; the `textures/` retry exists because every OBJ
shipped in `Resources/Models` (`crytek-sponza`, `Pillum`, `Sulo`/`WolfStahl`,
`VikingRoom`) puts its textures in a `textures/` sibling directory and
references them by bare filename, not by the format's own convention. A path
that resolves to neither candidate degrades silently to the untextured
default (Vulkan) or a missing image (glTF) rather than failing the whole load
- so both sides log a warning naming both candidates, which is the only
signal a wrong or missing texture leaves behind. An empty base directory (a
bare filename with no directory component) is treated as `.` on both sides,
so the candidates stay relative instead of resolving against the filesystem
root.

- **C++** (`scene/ObjLoader.cpp`, `resolveObjTexturePath`) - builds both
  candidate paths and records the first that `std::filesystem::exists`.
- **Rust** (`crates/webgpu_renderer/src/asset/obj_to_gltf.rs`,
  `convert_file`) - same two candidates, applied when copying the texture
  next to the converted glTF. The glTF `uri` it emits is always the bare
  filename as written in the `.mtl`, regardless of which candidate matched,
  so the converted document stays self-contained next to its `.bin`.

## Invariants worth preserving

- `parseCpu` leaves the flat `getVertices()`/`getIndices()`/
  `getMaterialIndices()`/`getMaterials()` exactly as before the split — the CPU
  parse tests key on them, and the slice reads from them.
- Every `MeshRange` must tile the flat arrays contiguously (no gap/overlap) and
  every index in a range must stay inside that range's own vertex block, or the
  `-vertexBase` re-base in `sliceMeshRange` corrupts the mesh. The
  `MultiShape…`/`MultiPrimitive…RecordsPerPrimitiveMeshRanges` parse tests assert
  this at the parse level, and `Test/commit/VulkanEngine/meshRangeSliceSuite.cpp`
  unit-tests `sliceMeshRange` itself directly, so the invariant has both a
  parse-level and a slice-level test.
- Each mesh currently shares the full `materials` array (its `materialIndex`
  holds the original indices). Trimming to a per-mesh material subset is an
  optional optimisation tracked in the backlog; since the slice loop is already
  shared in `sliceMeshRange`, that trim would edit exactly one place for both
  loaders.
