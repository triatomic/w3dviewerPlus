# DebugWindow — Change Log

This documents the changes made to the script-debugger window (`DebugWindow.dll`) and
its game-side integration. The window is the optional dialog loaded when the game is
launched with `-scriptDebug`; it shows the script log live and lets you pause/step the
simulation.

> **Build/deploy note (local only — nothing committed):**
> The biggest win lives entirely in the DLL, so for most of this you only need to drop
> in the rebuilt `DebugWindow.dll`. Two items (the `Log every N` throttle and the
> proc-address cleanup) live in the game and require a rebuilt executable. See
> **Deployment** at the bottom.

---

## 1. Performance fix — the game no longer slows down over time

**Symptom:** with the DLL attached, the game got progressively slower the longer it ran.

**Root cause:** the window did O(n²) work and a full edit-control repaint on *every*
call, and the game re-resolved every DLL function by name on every call:

### DLL side (`DebugWindowDialog.{h,cpp}`)
- **O(1) variable lookup.** `AdjustVariable` used to linearly scan every variable to
  find a match — and the game pushes *every* counter/flag every frame, so that was
  O(V²) per frame. Now a `std::map<name, index>` (`mVariableIndex`) gives O(1) lookup.
  The ordered `mVariables` vector is kept as the source of truth so display order and
  value read-back are unchanged.
- **Coalesced repaint.** Previously each variable/message update did a full
  `SetWindowText` on the (ever-growing) edit control. Now per-call work just sets a
  dirty flag (`mVarsDirty` / `mMesgDirty`); the actual repaint happens **at most once
  per frame** in `SetFrameNumber` → `_FlushDisplays()`, plus a `WM_TIMER` safety flush
  so the display still updates while the game is paused.
- **Bounded message history.** `mMessages` used to grow without limit, and the message
  string was rebuilt from the whole vector on every append → O(N²) over a session.
  Now the string is maintained incrementally and history is capped at
  `MAX_RETAINED_MESSAGES = 2000` (oldest dropped, with a dropped-count kept).
- **Read-only fix.** The Messages box is `ES_READONLY`, on which `EM_REPLACESEL` is
  silently ignored. Display uses `SetWindowText` of the maintained buffer instead.

### Game side (`ScriptEngine.cpp`, GeneralsMD **and** Generals)
- **Cached export pointers.** Every per-frame call (`CanAppContinue`, `RunAppFast`,
  `AppendMessage`, `AdjustVariable`, `SetFrameNumber`, …) used to call
  `GetProcAddress` by name *every time*. They are now resolved **once** when the DLL is
  loaded (`_resolveDebugDLLProcs`) into a struct of typed function pointers, and cleared
  on unload.
- **Fixed `isTimeFast` bug.** It resolved `"CanAppContinue"`, threw it away, then
  resolved `"RunAppFast"` — a copy-paste leftover doing a wasted lookup every frame.
  Now it just uses the cached `runAppFast` pointer.

---

## 2. New UI controls

A compact row was added below the buttons. Layout was adjusted so nothing overlaps.

### `Display ms` — display refresh interval
Milliseconds between window redraws (the `WM_TIMER` flush period; default 100 = 10 Hz).
- **Higher** → fewer repaints, cheaper, less "live".
- **Lower** → more live display, more overhead.
- **Works with any game build** (it's the DLL's own timer).

### `Log every N` — variable push cadence
Pushes variable values to the window only every Nth game frame (default 1 = every frame).
- **Higher** → less game slowdown (the per-frame variable push is the most expensive part).
- **1** → every frame (most accurate, slowest).
- **Requires a rebuilt game `.exe`** — the game reads this value via the new
  `GetDebugLogInterval` export (`DebugWindowExport.h` / `DebugWindow.cpp`) and applies
  the throttle in `ScriptEngine::update`. Older executables ignore it.

---

## 3. Tooltips

All controls now have hover tooltips. They are registered with **`TTF_SUBCLASS`**
(the tooltip control subclasses each target window) rather than relying on
`PreTranslateMessage`/`RelayEvent` — necessary because this is a modeless dialog driven
by the game's message loop, not MFC's own pump, so the usual relay path can't be relied
on. The two interval tooltips spell out the performance trade-off and note the
EXE-rebuild requirement for `Log every N`.

---

## 4. `Save` checkbox — log to a timestamped file

A **Save** checkbox tees the log to disk *in parallel* with the live in-game display.

- **What's written:** the script-fired **messages** plus **variable** snapshots
  (`--- frame N ---` blocks).
- **File:** `DebugLog_YYYY-MM-DD_HH-MM-SS.txt`, created in the game's working folder.
- **Behavior:** ticking Save opens a fresh file (logs from that point onward, no
  backlog); unticking flushes and closes it; re-ticking opens a new timestamped file.
- **Cost:** the file handle stays open for the session (buffered writes), so writing is
  cheap and doesn't stall the game. Closed cleanly on `OnDestroy` if the window is torn
  down while still checked.
- **Two companion checkboxes** sit next to Save (both default off):
  - **`Pretty`** — cosmetic only. On = boxed banner, indented + column-aligned
    `name = value`, `--- frame N @ HH:MM:SS ---` headers, and a close-footer summary.
    Off = plain header, bare `name = value`, `--- frame N ---`, no footer. **The
    size/throttle protections below apply either way** — unchecking Pretty does *not*
    bring back size inflation.
  - **`Lossless`** — fidelity. See "Lossless capture" below.

### Restart handling — one file per game session

The DLL and dialog live for the whole game process (created once in `ScriptEngine::init`,
destroyed at process exit), so a **match/mission restart does not reload the DLL**. The
frame counter, however, **resets to ~0 on restart**. `SetFrameNumber` detects this
(the incoming frame is lower than the last logged frame) and **rolls to a new
timestamped file** via `_OpenLogFile` — closing the old one cleanly and re-writing a
full variable baseline into the new one.
- **Effect on the log:** each game session is a self-contained file; two games are never
  blended, and logging never stalls waiting for the counter to climb back up.
- A per-process `mLogSession` counter suffixes the filename (`..._1.txt`, `..._2.txt`)
  so two opens within the same wall-clock second never clobber each other.

### Throttling — how it shapes the output log

The first version dumped *every* variable on *every* snapshot at the `Log every N`
rate (default 1 = every frame). With ~50 vars that was ~100 MB/hour of almost entirely
**redundant repeated lines**, plus a burst of writes each frame. Three changes fix both
the file-size inflation and the I/O frequency:

1. **Change-only variable writes.** A variable line is written **only when its value
   actually changed** since it was last written (tracked per-variable; reuses the
   change detection already in `AdjustVariable`). New variables are written once on
   first appearance. When Save is ticked, a one-time full baseline of all current
   variables is written, then only deltas after that.
   - **Effect on the log:** a counter/flag appears the frame it changes, *not* every
     frame. If **no** variable changed in a snapshot, **nothing is written — not even
     the `--- frame N ---` header.** Idle stretches add ~zero bytes. The file becomes a
     readable *event* log instead of a wall of repeated values.

2. **Separate file snapshot interval.** The file has its own cadence
   (`FILE_SNAPSHOT_INTERVAL_FRAMES`, default **5**), **decoupled** from the on-screen
   `Log every N`. So the display can stay live at 1 while the file is only considered
   for writing every 5th game frame.
   - **Effect on the log:** `--- frame N ---` headers step by ~5 frames at most (e.g.
     1310, 1315, 1320 …), never one per frame. Messages are unaffected — they are
     event-driven and still written the moment they arrive.

3. **1 GB hard size cap.** A running byte count (`mLogBytesWritten`, no per-write
   `stat`/`ftell`) is checked before each write; once the file reaches
   `MAX_LOG_FILE_BYTES` (1 GB) writing stops after emitting one line:
   `=== log size cap (1 GB) reached, stopping ===`.
   - **Effect on the log:** the file can never exceed ~1 GB even if Save is left on for
     a very long session; the cap notice marks where logging stopped.

Additionally, each frame's changed-variable block is assembled into one string and
written with a **single `fwrite`** (header + block) rather than one call per line, and
there is **no per-frame `fflush`** (the CRT buffers; flush happens on uncheck/close).

**Example output** (default = interval + change-only, plain style):
```
=== DebugWindow log started 2026-05-26 14:30:12 ===
EnemiesKilled = 0          <- baseline written once when Save was ticked
BridgeIntact = 1
--- frame 1315 ---
EnemiesKilled = 3          <- only the vars that changed since the baseline
--- frame 1340 ---
BridgeIntact = 0           <- the flag flipped; nothing else changed this block
```

### Lossless capture — the `Lossless` checkbox

By default (interval mode) the file keeps only each changed variable's **final value per
snapshot window** — if a counter ticks `1 → 2 → 3` within one 5-frame window, the file
shows only `3`. The fact that it changed is never lost (the dirty flag survives to the
next snapshot), but intermediate values are.

Tick **`Lossless`** to capture every change instead. When on:
- Each variable change is written **immediately** in `AdjustVariable`, frame-prefixed:
  `[1315] EnemiesKilled = 2`. No interval coalescing.
- The interval snapshot path is **suppressed** (so data isn't logged twice).
- Still honors the 1 GB cap and the restart-rolls-new-file behavior; `Pretty` still
  aligns the name column.

**What it costs:** volume and write-frequency rise (back toward — but still well below —
the original full-dump behavior, since only the *one* changed variable is written per
change, never the whole list). **Honest ceiling:** the game pushes each variable once
per frame, so "lossless" means *every per-frame value* — it cannot capture sub-frame
changes the data source never exposes.

```
[1315] EnemiesKilled = 1
[1316] EnemiesKilled = 2
[1317] EnemiesKilled = 3      <- every value, not just the window's final one
[1340] BridgeIntact = 0
```

> **Bug fixed alongside this:** the frame number string (`mFrameNumber`) was
> over-allocated by the original code (`frameNumber/10 + 2`) and left holding trailing
> `NUL` bytes past the digits. Harmless for `SetWindowText` (stops at the first NUL)
> but the file writer copied the full string length, producing a `--- frame 1315 ---`
> header followed by a run of `NUL`s. `SetFrameNumber` now sizes the string to the
> actual digits.

---

## 5. Concepts (glossary)

- **Frame** = one **game simulation tick**. The engine advances logic ~30 times/sec and
  `TheGameLogic->getFrame()` counts those ticks. It is the game's *own* logical clock —
  **not** a kernel/OS timer and **not** wall-clock time. If the game pauses or runs slow,
  the frame counter pauses/slows with it. `Log every N` is measured in these frames.
- **Display ms** is the one real-time clock here — a Windows `SetTimer` controlling how
  often the *window* repaints, independent of game frames.
- **Variables** = the script system's **counters and flags** (named values scripts read
  and write, e.g. `EnemiesKilled = 7`). Shown in the upper pane.
- **Messages** = a log of **which scripts fired** each frame (e.g.
  `1234 Run script - DestroyBaseTrigger`). Shown in the lower pane.

---

## Files changed

DLL (shared, used by both games):
- `Core/Tools/DebugWindow/DebugWindowDialog.h`
- `Core/Tools/DebugWindow/DebugWindowDialog.cpp`
- `Core/Tools/DebugWindow/DebugWindow.cpp`
- `Core/Tools/DebugWindow/DebugWindowExport.h`
- `Core/Tools/DebugWindow/DebugWindow.rc`
- `Core/Tools/DebugWindow/Resource.h`

Game side:
- `GeneralsMD/Code/GameEngine/Source/GameLogic/ScriptEngine/ScriptEngine.cpp` (Zero Hour)
- `Generals/Code/GameEngine/Source/GameLogic/ScriptEngine/ScriptEngine.cpp` (Generals v1.08)

---

## Deployment

| Change | Lives in | Needs |
|---|---|---|
| Perf fix (O(1) lookup, coalesced repaint, message cap, read-only fix) | DLL | rebuilt `DebugWindow.dll` |
| `Display ms` control | DLL | rebuilt `DebugWindow.dll` |
| `Save` checkbox + file logging (change-only, file interval, 1 GB cap) | DLL | rebuilt `DebugWindow.dll` |
| `Pretty` / `Lossless` checkboxes, restart-rolls-new-file | DLL | rebuilt `DebugWindow.dll` |
| Tooltips | DLL | rebuilt `DebugWindow.dll` |
| `Log every N` throttle + `GetDebugLogInterval` use | game | rebuilt `.exe` |
| Cached proc addresses, `isTimeFast` fix | game | rebuilt `.exe` |

Build targets (all built Release this session, exit 0):
- `core_debugwindow` → `build/vs2022/Core/Release/DebugWindow.dll`
- `z_generals` → `build/vs2022/GeneralsMD/Release/generalszh.exe`
- `g_generals` → `build/vs2022/Generals/Release/generalsv.exe`

> To stop the slowdown only, shipping the rebuilt **`DebugWindow.dll`** alone is enough;
> the `Log every N` field will be visible but inert until the game `.exe` is also rebuilt.
