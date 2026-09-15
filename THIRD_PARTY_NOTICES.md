# Third-party source and data

- **doomgeneric**: https://github.com/ozkl/doomgeneric at
  `dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284`. Upstream source is vendored
  unchanged under `TabletDoom/vendor/doomgeneric/`; see its
  [GPL license](TabletDoom/vendor/doomgeneric/LICENSE). The custom platform
  adapter is `TabletDoom/native/worker.c`. The packaging script includes the
  worker's corresponding source and build recipe.
- **MinHook**: https://github.com/TsudaKageyu/minhook at
  `8af6b4acae5a9388fd742b56fa79ece89d96f823`. Source and notices are under
  `PersonalDiscTrainer/native/vendor/minhook/`; see
  [LICENSE.txt](PersonalDiscTrainer/native/vendor/minhook/LICENSE.txt).
- **Zstandard**: https://github.com/facebook/zstd at
  `ee650ef23636145c97ded0440ca743a54362a3ab`. This snapshot includes the public
  header, generated single-file implementation and the upstream
  [BSD license](PersonalDiscTrainer/native/vendor/zstd/LICENSE) and
  [GPL license](PersonalDiscTrainer/native/vendor/zstd/COPYING).
- **Doom shareware data** is separate from the engine's source license. The WAD
  and original Doom95 shareware distribution are recorded with hashes in
  [TabletDoom/SOURCES.md](TabletDoom/SOURCES.md). The original distribution is
  preserved in `TabletDoom/downloads/doom95.zip`.

Echo VR tablet build inputs are snapshots of the resources/manifests used by the
existing tablet mod. This repository does not include the Echo VR executable or
script DLLs; packaging reads the required script inputs from a local installation.
