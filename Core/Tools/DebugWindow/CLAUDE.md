# CLAUDE.md — DebugWindow DLL

Guidance for working in `Core/Tools/DebugWindow/`. See `CHANGES.md` for the change log
and rationale; this file is the architectural orientation.

## What this is

`DebugWindow.dll` is an **MFC dialog** (`DebugWindowDialog`) — the script debugger window
the game loads when launched with `-scriptDebug`. It shows the live script log
(**Messages** pane) and the script system's **counters/flags** (**Variables** pane), and
lets you pause/step the simulation. The DLL is **shared by both games** — one binary
serves Zero Hour (`GeneralsMD`) and Generals (`Generals`).

Build target: `core_debugwindow` → `build/vs2022/Core/Release/DebugWindow.dll`.
(`LNK4017 DESCRIPTION ... ignored` is a harmless pre-existing `.def` warning; the
`'pwsh.exe' is not recognized` line is the post-build copy script, not a build error.)

## Game ↔ DLL contract

The game drives everything; the DLL never calls into the game. Exports are declared in
`DebugWindowExport.h` and forwarded in `DebugWindow.cpp` to the singleton dialog
(`theApp.GetDialogWindow()`). Every export must start with
`AFX_MANAGE_STATE(AfxGetStaticModuleState())` before any MFC call.

Per-frame the game calls (from `ScriptEngine.cpp` in both engines):
- `SetFrameNumber(frame)` — once per game frame; **the dialog's per-frame heartbeat**.
- `AdjustVariable(name, value)` — once per counter/flag (gated game-side by `Log every N`).
- `AppendMessage(...)` — when a script fires (event-driven).
- `CanAppContinue` / `RunAppFast` — pause/fast-forward state pulled back by the game.
- `GetDebugLogInterval()` — the game reads this to throttle its variable pushes.

**Game-side integration (`GeneralsMD` + `Generals` `ScriptEngine.cpp`, keep both in
sync):** export pointers are resolved once at load via `_resolveDebugDLLProcs` into a
typed-pointer struct (never `GetProcAddress` per call). The `Log every N` throttle gates
the per-frame `_adjustVariable` sweep in `ScriptEngine::update`. Changes to the
DLL/game contract require a **rebuilt `.exe`**; pure DLL changes do not.

## Critical invariants — do not break these

- **`mVariables`, `mVariableIndex`, `mVarFileDirty` are parallel/consistent.** `mVariables`
  (ordered `vector<PairString>`) is the source of truth for display order and value
  read-back. `mVariableIndex` maps name→index for O(1) `AdjustVariable`. `mVarFileDirty`
  is indexed the same. Any code that clears one (e.g. `OnClearWindows`) must clear all
  three (and reset `mMaxVarNameLen`).

- **Coalesced display, not per-call repaint.** `AdjustVariable`/`AppendMessage` only set
  `mVarsDirty`/`mMesgDirty` and update in-memory buffers. The real `SetWindowText`
  happens at most once per frame in `_FlushDisplays()` (called from `SetFrameNumber` and
  a `WM_TIMER` so it still updates while paused). Do **not** reintroduce per-call
  `SetWindowText` — that was the O(n²) slowdown.

- **The Messages box is `ES_READONLY`.** `EM_REPLACESEL` is silently ignored on it; use
  `SetWindowText` of the maintained `mMessagesString`.

- **Same-thread model — no locking needed, and don't add a worker thread.** The dialog is
  created on the game thread (`CreateDebugDialog`) and its message handlers
  (`OnSaveToggle`, `OnTimer`, …) are dispatched by the game's own message pump. So
  handlers and the game's `AppendMessage`/`SetFrameNumber` calls run on the **same
  thread** — shared state is safe without mutexes *because of this*. Introducing a
  background thread would break that assumption.

- **Tooltips use `TTF_SUBCLASS`** (in `OnInitDialog`), not `PreTranslateMessage` relay —
  the modeless dialog isn't on MFC's pump, so the relay path can't be relied on. Tip
  text with `\r\n` line breaks needs `TTM_SETMAXTIPWIDTH` set or the breaks are ignored.

- **`OnInitDialog`, not `OnCreate`, for child controls.** Dialog children don't exist at
  `WM_CREATE`; seed/query controls in `OnInitDialog`.

- **No fixed-size `sprintf` buffers for variable-length content.** A banner/header sized
  for a guess will overflow (this caused a Save crash once). Use `std::string` building +
  `fwrite`, or `fprintf` straight to the file. Frame numbers: format into a buffer then
  `assign` so the `std::string` length matches the digits (the old `frameNumber/10 + 2`
  sizing leaked trailing NULs into the file).

## Save-to-file feature (the `Save` / `Pretty` / `Lossless` checkboxes)

Tees the log to `DebugLog_<timestamp>[_session].txt` in the game's working dir, in
parallel with the live display. Key pieces in `DebugWindowDialog.cpp`:
- `_OpenLogFile` / `_CloseLogFile` — open keeps `FILE* mLogFile` for the session,
  re-baselines all vars dirty, resets counters; close writes the footer (pretty only).
- `_WriteVarsSnapshotToFile` — **interval mode**: changed-only vars, every
  `FILE_SNAPSHOT_INTERVAL_FRAMES` (default 5), one `fwrite`.
- `_WriteVarLineToFile` — **lossless mode** (`mLosslessLog`): one frame-prefixed line per
  change, written immediately in `AdjustVariable`; the interval path is suppressed.
- **Size cap** `MAX_LOG_FILE_BYTES` (1 GB), tracked via `mLogBytesWritten` (no per-write
  `stat`). Checked before every write.
- **Restart detection:** in `SetFrameNumber`, an incoming frame lower than
  `mLastVarsSnapshotFrame` means the game restarted → roll to a new file. `mLogSession`
  suffixes the name so same-second rolls don't collide.

Design rules to preserve: **messages are never dropped** (only the 1 GB cap stops them);
**interval mode keeps each var's final value per window** (intermediate values dropped by
design — that's what Lossless is for); `Pretty` is **cosmetic only** and must never
disable the change-only/interval/cap protections.

## Resource / layout notes

`Resource.h` IDs and `DebugWindow.rc` (`IDD_DebugWindow`) must stay in sync. New controls
go after the existing free IDs. The Save/Pretty/Lossless checkboxes are stacked on the
right of the rate row; if you add more, push the Variables edit box (`IDC_Variables`)
down and shrink its height so nothing overlaps — verify against the button rows at the
top (y 7–40) and the interval row.

## Style

Match the surrounding code (it's legacy EA MFC). Keep DLL-local changes DLL-local; only
touch `ScriptEngine.cpp` when the game↔DLL contract actually changes, and mirror such
changes in **both** `GeneralsMD` and `Generals`.
