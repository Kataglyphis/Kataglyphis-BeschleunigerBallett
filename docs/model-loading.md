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

## The async parse/upload split

The parse dominates load time — a measured 2802 ms of a 2818 ms load on the
bundled 27 MB `dinosaurs.obj` is device-free work — so it moves off the render
thread:

1. `AsyncModelParse` (`scene/AsyncModelParse.ixx`) runs `parseCpu` on a worker
   thread against a fresh device-free loader (`ObjLoader{}` / `GltfLoader{}`),
   dispatched by file extension.
2. When the worker finishes, `Scene::pollModelLoad` (`scene/Scene.cpp`, the
   `pendingModelParse.parsedGltf()` branch) takes the worker's loader, hands it
   to a **device-owning** uploader via `adoptParsed`, and calls `uploadParsed` —
   the only ~15 ms the frame loop pays. Both arms (glTF, OBJ) are symmetric.

So `adoptParsed` must move every field `uploadParsed` reads, including
`meshRanges` — miss one and the async path silently loads different geometry
than the synchronous one.

## One file, many meshes

A `Model` holds `std::vector<Mesh>` (`scene/Model.ixx`). A single file becomes a
multi-mesh model through a per-mesh **`MeshRange`** — a slice of the flat arrays:

```
struct MeshRange { vertexBase, vertexCount, indexStart, indexCount, triStart, triCount };
```

`parseCpu` records one range per sub-object while keeping the flat arrays intact
(so the `ObjParseUnit`/`GltfParseUnit` tests still assert on the flat getters),
and `uploadParsed` slices each range into its own `add_new_mesh`
(sub-vertices copied, sub-indices re-based by `-vertexBase`, per-range material
subset, full `materials` array shared). What a "sub-object" is differs by format:

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

## Invariants worth preserving

- `parseCpu` leaves the flat `getVertices()`/`getIndices()`/
  `getMaterialIndices()`/`getMaterials()` exactly as before the split — the CPU
  parse tests key on them, and the slice reads from them.
- Every `MeshRange` must tile the flat arrays contiguously (no gap/overlap) and
  every index in a range must stay inside that range's own vertex block, or the
  `-vertexBase` re-base in `uploadParsed` corrupts the mesh. The
  `MultiShape…`/`MultiPrimitive…RecordsPerPrimitiveMeshRanges` parse tests assert
  exactly this.
- Each mesh currently shares the full `materials` array (its `materialIndex`
  holds the original indices). Trimming to a per-mesh material subset is an
  optional optimisation tracked in the backlog; if done, it edits the same slice
  loop in both loaders (a natural moment to dedup that loop — see the campaign
  log's queued refactor).
