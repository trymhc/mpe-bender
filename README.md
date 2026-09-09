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

**Just want to use it?** Grab the latest zip from
[Releases](https://github.com/trymhc/mpe-bender/releases) and follow `INSTALL.txt`.
That build self-updates.

## Build from source

Requires: Visual Studio 2022 Build Tools (Desktop C++), CMake ≥ 3.22. JUCE 9.0.2
is a git submodule under `JUCE/`.

```bash
git clone --recursive https://github.com/trymhc/mpe-bender
cd mpe-bender
cmake -S . -B build
cmake --build build --config Release --target MpePianoRoll_VST3 MpePianoRoll_Standalone
```

(If you cloned without `--recursive`: `git submodule update --init`.)

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
2. It **auto-loads a synth** on a fresh instance — the last VST3 instrument you
   loaded, or Serum 2 if it can find it. Everything runs off the **synth menu** on
   the toolbar: **Load new synth…** picks any `.vst3` instrument (Serum, Vital,
   Pigments, Massive X, Diva, Surge XT, …) and offers to save it to the list with a
   name you choose. After that the menu is your quick-switch list — pick an entry to
   load it instantly; rename / remove entries from the bottom of the menu. The list
   lives at `%APPDATA%\MPE Bender\synths.xml` and is shared by all instances.
3. Click **Open synth** to show the hosted synth's own window. **Turn on that synth's
   MPE mode** and set its pitch‑bend range to match the **Pitch-bend range** on the
   Settings tab (default 48). Without MPE the synth still plays, but every sounding
   note shares one bend instead of bending independently.
4. Two tools, toggled in the toolbar or with **b** / **s** (like FL's piano roll):

   **Draw** (b):
   - **click** empty grid = new note; **drag body** = move
   - **drag the right edge** = the note's end bend point: horizontal changes length,
     vertical bends the tail (one 2D handle)
   - **Ctrl+click** or **double‑click** the ribbon = add a **bend point**; drag points
     to shape the note's straight‑line "chord".
   - **right‑click** = deselect everything; **right‑click + drag** across notes = erase
     them. Right‑click a bend point of the selected note = remove that point.
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

   **Zoom / navigate**: **wheel** = scroll vertically, **Shift+wheel** = scroll
   horizontally, **Ctrl+wheel** = zoom horizontally, **Ctrl+Shift+wheel** = zoom
   vertically, trackpad **pinch** = zoom both, **middle-drag** = pan, **1:1** button
   (bottom-right) = reset zoom.

   **Editing shortcuts** (piano roll focused):
   - **Ctrl+Z** undo, **Ctrl+Shift+Z** / **Ctrl+Y** redo
   - **Ctrl+C** / **Ctrl+X** / **Ctrl+V** copy / cut / paste (paste lands at the mouse)
   - **Ctrl+B** = duplicate the selection right after itself
   - **Shift**+drag a note = duplicate it (or the whole selection)
   - **↑ / ↓** move the selection by a semitone (a scale degree with Snap on);
     **Ctrl+↑ / ↓** by an octave
   - **M** = mute / unmute the selected notes (muted notes draw hollow and are skipped)

   **Scale viewer** (Roll toolbar, next to Loop): pick a **root** + **scale** and the
   in-scale rows show bright while out-of-scale rows go dark; the root note is marked
   on the piano keys. **Snap** restricts where you can place / move notes to the
   scale's notes (like FL's snap-to-scale).
5. Press play in FL. By default the loop only plays **while your DAW's channel is
   sending MPE Bender a note** — put a note in the pattern to gate it (any pitch;
   the note itself is silent). Toggle **Free run** on the toolbar to make the loop
   play with the transport regardless.

`Fwd host MIDI` (free-run only) also passes MIDI from the FL piano roll / a
keyboard straight through to Serum, so you can play it live over the loop.

## Notes / limits

- **Any VST3 instrument** can be hosted, not just Serum. Per‑note *independent* bend
  needs the synth in **MPE mode** (own channel + pitch bend per note): Serum 2, Vital,
  Pigments, Massive X, Diva/Repro, Surge XT, Ableton stock, and most modern synths do.
  A non‑MPE synth still works but bends are shared across its voices. VST2‑ and
  AU‑only synths aren't supported (VST3 host only).
- `MPE Channels` = how many member channels (voices) can bend independently at
  once (max 14). Notes beyond that steal the oldest voice.
- Project state saves the Serum path **and** Serum's current patch, so reopening
  a project restores everything.
- Hosting a VST inside a VST is allowed and works in FL Studio; a few other DAWs
  sandbox plugins in ways that can make the nested editor flaky.

## Auto-update

Builds self-update against
[`latest.json`](https://raw.githubusercontent.com/trymhc/mpe-bender/main/latest.json)
in this repo. On startup (throttled to once/day) MPE Bender fetches it, and if a
newer version is published it downloads the new `.vst3` and stages it; the
**Settings → Updates** panel shows the status and an **Install update** button.
Installing launches a tiny detached script that copies the new bundle into place
**as soon as every DAW using the plugin is closed**, so the next launch is the new
version. Nothing happens mid-session and nothing needs admin.

The feed URL is baked in by default (`CMakeLists.txt`); build with
`-DMPE_BENDER_UPDATE_URL=off` to disable it, or point it elsewhere.

### Cutting a release

1. Bump `project(MpePianoRoll VERSION x.y.z)` in `CMakeLists.txt`, commit your changes.
2. `powershell -ExecutionPolicy Bypass -File publish-update.ps1 -Notes "what changed"`

That builds the VST3, zips `MPE Bender.vst3` + `INSTALL.txt` into
`dist/MPE-Bender-x.y.z.vst3.zip`, creates GitHub release `vx.y.z` with it attached,
and rewrites + pushes `latest.json`. Friends on a self-updating build pick it up on
their next DAW restart; new friends download the zip from
[Releases](https://github.com/trymhc/mpe-bender/releases).

## Source map

| File | Role |
|------|------|
| `Source/NoteModel.h` | `MpeNote` (base key + `bend` curve) + `ExpressionCurve` |
| `Source/MpeEngine.*` | notes → MPE MIDI (note-on/off + per-note pitch bend), channel allocation |
| `Source/HostedPlugin.*` | load / prepare / run a VST3 instance (Serum 2) |
| `Source/HostedPluginWindow.h` | floating window for the hosted synth's editor |
| `Source/PluginProcessor.*` | transport, loop playback, state, undo/redo, wiring |
| `Source/PluginEditor.*` | Roll / Settings tabs, hosted‑synth controls, scale + update UI |
| `Source/PianoRollComponent.*` | the note grid; notes drawn/edited as pitch ribbons |
| `Source/KeyboardSidebar.h` | the frozen piano‑key column + scale highlight |
| `Source/Scale.h` | scale masks / names for the scale viewer |
| `Source/UiTheme.h` | Light / Graphite / Dark palettes + `FlatLookAndFeel` |
| `Source/UpdateChecker.*` | background version check + staged self-install |
