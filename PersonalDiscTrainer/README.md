# Shared native tablet runtime

**Doom is disabled by default.** `DOOM_ENABLED=false` in `native/runtime.cpp`
hides and gates the ? tab, prevents worker launch and skips both Doom rendering
hooks. Tools, Personal Disc and Goalie remain available. See the
[enable/disable instructions](../README.md#enable-or-disable-doom) to change the
flag and rebuild/reinstall; the source and assets are preserved.

Tablet Doom uses the `EchoTabletTrainer` runtime to load through Echo's script
loader, manage tablet pages and submit Doom frames to the native canvas renderer.
This source snapshot includes the runtime, script wrappers, manifest merger,
installer, native tests and the generated headers needed to compile them.

Follow the [repository README](../README.md) for the complete build, packaging,
install and restore commands. The generated `native/dist/` directory and local
test ZIP are build outputs, not tracked source.

## Included Tools features

- Personal Disc starts **on** each launch. The Tools toggle can disable it; the
  existing v3.0.1 match-phase guard still controls permitted spawning.
- Goalie starts **off**. Its Tools toggle starts/stops a 60-second session.
  Disabling Personal Disc also stops Goalie.
- Goalie requires an arena and a phase permitted by the Personal Disc guard.
  Expiry, a disallowed phase or a map transition cancels the session.
- Shots target the blue goal. The generated bank includes direct, bounce and
  cut/slap plans, with launch speeds of 18-20 m/s.

Native tablet loading and Personal Disc switching have been observed in game.
Direct/bounce shots and lobby denial were confirmed in the development workspace.
The newer faster shots, cut/slap behavior and active-round cancellation still
need headset verification. Doom's own remaining checks are listed in the
[Doom notes](../TabletDoom/README.md).

## Build inputs

`native/generated/` contains executable signatures, Doom page symbols, trainer
shot/cut tables and the Zstandard amalgamation. The shot bank is preserved from
the development workspace so compiling this runtime does not require the
separate DiscTracer collision-model research project. Its rebaking tools and
broader trajectory tests live in that workspace.

`prepare_native.py` validates the game and script inputs, refreshes the binding
header, copies the preserved original scripts and stages the generated Doom
tablet patch. `package_native.py` assembles the local installer package, integrity
manifest, third-party notices and corresponding Doom worker source archive.

The Visual Studio path is currently explicit in `native/build.cmd`; adjust it
for another development machine. The build uses the static CRT and executes
`native/tests.cpp` policy/cut tests. Runtime diagnostics are written to
`%LOCALAPPDATA%\EchoTabletTrainer\trainer.log`.
