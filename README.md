# MPE Bender

A VST3 **instrument** for Windows that hosts **Serum 2** (or any VST3 synth) inside
itself and drives it from a built‑in piano roll where **every note has its own
pitch‑bend, pressure and timbre curve**. The per‑note expression is sent to the
hosted synth as MPE (MIDI Polyphonic Expression), so bends are truly independent
per note instead of one shared pitch‑bend for the whole track.

```
   ┌─────────────────────────  MPE Bender (VST3 instrument)  ─────────────────────────┐
   │  piano roll + bend/pressure/timbre lanes                                         │
   │              │                                                                  │
   │              ▼                                                                   │
   │   MpeEngine ──► MPE MIDI (note + per‑channel pitch bend / CC74 / pressure)       │
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

FL Studio only reliably scans the system folder `C:\Program Files\Common Files\VST3`
(custom search paths were not picked up here). Writing there needs admin once:

```powershell
# elevated PowerShell (Win+X -> Terminal (Admin))
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

Then in FL: **Options → Manage plugins → Find installed plugins**, search `bender`.
It shows up as a **Synth**. Re-run `install.ps1` after each rebuild.

## Use it (FL Studio)

1. Add `MPE Bender` on a channel as an **instrument** (not an effect).
2. Click **Load Serum 2…** – it auto‑points at
   `C:\Program Files\Common Files\VST3\Serum2.vst3`. Pick the `.vst3`.
3. Click **Open synth UI** to show Serum's own window. In Serum 2, **turn on MPE**
   and set its pitch‑bend range to match the **PB Range** slider here (default 48
   semitones).
4. Draw notes in the piano roll. Select a note, then draw points in the
   **Pitch Bend** lane (semitone offset), **Pressure**, or **Timbre** lane.
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
| `Source/NoteModel.h` | `MpeNote` + `ExpressionCurve` (breakpoint envelopes) |
| `Source/MpeEngine.*` | notes + curves → MPE MIDI, member‑channel allocation |
| `Source/HostedPlugin.*` | load / prepare / run a VST3 instance (Serum 2) |
| `Source/HostedPluginWindow.h` | floating window for the hosted synth's editor |
| `Source/PluginProcessor.*` | transport, loop playback, state, wiring |
| `Source/PluginEditor.*` | toolbar + hosted‑synth controls |
| `Source/PianoRollComponent.*` | the note grid |
| `Source/CurveLaneComponent.*` | one expression lane (bend / pressure / timbre) |
