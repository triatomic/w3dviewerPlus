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

From `MAPPERS.TXT`. Mappers run **on the CPU per frame** to build a texture matrix
(`D3DTS_TEXTURE0/1`); they add **no per-pixel GPU cost**, only a small per-object
CPU cost — and, for animated ones, they keep the surface **dynamic** (opts it out
of static batching, keeps `WW3D::Sync` advancing it).

| Mapper | What it does | Relative cost |
|---|---|---|
| **(none) / plain UV** | uses stored UVs | **zero** — no texture matrix, fully static/batchable |
| **LINEAR_OFFSET** | scrolls UVs at constant speed | trivial — a couple of adds/frame |
| **SCALE** | static UV scale (detail mapping) | trivial, constant (not animated) |
| **STEP / ZIGZAG / RANDOM linear offset** | scroll variants (stepped / reversing / jittered) | trivial — a few branches/frame |
| **ROTATE** | rotate about a center, then scale | cheap — one sin/cos + matrix build/frame |
| **SINE_LINEAR_OFFSET** | Lissajous wobble | cheap — a few sin() calls/frame |
| **GRID** | flipbook animation over a grid of sub-frames | cheap CPU; picks a sub-rect/frame |
| **SCREEN** | UV = screen position | cheap, but view-dependent (recomputed as camera moves) |
| **CLASSIC_ENVIRONMENT / ENVIRONMENT** | env-map via normal / reflection vector | moderate — needs env texture + per-vertex vector math; reflection (`ENVIRONMENT`) slightly heavier than normal (`CLASSIC`) |
| **WS_* / GRID_* environment** | world-space and animated env maps | as above + world-space / grid bookkeeping |
| **EDGE** | Z of reflection/normal drives U; V scrolls | moderate — reflection math like env maps |
| **BUMPENV** | sets up (and optionally animates) the bump matrix | **heaviest mapper** — CPU half of the Bump-Env gradient; only meaningful with a bump-env shader, itself the priciest shader mode |

**The dominant mapper cost** is not GPU at all: any *animated* mapper makes the
object permanently dynamic (its texture matrix changes every frame), so the engine
cannot treat it as static geometry. Plain UV or a static `SCALE` avoids that
entirely.
