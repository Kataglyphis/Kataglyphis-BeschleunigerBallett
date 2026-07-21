# The bounds invariant (WebGPU renderer)

Eight separate bugs in this renderer were the same bug. Writing the rule down so
there is no ninth.

## The rule

> **World bounds must cover every pose the geometry can actually reach — not the
> pose it was authored in.**

Bounds are not decoration. They are the *input* to decisions that delete
geometry from the frame. If bounds describe a pose the GPU is not drawing, the
renderer confidently discards things that are on screen, and the symptom appears
nowhere near the cause.

## Why it keeps happening

Every one of these features moves vertices **on the GPU, at draw time**, while
the bookkeeping that describes where those vertices *are* lives on the CPU and
was written before the feature existed:

| Feature | Moves geometry by | Bookkeeping that forgot |
| --- | --- | --- |
| Morph targets | per-frame vertex re-blend | local AABB, scene bounds |
| Skinning | joint matrices in the shader | local AABB (bind pose only) |
| Instancing | per-instance matrix in the shader | per-primitive AABB, scene bounds |
| LOD | swapping the vertex buffer | drew the *un-morphed* simplified buffer |

The author of each feature updated the thing they were thinking about and missed
the other consumers, because nothing named them in one place. That is what this
document is for.

## Consumers — everything that reads bounds

Change any of these and you inherit the invariant:

- **Frustum culling** (`Frustum::intersects_aabb`) — wrong bounds delete a
  visible object outright.
- **Shadow-caster culling** (`intersects_aabb_as_caster`) — wrong bounds delete
  its *shadow*, which looks like a lighting bug, not a culling bug.
- **Cascade fitting** (`update_cascades` via `scene_bounds`) — this one is
  scene-wide: bad bounds misfit *all three* cascades, so unrelated objects lose
  their shadows.
- **LOD selection** (`geometry_for`, via `world_center`).
- **Transparent sort order** (via `world_center`).
- **Occlusion proxy boxes** — the box drawn *is* the AABB.

Note the last three consume `world_center`, which must stay **one metric**. It
was briefly the vertex centroid at upload and the AABB centre afterwards, so a
`set_animation_time(0.0)` that moved nothing could still flip an LOD level and
reorder blending.

## Maintainers — everything that must update bounds

There are exactly three places, and **all three must handle all of it**:

1. `upload_scene` — the initial pose, including joints already posed away from
   the bind pose (a scene with no animations never reaches the animation path).
2. `set_animation_time` — node transforms, skinning, morph weights.
3. `set_instances` — instance transforms, **and `scene_bounds`**, which was the
   eighth bug: the per-primitive AABB was updated and the scene bounds were not.

Helpers exist so this is composition rather than repetition:
`primitive_local_aabb` (covers morph), `widen_bounds_for_skin`,
`instanced_bounds`, `recompute_scene_bounds`.

`recompute_scene_bounds` is deliberately *one function every mutator calls*,
because the bug it fixes was two call sites disagreeing about whose job it was.

## How to be conservative correctly

Bounds may be **too big** — that only costs a little culling efficiency. Bounds
that are **too small delete visible geometry**. So when a pose is not known
exactly, over-cover it:

- **Morph:** for weights in `[0,1]`, a vertex's extremes are its position plus
  the sum of the negative deltas (lower) and the positive deltas (upper). Exact
  over that range, conservative outside it.
- **Skinning:** skin weights are normalised, so a skinned position is a *convex
  combination* of `J_i · v` and therefore lies inside the **union of the
  per-joint boxes**. Union them.
- **Instancing:** union `transform_aabb(instance_i, posed_box)` over instances.

Each of those is a proof, not a fudge factor — which is why none of them needs a
magic epsilon.

## Non-finite input

Bounds are also where bad data does the most damage, because it *spreads*:

- One non-finite vertex NaNs the primitive bounds → the scene bounds → the
  cascade radius → **all three cascade matrices**, so shadows break for every
  object in the scene, not just the bad mesh.
- `Frustum::test_planes` treats NaN as *visible*, so the offending primitive
  also never culls.

Both AABB helpers therefore skip non-finite vertices rather than propagate them.

## Adding a feature that moves geometry

1. Which of the consumers above can now be wrong?
2. Do all three maintainers cover it — including `upload_scene`, for scenes with
   no animation?
3. Is the over-cover argument a proof, or a guess?
4. Write the test against the bounds the frustum test *actually reads*
   (`primitive_world_aabb`, `scene_bounds`), not a recomputed copy.

### One warning about testing this

Several of these bugs are invisible to the obvious test. Confirm your test
**fails before the fix** — twice during this work it did not:

- A cube cannot detect a wrong normal transform under a diagonal scale: its
  normals are axis-aligned, so the correct and incorrect matrices normalise to
  the same direction. Compose the scale with a rotation.
- A camera inside a closed, back-face-culled mesh renders *nothing*, so the
  depth buffer stays empty and an occlusion proxy passes regardless — the strobe
  it was meant to reproduce cannot occur with that geometry.
