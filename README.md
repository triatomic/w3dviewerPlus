[![Download latest release](https://img.shields.io/github/v/release/triatomic/w3dviewerPlus?include_prereleases&sort=date&display_name=tag&style=for-the-badge&label=Download%20W3DView%2B&color=2ea44f)](https://github.com/triatomic/w3dviewerPlus/releases/latest)
[![Latest release](https://img.shields.io/github/release-date/triatomic/w3dviewerPlus?style=flat&label=Released)](https://github.com/triatomic/w3dviewerPlus/releases/latest)

# W3DView+

**W3DView+** is an enhanced build of the Westwood 3D model viewer (`W3DView`) for
*Command & Conquer: Generals* and *Zero Hour*, built on the
[TheSuperHackers GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode)
engine. It adds a modern dark theme, a full Qt-based material editor, camera and
lighting improvements, and a ground-plane preview — on top of the original tool
for inspecting `.w3d` meshes, hierarchies, animations, and materials.

## Download

**➡️ [Download the latest release](https://github.com/triatomic/w3dviewerPlus/releases/latest)**

Grab the `W3DViewZH-<version>.zip` asset from the latest release. It is a
self-contained bundle — no separate install needed.

## Running

1. Download and extract `W3DViewZH-<version>.zip` anywhere.
2. Run **`W3DViewZH.exe`**.

The zip already includes everything the viewer needs to run:

```
W3DViewZH.exe          the viewer
Qt5Core.dll            Qt runtime (material editor panel)
Qt5Gui.dll
Qt5Widgets.dll
mss32.dll              Miles audio
platforms/qwindows.dll Qt platform plugin (must stay in this subfolder)
```

Keep these files together — in particular, `platforms/qwindows.dll` must remain
in its `platforms` subfolder next to the exe, or the Qt material panel will not
load.

To view a model, use **File → Open** (or drag a `.w3d` file — or a whole folder
of them — onto the window). Textures are resolved from the folders alongside the
`.w3d` files.

## New features

Everything below is added by W3DView+ on top of the stock tool:

**Material editor (Qt panel)**
- A full material editor replicating the 3ds Max W3D material UI, fed directly
  from the raw `.w3d` chunk data.
- Open several `.w3d` files at once, one tab per file.
- Live preview: edits to vertex materials, shaders, mappers, and **Clamp U/V**
  update the 3D preview immediately.
- Status-bar help describing mapper arguments and the UV mapping type; `+`/`×`
  buttons to add and remove mapper-arg lines.
- Copy/paste buttons for the material panel's settings groups.

**Viewport & camera**
- Reworked 3ds Max–style orbit camera; the Material Viewer preview matches the
  main viewport's full control set.
- Vertical camera invert defaults to on (configurable).
- **Ground plane** (new **Ground** menu, in both the main viewport and the
  Material Viewer): a checkered plane with a soft blob shadow under the model.
  It is lit by the scene, so moving the light sweeps a pool of light across it;
  additive/alpha glow from the model blends onto the ground. Adjustable height
  and reset.
- **Back-face tint**: render normally-culled back faces in a pickable colour so
  you can inspect the inside of a mesh without changing its Double-Sided flag.

**Lighting**
- Movable scene light in the Material Viewer preview.
- **Light** menu with Free Roam / Per Face placement modes.

**Theme**
- Full dark mode (ported from Notepad++'s NppDarkMode), following the Windows
  app theme by default, covering the menus, toolbars, tree, and viewport.

## Building

The project uses CMake with Visual Studio 2022. The W3DView+ tool builds as the
`z_w3dview` target (produces `W3DViewZH.exe`).

**Windows (Visual Studio 2022)**

```bash
cmake --preset win32
cmake --build build/win32 --target z_w3dview --config Release
```

The built viewer lands at `build/win32/GeneralsMD/Release/W3DViewZH.exe`, with
the Qt and Miles runtime DLLs copied alongside it automatically.

> The Qt material panel requires Qt5 (Core/Gui/Widgets). It is provided through
> the vcpkg manifest (`vcpkg.json`); the first configure resolves it. Without
> Qt, the viewer still builds and runs, but the material-editor panel is
> replaced by a placeholder.

For the full engine and the other build guides (VS6, CLion, Docker/Linux), see
the upstream
[TheSuperHackers Wiki](https://github.com/TheSuperHackers/GeneralsGameCode/wiki/build_guides).

## Credits & License

W3DView+ is a fork of
[TheSuperHackers GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode),
which is itself based on the source code released by Electronic Arts. The dark
mode is a trimmed port of [Notepad++](https://github.com/notepad-plus-plus/notepad-plus-plus)'s
NppDarkMode.

EA has not endorsed and does not support this product. All trademarks are the
property of their respective owners.

This project is licensed under the [GPL-3.0 License](https://www.gnu.org/licenses/gpl-3.0.html).
See [LICENSE.md](LICENSE.md) for details.
