# W3D Shader / Texture / Mapping Cost Reference (DX8 engine)

A grounded reference for the relative cost and behavior of the W3D shader,
texture, and stage-mapping options as they are actually implemented in this
engine's DirectX 8 renderer. Every claim below is traced to the engine source,
not generic GPU folklore.

Primary sources:
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/shader.cpp` — `ShaderClass::Apply`, `Get_SS_Category`, `Guess_Sort_Level`
- `Core/Libraries/Source/WWVegas/WW3D2/texturefilter.cpp` / `texture.cpp` — texture filter + address mode
- `Core/Libraries/Source/WWVegas/WW3D2/MAPPERS.TXT` — UV mapper catalog

The Material Viewer exposes these under **View → W3D Shaders**, the texture
options, and the per-stage UV mapping type.

---

## TL;DR — the cost hierarchy

Expensive things, in order:

1. **Bump-env** (shader gradient + `BUMPENV` mapper) — consumes a texture stage as a bump lookup.
2. **Multitexture detail stages** (Detail Color / Detail Alpha funcs) — a second per-pixel texture sample.
3. **Anisotropic filtering** — up to N× texel fetches.
4. **Alpha blend's forced CPU depth sort** — not the per-pixel work, the per-frame sort.

Everything else is essentially free render-state that only matters because of the
**category** it puts the surface in: sorted vs. not, lit vs. not, static vs.
dynamic, depth-writing vs. not.

---

## Alpha test vs. alpha blend (and the combined mode)

The dominant cost difference is **not** per-pixel work — it is whether the engine
must **depth-sort** the geometry every frame. This is decided by
`Get_SS_Category` + `Guess_Sort_Level` (`shader.cpp:1138`):

```cpp
case SSCAT_OPAQUE:
case SSCAT_ALPHA_TEST:   sort_level = SORT_LEVEL_NONE;   // NOT sorted, batched like opaque
case SSCAT_SCREEN:       sort_level = SORT_LEVEL_BIN2;
case SSCAT_ADDITIVE:     sort_level = SORT_LEVEL_BIN3;
default:                 sort_level = SORT_LEVEL_BIN1;    // sorted every frame
```

And critically (`shader.cpp:1144`), **alpha-blend-AND-test is classified as
`SSCAT_ALPHA_TEST`**, inheriting the no-sort cheapness:

```cpp
if (ALPHATEST_ENABLE == Get_Alpha_Test()) {
    if (DSTBLEND_ZERO == Get_Dst_Blend_Func()) return SSCAT_ALPHA_TEST;       // plain alpha test
    if (SRCBLEND_SRC_ALPHA == Get_Src_Blend_Func() &&
        DSTBLEND_ONE_MINUS_SRC_ALPHA == Get_Dst_Blend_Func())
        return SSCAT_ALPHA_TEST;                                              // blend + test too!
}
```

| Menu option | Sort category | CPU sort/frame | Writes depth | Per-pixel GPU | Edges |
|---|---|---|---|---|---|
| **Alpha Test** | `SSCAT_ALPHA_TEST` | **No** | Yes | cheapest (compare + kill) | hard / aliased |
| **Alpha Blend and Test** | `SSCAT_ALPHA_TEST` | **No** | Yes | test kill + blend on survivors | softened edge |
| **Alpha Blend** | `SSCAT_OTHER` | **Yes** (BIN1) | No | blend read-modify-write | smooth |
| Additive | `SSCAT_ADDITIVE` | Yes (BIN3) | No | blend | smooth glow |

Per-pixel mechanics (`shader.cpp:509-530`):
- **Alpha test** sets `D3DRS_ALPHATESTENABLE` + `D3DCMP_GREATEREQUAL` against
  `D3DRS_ALPHAREF` (0x60). One compare, kills the pixel if it fails — no
  framebuffer read, writes depth. Cheap.
- **Alpha blend** sets `D3DRS_ALPHABLENDENABLE`; every surviving pixel does a
  framebuffer **read-modify-write** (`src*srcblend + dst*dstblend`). The read is
  the expensive part, worse under overdraw.

**Takeaway:** *Alpha Blend and Test* is cheaper at the engine level than plain
*Alpha Blend* (no CPU sort, keeps batching, writes depth) even though it does more
per-pixel work — but it is only correct when the alpha is near-binary (cutout
with a soft edge). Genuinely layered transparency (glass over glass, smoke) still
needs plain *Alpha Blend* and its sort.

---

## Other shader options

All from `ShaderClass::Apply`.

### Depth Compare (`D3DRS_ZFUNC`, `shader.cpp:1067`)
The Z-test function (Never / Less / Equal / **LessEqual** (default) / Greater /
NotEqual / GreaterEqual / Always).
- **Effectively free** — one fixed-function render state.
- `PASS_LEQUAL` (default) is the *cheapest in practice* because it lets
  hierarchical-Z cull hidden fragments. `PASS_ALWAYS` disables early-Z rejection —
  never use it on opaque geometry.

### Depth Mask / Write Z Buffer (`D3DRS_ZWRITEENABLE`, `shader.cpp:1070`)
Whether the pixel writes depth.
- Free to set, but has scene-wide consequences: writing depth (opaque default) is
  what lets later geometry be occluded. Disabling it (translucent/additive) is
  correct for glass/glow but is *why* those surfaces land in the sorted bins.

### Primary Gradient (`shader.cpp:614`) — texture × vertex lighting
| Option | D3D op | Cost |
|---|---|---|
| **Modulate** (default) | `D3DTOP_MODULATE` (texture × diffuse) | baseline, free |
| **Disable / Decal** | `D3DTOP_SELECTARG1` (texture only, unlit) | *cheaper* — skips the multiply |
| **Add** | `D3DTOP_ADD` (texture + diffuse) | same as modulate; falls back to modulate without `D3DTEXOPCAPS_ADD` |
| **Bump Env / Bump Env Luminance** | `D3DTOP_BUMPENVMAP[LUMINANCE]` | **heaviest** — consumes a texture stage; source comments call it "a hack ... we only have two stages." Falls back to flat diffuse if unsupported. |

### Secondary Gradient (`D3DRS_SPECULARENABLE`, `shader.cpp:1064`)
Enables the specular color channel.
- **Cheap** — one render state, fixed-function specular, one extra gradient
  interpolation, no extra pass.

### Detail Color / Detail Alpha functions (secondary texture stage, `shader.cpp:~1000-1058`)
Control a **second texture stage**. Anything but "disable" = multitexturing.
- **Real per-pixel multiplier** — roughly doubles texture-sampling and fill for
  that surface (one fetch vs. two+ per pixel) on the 2-4 stage DX8 target.
- Not live-previewed in the Material Viewer (save-only "Advanced" options).

### Cull Mode / Double-Sided (`D3DRS_CULLMODE`, `shader.cpp:1082`)
- Free to set, but **Double-Sided ≈ 2× pixel work** — back faces that would be
  culled now go through the full pixel pipeline. The cull *test* is free;
  disabling it is what costs.

---

## Texture options

### Filter (min / mag / mip — `D3DTSS_MINFILTER/MAGFILTER/MIPFILTER`)
| Filter | Cost |
|---|---|
| **Point / None** | cheapest — 1 texel fetch, no interpolation |
| **Bilinear** | 4 fetches + lerp — standard baseline |
| **Trilinear** | 8 fetches (two mips blended) — modest, removes mip-seam popping |
| **Anisotropic (2x-16x)** | **heaviest** — up to N× samples along the anisotropy direction |

### Clamp U / Clamp V (address mode — `D3DTSS_ADDRESSU/ADDRESSV`)
- **Free** — WRAP vs CLAMP is the same fixed-function addressing, no cost
  difference. Decoded once at load from the `.w3d` `W3DTEXTURE_CLAMP_U/V` bits into
  the `TextureFilterClass`, shared by every mesh using the texture. (Live-previewed
  in the Material Viewer via a private texture copy so the shared original is not
  mutated.)

### Mip settings (No-LOD / mip-level count — a **load-time** attribute)
- Cost is inverted from intuition: *disabling* mips (No-LOD) is **worse at
  runtime** — a minified texture without mips thrashes the texture cache. Mips cost
  ~33% more memory but save bandwidth. Mips-on is the fast path; No-LOD is for
  UI/sprites that are never minified.

---

## Stage mapping (UV mappers)

Source of truth: `mapper.{h,cpp}` (WW3D2) and `VertexMaterialClass::Apply`
(`vertmaterial.cpp:962`). The cost model here is the opposite of the intuitive
"mappers chew UVs on the CPU" — reading the code, the CPU work is O(1) per mapper
and the actual UV transform happens on the **GPU**.

### How a mapper actually works

A mapper does NOT touch per-vertex UVs on the CPU. Every mapper's `Apply()` does
just three things (see `ScaleTextureMapperClass::Apply`, `mapper.cpp:88`):

1. `Calculate_Texture_Matrix(m)` — build one **4×4 texture matrix** with a handful
   of scalar ops.
2. `DX8Wrapper::Set_Transform(D3DTS_TEXTURE0 + Stage, m)` — hand that matrix to the
   fixed-function pipeline.
3. Set `D3DTSS_TEXCOORDINDEX` + `D3DTSS_TEXTURETRANSFORMFLAGS = D3DTTFF_COUNT2`.

The **GPU's fixed-function texture-transform stage** then multiplies every vertex's
UV by that matrix during T&L — per-vertex, but on the GPU, essentially free (it is
already running vertex transform). So a mapper's job is "compute one small matrix
and set a few states," not "rewrite the vertex UV array."

`Apply()` runs **once per vertex-material bind** — i.e. once per draw batch, not
once per vertex and not once globally per frame (`vertmaterial.cpp:964`). With **no**
mapper, `Apply` sets `D3DTTFF_DISABLE` (`vertmaterial.cpp:967`): the texture
transform is fully off — the genuine zero-cost path.

Animated mappers (`Is_Time_Variant()` → true) rebuild their matrix each time from
`WW3D::Get_Sync_Time()`, so the matrix differs frame to frame; static ones
(`SCALE`) build a constant matrix.

### Two flavors, two costs

**Matrix mappers** (offset / scale / rotate / sine / grid / edge / bumpenv): the
CPU builds a matrix from time and constants — a few adds, or one `Floor`/`Clamp`,
or one `sin`/`cos`. That is the entire per-batch CPU cost. Example: `LinearOffset`
is `offset += deltaPerMS * elapsed; frac(); write two matrix cells`
(`mapper.cpp:157`).

**Environment mappers** (`CLASSIC_ENVIRONMENT`, `ENVIRONMENT`, `EDGE`, the `WS_*`
and `GRID_*` env variants): these do **GPU texgen**, not CPU vector math. Their
`Apply` sets `D3DTSS_TEXCOORDINDEX` to `D3DTSS_TCI_CAMERASPACENORMAL` (classic) or
`D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR` (reflection) (`mapper.cpp:629`, `:655`), so
the **GPU** generates the UVs from each vertex's normal / reflection vector. The CPU
side is often a *constant* 0.5-bias matrix (`ClassicEnvironmentMapperClass::
Calculate_Texture_Matrix`, `mapper.cpp:636` — no time term at all). Their real cost
is a hard requirement: **vertex normals must be present in the stream**
(`Needs_Normals()` → true).

Perf impact = the real end-to-end cost in a scene, folding in the second-order
effects (needs-normals, ties to a costly shader, per-bind matrix rebuild) — not the
per-batch math, which is negligible for every mapper. Note: animation does **not**
cost batching here (see #3 below); an animated mapper just rebuilds a
non-cacheable matrix per material bind.

| Mapper | CPU per batch | GPU | Perf impact | Notes |
|---|---|---|---|---|
| **(none) / plain UV** | zero (`D3DTTFF_DISABLE`) | none | **None** | texture transform fully off |
| **SCALE** | build 1 constant matrix, once | fixed-func UV × matrix | **Negligible** | constant matrix (not time-variant) |
| **LINEAR_OFFSET** | 2 muls + `Floor`/`Clamp` | fixed-func UV × matrix | **Low** | animated: rebuilds matrix per bind |
| **STEP / ZIGZAG / RANDOM** | a few adds + branch (`RANDOM` also 1 rand) | same | **Low** | animated: rebuilds matrix per bind |
| **ROTATE** | 1 `sin`/`cos` + matrix build | same | **Low** | animated: rebuilds matrix per bind |
| **SINE_LINEAR_OFFSET** | a couple of `sin()` | same | **Low** | animated: rebuilds matrix per bind |
| **GRID** | integer frame timing → sub-rect offset | same | **Low** | animated flipbook |
| **SCREEN** | LinearOffset math (view-dependent) | same | **Low** | recomputed as camera moves |
| **CLASSIC_ENVIRONMENT** | constant 0.5-bias matrix | **GPU texgen from normal** | **Low–Med** | requires vertex normals in the stream |
| **ENVIRONMENT** | constant 0.5-bias matrix | **GPU texgen from reflection vector** | **Low–Med** | requires normals; same CPU cost as classic |
| **EDGE** | 1 add + `Floor` (V scroll) | GPU texgen (normal or reflection) | **Low–Med** | requires normals; animated |
| **WS_* environment** | axis-swizzled matrix from world transform | GPU texgen from normal | **Low–Med** | requires normals; world-space |
| **GRID_* / GRID_WS_* environment** | grid timing + env matrix | GPU texgen | **Low–Med** | requires normals; animated env |
| **BUMPENV** | LinearOffset math + a rotating bump matrix | pairs with the bump-env **shader** gradient | **Medium** | drags in the bump-env shader (burns a texture stage) — the priciest combo |

### Does the argument *value* matter? (FPS, speed, scale, …)

**No.** The numeric value of a mapper's arguments — `FPS`, `Speed`, `UPerSec`/
`VPerSec`, `SPS`, `Period`, `UScale`/`VScale`, etc. — has **zero** effect on
performance. A texture scrolling at 0.1/sec and one at 1000/sec cost exactly the
same; a 1-FPS flipbook and a 240-FPS flipbook cost exactly the same.

Why, from the code:

- **The value is a constant folded in once at construction, not per frame.** `GRID`
  turns `FPS` into `MSPerFrame = 1000/fps` in `initialize()` (`mapper.cpp:293`);
  `ROTATE` turns `Speed` into `RadiansPerMilliSec` in its constructor
  (`mapper.cpp:343`); `LINEAR_OFFSET` turns `UPerSec`/`VPerSec` into
  `UVOffsetDeltaPerMS` once (`mapper.cpp:122`). Nothing re-derives these per frame.

- **The per-frame update is fixed-cost arithmetic regardless of the value.** `GRID`'s
  `update_temporal_state` is always the same `Remainder / MSPerFrame` divide + modulo
  (`mapper.cpp:306`) — a bigger FPS just makes `MSPerFrame` smaller so the divide
  yields a larger integer; it is the *same* divide. `LINEAR_OFFSET` is always
  `offset += deltaPerMS * elapsed; frac()` (`mapper.cpp:161`). `ROTATE` is always one
  `CurrentAngle += RadiansPerMilliSec * delta`. Faster animation ≠ more work per
  frame; it just plugs a different number into the same operations.

- **Higher FPS does NOT mean more updates.** The mapper is evaluated once per material
  bind per rendered frame (see #3), driven by `WW3D::Get_Sync_Time()` — never by the
  animation's own rate. A 1000-FPS flipbook still updates once per rendered frame; it
  simply advances more grid cells per update (same modulo math). It does not force
  extra draws or extra evaluations.

**Practical takeaway:** tune `FPS` / `Speed` / `Scale` purely for the look you want —
there is no performance budget attached to the number. (The only value that changes
*anything* structural is `Log2Width` on `GRID`, which sets how the texture is
subdivided; even that is a constant mask, not a per-frame cost.)

### What actually costs, ranked

1. **`BUMPENV`** — not because the mapper is heavy, but because it only makes sense
   paired with the bump-env primary-gradient shader, which burns a texture stage
   (see the Primary Gradient row above). The mapper itself is LinearOffset + one
   extra rotating matrix.
2. **Environment mappers** — cheap CPU, but they **force vertex normals** into the
   stream and depend on fixed-function texgen; heavier than a plain scroll only in
   that constraint, not in per-batch math.
3. **Animated matrix mappers** (offset / rotate / sine / grid / …) — the real cost
   is small and specific, and it is worth being precise about because it is easy to
   overstate.

   **What it is NOT:** in this engine an animated mapper does **not** kick the
   object out of static batching. The `VertexMaterialClass::Are_Mappers_Time_Variant()`
   query exists (`vertmaterial.h:306`) but **has no caller** — nothing in the render
   path branches on it. Time-variant and constant mappers travel the exact same
   draw path; there is no "dynamic vertex buffer" or "excluded from the static mesh
   list" penalty tied to animation here.

   **What it actually is:** the mapper's matrix is rebuilt inside
   `VertexMaterialClass::Apply()`, which `DX8Wrapper::Apply_Render_State_Changes`
   only calls when the material bind is dirty (`MATERIAL_CHANGED`,
   `dx8wrapper.cpp:2263`). So the cost is:

   - **One matrix rebuild + `Set_Transform(D3DTS_TEXTUREn)` per material bind.** For a
     constant mapper (`SCALE`) the rebuilt matrix is identical every time, so the only
     "waste" is recomputing a constant; for an animated one it genuinely differs each
     frame. Either way it is a few scalar ops and one device call — per *bind*, not
     per vertex.

   - **A dependency on the global sync clock.** Animated mappers read
     `WW3D::Get_Sync_Time()` (`mapper.cpp:159`), so their motion only advances while
     something calls `WW3D::Sync()`. That is why, e.g., the Material Viewer has to
     pump `WW3D::Sync` itself to keep scrolling UVs moving while the main viewport is
     paused — a correctness detail, not a per-frame cost.

   Net: the incremental cost of *animating* a mapper over leaving it constant is
   essentially one non-cacheable matrix per bind. Real, but tiny — and unrelated to
   batching. The only way it becomes visible is pathological fan-out (thousands of
   separately-bound materials each re-Applying an animated matrix every frame).
4. **`SCALE` / plain UV** — no per-frame work; plain UV sets `D3DTTFF_DISABLE` and
   is the true zero.

**Bottom line:** UV mappers are among the *cheapest* things on this list per batch
(one small matrix + a few state sets). Their cost lives in second-order effects —
being an environment map (requires vertex normals, ties to fixed-function texgen)
or dragging in a costly shader (`BUMPENV`) — not in per-vertex CPU work, which they
don't do, and not in batching, which animation does not affect in this engine.

---

## In-game scaling: one unit, and 20+ of the same unit

The per-mapper numbers above are per *material bind*. To reason about a real scene
you have to know how binds scale with instance count — which is set by the renderer's
batching, not by the mapper.

### How the engine batches instances (`dx8renderer.cpp`)

The DX8 mesh renderer groups all visible geometry into **texture categories** keyed by
`(textures, vertex material, shader)` (`DX8TextureCategoryClass`, sorted in
`dx8renderer.cpp:1126`). For each category it binds **once** —
`Set_Texture` / `Set_Material` / `Set_Shader` at `dx8renderer.cpp:1685-1710`, and
`Set_Material` is what runs the mapper's `Apply()` — then loops over every render task
that shares that material (`while (prt)`, `dx8renderer.cpp:1729`), drawing each without
re-binding.

There is **no hardware instancing**: each visible instance is its own
`PolyRenderTaskClass` (`dx8renderer.cpp:100`) and its own draw call. So for **N copies
of a unit that share one material**:

- **Mapper `Apply()` runs ONCE per category per frame — not N times.** The matrix build
  and `Set_Transform` are paid once and amortized across all N instances.
- **N draw calls** are issued (the real per-instance CPU cost — draw-call overhead, not
  mapper cost).
- **GPU work scales with total geometry** — N × the unit's vertices through T&L, N × its
  pixels through the rasterizer. This is where a mapper's *GPU* character (texgen,
  fixed-function transform) is multiplied by N, but it is per-vertex/per-pixel work the
  GPU does anyway; the mapper only decides *which* transform.

### Per-mapper penalty at 20+ instances

N = the instance count (how many copies of the unit are on screen). The last column
answers: **does this mapper's cost multiply as you add more copies, or is it a one-time
cost no matter how many you have?**

- **"No"** — flat. The mapper adds no cost that grows with the crowd. Its CPU work
  (matrix build + `Set_Transform`) is paid once per material category per frame and
  shared by all N instances, and it adds nothing to the GPU beyond the UV transform a
  textured vertex already gets. Going from 5 to 50 copies adds draw calls but **zero**
  extra mapper cost, CPU or GPU.
- **"GPU only"** — the mapper adds a *distinctive* GPU behavior (texgen from vertex
  normals) that runs per vertex, so it rises with N × vertices. The CPU/mapper side is
  still flat (once per category). This isn't a runaway penalty — it's the ordinary "more
  units = more geometry = more GPU work" — but it's called out because the mapper adds a
  step (texgen + a normals requirement) that a plain scroll does not.
- **"Yes"** — the mapper adds a per-pixel cost that genuinely multiplies with the crowd
  (extra work on every pixel of every instance).

| Mapper | Penalty at N=20+ (same unit) | Cost multiplies with N? |
|---|---|---|
| **plain UV / SCALE** | none — no transform, or one constant matrix for the whole category | **No** |
| **LINEAR_OFFSET / STEP / ZIGZAG / RANDOM / ROTATE / SINE / GRID / SCREEN** | one matrix rebuild per category per frame (a few scalar ops), shared by all 20; the GPU applies the same fixed-function UV transform every textured vertex already gets | **No** — the mapper adds nothing that scales; it's the same transform, just a moving matrix |
| **CLASSIC / ENVIRONMENT / EDGE** | same one-per-category bias matrix, but every instance's vertices **must carry normals**, and the GPU runs camera-space texgen for all N × verts | **GPU only** — texgen runs per vertex (N × verts); CPU/mapper cost stays flat |
| **WS_* / GRID_WS_* environment** | as above **plus** a 4×4 view-inverse **matrix multiply** in `Calculate_Texture_Matrix` (`mapper.cpp:796`) — still once per category, not per instance | **GPU only** — texgen per vertex (N × verts); the extra CPU multiply is still one-time |
| **BUMPENV** | the mapper is cheap; the paired **bump-env shader burns a texture stage**, so all N × pixels pay the extra stage — this is the one that stings at scale | **Yes** — extra per-pixel texture stage runs on N × pixels |

### What actually determines the cost of "20 tanks with scrolling treads"

1. **Do they share a material?** If all 20 use the same texture+material+shader, the
   mapper is Applied **once**. If they diverge (different team colors → different
   textures → different categories), you pay the bind — and one mapper Apply — **per
   category**, i.e. per distinct material, not per instance. Team-color variants are the
   usual reason N units become a handful of categories instead of one.

2. **Draw-call count (N).** The dominant CPU cost of 20 units is 20+ draw calls, which is
   true no matter what the mapper is. Scrolling UVs add nothing to that.

3. **The shader/gradient the mapper drags in.** A plain scroll/rotate/grid costs
   effectively nothing extra at 20× — it is the same GPU transform the vertices would get
   anyway. The only mapper that changes the per-pixel cost across all N instances is
   **BUMPENV**, via its shader.

**Rule of thumb for content:** animated UV scroll / rotate / grid / sine on 20+ units is
free-to-negligible — treat it as cosmetic with no budget. Environment maps cost you the
normals in the vertex format and fixed-function texgen (fine in moderation, watch it on
very high-poly units × large counts). **Bump-env is the only mapper worth rationing at
scale**, and that is really the shader's cost, not the mapper's.

### One object, many meshes, many mappers

The batching key is **material, not mesh** — so the question "does a multi-mesh object
cost more?" reduces to "how many *distinct* materials does it use?"

When an object is registered, `Add_Mesh` splits each mesh by material and files every
polygon group into a texture category via `Insert_To_Texture_Category`
(`dx8renderer.cpp:1234`), keyed by `(textures, vertex material, shader)`. Meshes are not
kept separate — they are flattened into the shared category list. Therefore:

- **Meshes that share a material merge into ONE category** — one bind, one mapper
  `Apply()`, regardless of how many meshes (or sub-objects, or HLod levels) feed it. Ten
  meshes all using the same scrolling-tread material = the mapper runs **once**.

- **Each DISTINCT mapper-using material is one category** — one bind, one mapper `Apply()`
  each. An object with 4 meshes using 4 different animated materials pays 4 matrix
  builds; an object with 4 meshes using the *same* material pays 1.

So a multi-mesh object does not cost more *because it has more meshes* — it costs more
only if those meshes introduce more distinct materials/shaders/texture sets. The per-mesh
count is irrelevant to mapper cost; the per-**material** count is what you pay.

This composes with the instance rule the obvious way: cost ≈ **(distinct material
categories in the object) × (one mapper Apply each, per frame)** for the CPU side, plus
**N instances × per-category draw calls** and **N × total geometry** on the GPU. Adding
meshes that reuse existing materials is nearly free; adding meshes with new materials
adds one category (one bind + one mapper Apply) apiece.

**Content takeaway:** an object with many meshes but few materials (the common case —
sub-objects sharing a texture atlas) is as cheap as a single-mesh object. What raises the
bill is material *variety* — every extra unique material/shader/texture set is another
category to bind, whether it lives on one mesh or ten. Consolidating meshes onto shared
materials, not reducing mesh count, is the lever.

### Do moving units change any of this?

Units move every frame, so it is fair to ask whether motion adds to the mapper/material
cost. For the rendering path analysed here: **no** — motion is already the normal case,
not an extra.

- **Rigid units (vehicles, buildings).** The per-instance render task sets the world
  matrix from `mesh->Get_Transform()` inside the draw loop
  (`Set_Transform(D3DTS_WORLD, …)`, `dx8renderer.cpp:1857`). A stationary and a moving
  instance run the *identical* code; only the matrix *value* differs. Moving a unit costs
  nothing beyond drawing it. It does not re-bind the material or re-Apply the mapper —
  those are still once per category, upstream of this per-instance transform.

- **Skinned units (infantry).** `SKIN` meshes set world **identity**
  (`dx8renderer.cpp:1842`) and are deformed by bones on the CPU each frame. That skinning
  cost is real and per-instance, but it is driven by **animation**, not by translation
  through the world, and it is entirely separate from UV mappers.

- **Environment mappers and motion.** Env / WS-env mappers build their matrix from the
  **view (camera) transform** (`Get_Transform(D3DTS_VIEW)`, `mapper.cpp:797`), not the
  object's world transform. So a *unit* moving does not touch the env matrix; only the
  *camera* moving changes it — and it is still rebuilt once per category, not per instance.
  (Their reflection/normal texgen is per-vertex on the GPU and updates for free as the
  geometry moves.)

Where movement genuinely costs is **outside** this document's scope — GameLogic
(pathfinding, steering, collision) and the animation/bone updates that feed
`mesh->Get_Transform()` and the skin deform. None of that is mapper- or material-related;
it would be there with plain-UV textures too.

**Takeaway:** movement is orthogonal to mapper cost. A scrolling-tread tank costs the same
mapper work parked or driving; the driving cost lives in logic and animation, not in the
material/UV path.
