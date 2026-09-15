# Source and data provenance

- Engine: [ozkl/doomgeneric](https://github.com/ozkl/doomgeneric), commit
  `dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284`. Upstream files are unchanged.
  The native platform adapter is `native/worker.c`. Its corresponding source,
  upstream source and build recipe are included as `licenses/DoomWorker-source.zip`
  in the local test package, with `licenses/DoomGeneric.txt`.
- Shareware data: `DOOM1.WAD` extracted from the original
  [Doom95 shareware archive](https://www.gamers.org/pub/idgames/idstuff/doom/win95/doom95.zip).
  No executables from that archive were run. The complete original archive is
  retained in `downloads/doom95.zip` for provenance.
- Archive SHA-256:
  `7771ebd38d5099aee20a03103ff934e27bfe86693f11224f60a30cf41552dbed`.
- `doom1.wad`: 4,196,020 bytes, SHA-256
  `1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771`.

This repository publishes the experimental source snapshot. Installable test
packages are generated locally using the root README's build instructions.
