# MPE Bender

A VST3 **instrument** for Windows that hosts **Serum 2** (or any VST3 synth) inside
itself and drives it from a built‑in piano roll where **each note carries its own
pitch curve**. A note is drawn as a ribbon that follows base‑key + bend points
across the keyboard, so it can start on one key and sweep to another. Each note is
sent on its own MPE channel (MIDI Polyphonic Expression), so the bends are truly
independent per note.

```
   ┌─────────────────────────  MPE Bender (VST3 instrument)  ─────────────────────────┐
   │  piano roll — notes ARE their own pitch curves (base key + bend points)          │
   │              │                                                                  │
   │              ▼                                                                   │
   │   MpeEngine ──► MPE MIDI (note-on/off + per-note pitch bend)                     │
   │              │                                                                   │
   │              ▼                                                                   │
   │        hosted Serum 2  ──►  audio  ──────────────────────────────────► DAW out   │
   └────────────────────────────────────────────────────────────────────────────────┘
```

## Build

Requires: Visual Studio 2022 Build Tools (Desktop C++), CMake ≥ 3.22. JUCE 9.0.2
is a git submodule under `JUCE/`.

```bash
cmake -S . -B build
cmake --build build --config Release --target MpePianoRoll_VST3 MpePianoRoll_Standalone
```

Output: `build/MpePianoRoll_artefacts/Release/VST3/MPE Bender.vst3`

## Install

FL Studio only reliably scans `C:\Program Files\Common Files\VST3`, and writing
there needs admin. Two options:

**Recommended — link once, then every build is live automatically:**

```powershell
# elevated PowerShell (Win+X -> Terminal (Admin)), ONCE
powershell -ExecutionPolicy Bypass -File .\link.ps1
```

`link.ps1` points `C:\Program Files\Common Files\VST3\MPE Bender.vst3` at the build
output via a junction. After that, any rebuild is instantly the plugin FL loads —
no admin, no copying. Just reload the plugin instance in FL to pick up new code.

**Auto-rebuild on save** (normal shell, optional):

```powershell
powershell -ExecutionPolicy Bypass -File .\watch.ps1
```

Watches `Source\` + `CMakeLists.txt` and rebuilds on every save. Unload MPE Bender
in FL while it rebuilds (Windows locks the loaded `.vst3`), then re-add it.

**One-shot copy** (`install.ps1`, elevated) is the fallback if you don't want the
junction.

Then in FL: **Options → Manage plugins → Find installed plugins**, search `bender`
(it's a **Synth**).

## Use it (FL Studio)

1. Add `MPE Bender` on a channel as an **instrument** (not an effect).
2. Click **Load Serum 2…** – it auto‑points at
   `C:\Program Files\Common Files\VST3\Serum2.vst3`. Pick the `.vst3`.
3. Click **Open synth UI** to show Serum's own window. In Serum 2, **turn on MPE**
   and set its pitch‑bend range to match the **PB Range** slider here (default 48
   semitones).
4. Two tools, toggled in the toolbar or with **b** / **s** (like FL's piano roll):

   **Draw** (b):
   - **click** empty grid = new note; **drag body** = move
   - **drag the right edge** = the note's end bend point: horizontal changes length,
     vertical bends the tail (one 2D handle)
   - **Ctrl+click** or **double‑click** the ribbon = add a **bend point**; drag points
     to shape the note's straight‑line "chord". **right‑click** a point = remove it.
   - hold **Alt** while dragging = fine / no snap; **Shift**+drag = Select for that drag

   **The shape editor** — select one note and controls appear around it:
   - a **STR / SIN / TRI** wheel below the note sets its shape. STR = a plain line
     between the bend points; SIN / TRI make a wave ride on top of it.
   - for a wave: a **cycles** slider below the note (how many cycles fit), a
     **squeeze** slider above it (bunch the cycles toward one end), and two vertical
     **amplitude** sliders just outside the start and end points — set them apart to
     grow the wobble from start to finish.

   **Select** (s):
   - **drag a box** = marquee‑select; **Shift**+drag = add; **drag a selected note** =
     move the group; **drag a selected right edge** = resize the group;
     **Delete** / **Esc**.

   **Zoom**: `-` / `+` / `1:1` buttons, or **Ctrl+scroll** (horizontal, around the
   cursor) and **Ctrl+Shift+scroll** (vertical).
5. Press play in FL. The plugin loops its own pattern (**Loop (beats)** slider),
   synced to the host tempo/position, and plays Serum with the per‑note bends.

`Fwd host MIDI` (on by default) also passes MIDI from the FL piano roll / a
keyboard straight through to Serum, so you can still play it normally.

## Notes / limits

- Per‑note bend needs the hosted synth in **MPE mode**. Serum 2 supports it.
- `MPE Channels` = how many member channels (voices) can bend independently at
  once (max 14). Notes beyond that steal the oldest voice.
- Project state saves the Serum path **and** Serum's current patch, so reopening
  a project restores everything.
- Hosting a VST inside a VST is allowed and works in FL Studio; a few other DAWs
  sandbox plugins in ways that can make the nested editor flaky.

## Source map

| File | Role |
|------|------|
| `Source/NoteModel.h` | `MpeNote` (base key + `bend` curve) + `ExpressionCurve` |
| `Source/MpeEngine.*` | notes → MPE MIDI (note-on/off + per-note pitch bend), channel allocation |
| `Source/HostedPlugin.*` | load / prepare / run a VST3 instance (Serum 2) |
| `Source/HostedPluginWindow.h` | floating window for the hosted synth's editor |
| `Source/PluginProcessor.*` | transport, loop playback, state, wiring |
| `Source/PluginEditor.*` | toolbar + hosted‑synth controls |
| `Source/PianoRollComponent.*` | the note grid; notes drawn/edited as pitch ribbons |
