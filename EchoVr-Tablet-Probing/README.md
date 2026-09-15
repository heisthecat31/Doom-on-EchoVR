# Tablet build support

This is the subset of the tablet research workspace required by Tablet Doom.
The Python tools read/edit Echo's package manifests and UI canvases; the Doom
builder imports `build_tools_tab.py` for the shared layout definitions.

`build/tools_tab/manifests/`, `build/tools_tab/packages/` and `patch_items.json`
are committed build inputs from the existing Tools page. The timestamped
`backups/20260914T235837383130Z/manifest.before` is its pinned pre-Tools baseline.
They are intentionally included so the Doom patch can be regenerated from a
fresh clone. Do not replace them with snapshots from a different game build.

The package helper respects `ECHOVR_GAME` (game root) and `ECHOVR_DATA` (explicit
data root). The main build instructions are in the [root README](../README.md).
Renderer research is in [ECHOVR_TABLET_UI_NOTES.md](ECHOVR_TABLET_UI_NOTES.md).
