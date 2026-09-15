# Echo VR — menu / "tablet" UI: canvases, textures, button presses, tabs

Decoded 2026-09-11 from `echovr.exe` (2023-05-24 build) via ReVault and from the
package data on this PC (`C:\echovr\ready-at-dawn-echo-arena\_data\5932408047\rad15\win10`).
Reader: `echovr_ui.py` (needs `echovr_pkg.py`, `echovr_symbols.tsv`).
Addresses are authoritative; ReVault labels were sometimes auto-generated.

Goal this serves: add a "Tools" tab (goalie-trainer on/off) to the in-game menu.

---

## Scope correction, 2026-09-14

These notes primarily describe the pause/settings menu. The requested hand/arm
social tablet uses a separate native touch-button system in `r14_glb_global_mp`.
See [TOOLS_TAB.md](TOOLS_TAB.md) for its fifth-tab implementation, shared-map
scope, current verification status and project file locations.

## 1. Big picture

The whole menu is **data**, not code:

```
level (CGameLevelResource, e.g. mnu_master)
 └─ actor with a CanvasUI component (CCanvasUICR record)
     ├─ CUICanvasResource   = the 2-D layout: elements (Sprite/Text/Panel/...) + behaviors (buttons, radio, anims)
     ├─ CGTextureResource   = each Sprite's texture, referenced by name hash
     └─ TextureOverride     = the canvas is rendered to a render target ("CanvasUI RT") which replaces
                              a texture on the actor's model (the physical "tablet"/panel mesh)
 └─ actors with CR15UIPage2 / CR15UIPage2Element components  = pages ("tabs") and per-page element states
 └─ CScriptCR (visual scripts)                               = glue: OnPointerInteractPress → EnablePage, SetText, ...
 └─ CR15PointerInterface / CR15PointerMesh / CR15Pointer     = the hand laser pointer that drives presses
```

Levels that carry menu UI (all in manifest `48037dc70b0ecab2`):

| level | CanvasUI comps | UIPage2 | notes |
|---|---|---|---|
| `mnu_master` | 83 | 81 KB | main lobby menu (`mnu_master_mp_ingame` = in-match variant) |
| `d09afd15b1c75c04` (name unknown) | 129 | 50 KB | customization / store / battle-pass panes, lobby terminals ("MATCHMAKING TERMINAL", "RESERVED FOR PARTY LEADER") |
| `mpl_*` levels | few | — | HUD / scoreboards |

277 `CUICanvasResource`s exist in total. Their names are hashes only (not in the exe symbol table); element names mostly likewise, except generic ones (`Background`, `header`, `title`, `text`, `social`, `RightButton`, ...). Localized labels are **not** in the canvas (set at runtime by script nodes such as `SetText2Node`), so canvases can't be identified by label text.

---

## 2. CanvasUI component record (`CCanvasUICR`, stride 0x58, level-level resource)

Parsed by `CCanvasUICS::InitCanvasUICI` @ `0x14031d1d0` (CD = record + 0x20 is passed as `param_3`, so CD offsets below are record offsets − 0x20... written here as **record** offsets):

| rec off | type | meaning |
|---|---|---|
| +0x00 | sym | component type (`CanvasUI`, or a variant sym) |
| +0x08 | sym | actor |
| +0x18 | u64 | flags |
| +0x20 | sym | **CUICanvasResource name** |
| +0x28 | f32,f32 | world size in metres (e.g. 5.0 × 4.2) |
| +0x30 | f32 | pixels per metre (150 / 412) |
| +0x38 | u32 | flag (→ CI bit 8) |
| +0x3c | u32 | flag (→ CI bit 0x10, "shared" path) |
| +0x40 | sym | transform component to attach to (−1 = actor base transform) |
| +0x48 | sym | model/instance whose texture is overridden (−1 = none) |
| +0x50 | sym | **texture to override** (CGTextureResource name). Almost every menu canvas uses the same 256×256 BC1 placeholder `34dfbe67e4424f76` |

Render path: `CCanvasUICS::RenderMT` draws the canvas into a render target; the `TextureOverride` component (found via `CSortedMap::Find(..., 0xa621530b7aaca923)`) swaps that RT in for the model's texture (`FUN_1402ef850`). Errors: "[CANVAS] No TextureOverride component on actor %s".

## 3. `CUICanvasResource` on-disk layout

Loaded by `CUICanvasInstance` (vtable `0x1416f1298`): slot 1 `0x1407216e0` = LoadResource(type sym `sym("CUICanvasResource")` = `46222c5d2afeb3ff`), slot 3 `0x14071fc90` = **OnLoaded** (copies the arrays below into the instance), slot 4 `0x1407287b0` = Unload. Instance fields: `+0x8` resource, `+0x90` elements ptr, `+0xc0` element count, `+0x1f8` behaviors ptr, `+0x228` behavior count, `+0x318` computed element quads (float4 per element), `+0x370` owning CS, `+0x378` parent canvas (child canvases), `+0x390/+0x398` child instances.

Header (0x1b0 bytes). Array descriptors are the engine's serialized `CMemBlock`+array: `ptr(0 on disk) | byteSize | alloc | u32 flags @+0x1c | capacity | ? | count`; array **data follows the header sequentially** in this order: elements, behavior records, behavior data blobs, texture table, ..., text bytes.

| off | meaning |
|---|---|
| +0x00 | canvas name sym |
| +0x08 | version |
| +0x0c/+0x10 | render-target size w,h |
| +0x14/+0x18 | **layout size in pixels** (ElementQuad divides by these) |
| +0x1c | mode: 2 = texture-override canvas, 3 = ?, 4 = world/large (only modes 2 and 4 create the RT) |
| +0x28 | u32 (layer count?) — *not* the element count |
| +0x34..+0x50 | 4 floats colour/tint + 2 floats pivot (1,1,1,1, 0.5,0.5) |
| +0x50 | elements array; **count at +0x80**; stride 0xe0 |
| +0x88 | behaviors array; **count at +0xb8**; stride 0x58 |
| +0xc0 | texture/asset table; count at +0xf0; stride 0x18 (`sym, u64, f32 x2`) |
| +0x100 | u32 array (count +0x130) |
| +0x138 | text bytes (UTF-8, count +0x168) — default strings of Text elements |
| +0x170 | array, usually empty |

**Element record (0xe0):**

| off | meaning |
|---|---|
| +0x00 | element name sym |
| +0x08 | type: 1 Text, 2 Panel, 3 Sprite, 4 Mask, 5 EndMask, 6 RenderPool, 7 ChildCanvas, 8 ColorMask (`FUN_140c471a0`) |
| +0x24..+0x30 | anchor min/max (fractions of parent quad) |
| +0x34..+0x40 | offsets in pixels (x0,y0,x1,y1) added to anchored quad — `ElementQuad` @ `0x14071c240` |
| +0x5c | parent element index (−1 = canvas) |
| +0x60 | element id (== index in shipped data) |
| +0x78 | Mask: texture sym |
| +0x88 | **Sprite: CGTextureResource name sym; Text: CTTFontResource name sym** (registered by `FUN_140719b80`) |
| +0xa8 | ColorMask: texture sym |
| +0x78 | ChildCanvas: canvas name (`FUN_1407235e0` creates the child instance); corrected from +0x98 |

**Behavior record (0x58):** `+0 type sym` (= `sym("R15PointerInteractBehavior")` etc. — the class name without the `C`), `+8 behavior name sym`, `+0x10` data MemBlock (byte size at +0x18, data lives in the blob area after the records), `+0x48 element index`, `+0x50 u16`, `+0x52 u32`, `+0x56 u16`. `InitializeBehaviors` @ `0x1407205c0` looks the type sym up in the CS's registry (`CS+0x1b8` sorted map → index into `CS+0x180` `SUIBehaviorTypeItems` array) and calls the factory's vslot 3 (e.g. `FUN_14010e2d0` for PointerInteract) then the behavior's vslot 7 `Init(componentId, elementId, canvasInstance, elementPtr)`. Unknown type → assert "Behavior type '%s' (found in canvas %s) not registered" (`cuicanvasinstance.cpp:0x93d`).

Known behavior classes (35): `R15PointerInteractBehavior`, `R15PointerInteractStateControlBehavior`, `R15PointerCursorBehavior`, `RadioButtonBehavior`, `R15ElementAnimationBehavior`, `R15SelectedFrameBehavior`, `ScrollableList/Element/PageBehavior`, `FlipbookBehavior`, `TextAnchor/TextResizeBehavior`, `R15DynLoadTextureBehavior`, `R15ItemPreview/ItemRarityFrame/StoreItem/BattlePass*`, `R15TextEntryBehavior`, ... (full list in `echovr_ui.py`).

A **button** in shipped data = a Panel/Sprite element carrying `R15PointerInteractBehavior` (16 B data) + `R15PointerInteractStateControlBehavior` (304 B data: named states idle/hover/press with animation links) + usually `R15ElementAnimationBehavior`. A **tab strip** additionally uses `RadioButtonBehavior` (links the sibling buttons; `FUN_1410474a0`).

## 4. Textures (`CGTextureResource`)

Two resources per texture: `Win10` (256-byte header) and `Win10GPU` (148-byte header + BCn payload).
Header (all 0xff until 0xc0): `+0xc0 u32 1 | +0xc4 width | +0xc8 height | +0xcc depth | +0xd0 mips | +0xd8 DXGI_FORMAT (71 = BC1_UNORM, 99 = BC7_UNORM) | +0xe8/+0xec w,h | +0xf4 GPU blob size | +0xf8 payload bytes`.
Menu icons seen: 256×256 BC7 (`ec1cb9187987a78b`, 65 684 B). The shared canvas override target `34dfbe67e4424f76` is 256×256 BC1. `python echovr_ui.py tex 0x<sym>` prints this.

**Editing an existing tab icon** = replace that sprite's `CGTextureResource/Win10GPU` payload (same size/format) or repoint the element's `+0x88` sym at another texture. **Adding a new icon** = add a `CGTextureResource` pair to the manifest (new name sym) — same problem as adding any resource (see `ECHOVR_GRABBABLE_OBJECTS_NOTES.md` §6 for the manifest-append procedure).

## 5. Button presses — the full path

1. **Input** — `CR15PointerCS` update `FUN_140a00d20`: per pointer record (0x58 stride at `CS+0xf8`), reads the input frame (`FUN_140f9b8b0`) and sets `flags@+2`: bit0 = held (`FUN_140f9b770(input, hand)` — action 0 left / 1 right / 2 either), bit1 = just-changed edge (`FUN_140f9bf40`). `SetRayLength` @ `0x140a009e0`.
2. **Ray → canvas** — `CR15PointerInterfaceCS` update `FUN_140a04d90`: for each pointer-interface component (0x20 records at `CS+0xf8`) intersects the ray with the canvas actor's bounding sphere + plane, writes the hit as normalized canvas coords `+0x18,+0x1c` (or FLT_MAX when missed), copies press bits into `+2`, fires component events `0x88c4f2ae8eccd739` (held) / `0xf111358b713670cb` (edge) / `0x55c56d4a9201a66c` (became hovered).
3. **Element hit-test + state machine** — `CR15PointerInteractBehavior::Update` @ `0x140c47a80` (called from the behavior tick `FUN_140126a50`): gets pointer canvas position (`FUN_140a04a00`), element quad via `ElementQuad(inst, out, elementId, 4)` normalised by canvas pixel size, sets hover state (`FUN_140c453a0(this, 1|2)`), and fires delegates through **`FUN_1407269f0` = CUIBehavior::FireDelegate(canvasInstance, behavior, delegateSym, alsoComponentEvent)**:
   - `bhvr_delegate_OnPointerInteractIdle`   = `27b7ebd0100cb25d` (after `+0x74` timer expires)
   - `bhvr_delegate_OnPointerInteractHover`  = `0c40e9c29ebcc824`
   - `bhvr_delegate_OnPointerInteractPress`  = `0c40e9da83afde25`  (held && hovered && state!=pressed)
   - `bhvr_delegate_OnPointerInteractRelease`= `e6d9364a71459d93`
   Behavior object: `+0x44` component id, `+0x48` canvas instance, `+0x50` element ptr, `+0x60/+0x68` pointer-interface component, `+0x70` hover state (1 idle, 2 hover), `+0x72` press state (1 up, 2 down).
4. **FireDelegate** pushes an `SUIBehaviorEvent` (0x28 B: `delegateSym, componentId, elementId, behaviorName, ?, actorSym`) into the CS event table (`CEventDBT<SUIBehaviorEvent>`, id `0x730193eae005c616`) **and** dispatches a component event keyed by the delegate sym (`DispatchComponentEventInternal` @ `0x14050fed0`) — that is what the level's visual **scripts** (`CScriptCR`) bind to (`CBindUINode`/`CCreateUIBindingsNode`).
5. **Scripts** react with UI nodes: `R15UIPage2EnablePageNode` (`FUN_140bfd1a0`), `R15UIPage2ElementSetStateNode`, `RadioButtonSetCheckedNode`, `SetSpriteUINode`, `SetText2Node`, `R15PlayUIElementAnimationNode`, `R15ChangePointerInteractStateNode`, ... (script node names all appear as `OlPrEfIx…` strings in the exe).

## 6. Tabs = `CR15UIPage2`

`CR15UIPage2CS` init `FUN_140a6a370` (`cr15uipage2cs.cpp`) and `FUN_140a6c8d0` (exclusive groups). Each page actor lists: connected actors to enable/disable, connected `UIPage2Element`s with the **state** each element takes when the page is enabled, layouts, blackboard targets, sub-pages, and an **exclusive page group** (records `+0xf8 count / +0xc8 syms`, stride 0x148). Enabling a page (`FUN_140a697a0`) dispatches event `0xd64f3478683e4a9c`, activates its actors (`FUN_1403faee0`), applies element states (`FUN_140a66620/665f0/66700`, deferred through `CDeferredMethodQueue`), and disables the other pages of its exclusive group — that is tab switching. `mnu_master` has 0x3a = 58 page records and 57 page-element records (`CR15UIPage2CR` is a struct-of-arrays container; use `parse_cr` = None path).

## 7. Scripts are native DLLs (from the `Downloads\Notes\docs` corpus, verified here 2026-09-11)

Details: `notes_corpus_digest/SCRIPTING.md`. The short version:

- `CScriptResource` on disk is a 4-byte zero stub. The script is `bin\win10\scripts\<hash>.dll` (567
  present), a plain PE with one export `setup_bindings(EXECUTION_BINDINGS*)`; the engine does
  `LoadLibraryW` + `GetProcAddress` and nothing else (`sub_1400EB0F0` = LoadLibrary wrapper, RVA 0xEB0F0).
- `CScriptCR` (720-B entries, container header 0x38) binds a DLL to an actor: `+0x08` actor, **`+0x20` DLL
  hash**, then 12 descriptor tables of variable defaults / actor bindings. `mnu_master` has 218 entries →
  105 DLLs; a `CScriptStateStackCR` marker must sit on the same actor.
- Node types are engine-registered (450 nodes / 463 expressions); a DLL calls `lookup(typehash)` where
  typehash = exe-symbol hash of the node name (`R15UIPage2EnablePageNode` = `6d453494a15afca1`,
  `SetText2Node` = `97ee244dcdc94ead`, `SetSpriteUINode` = `5c2ac25c45064ccf`,
  `RadioButtonSetCheckedNode` = `44cc207763f0f0ef`, `CreateUIBindingsNode` = `3c9ed05de4259d55`).
- In `mnu_master`, ~55 one-actor scripts each call `R15UIPage2EnablePageNode` — one per page actor. No
  shipped DLL references the press delegate hash, so **button → page is data** (`CR15UIPage2ElementCR`
  event forwarding into the page actor); the page's script only enables the page.
- Several canvases share their name hash with their driving script (`86300ac7254c2eb8`,
  `0fb32c3be4168307`, `333eece492710114`, `0a8654ced24a070b` …): canvas + script come in pairs.

## 8. Shipping modified data (same corpus; `notes_corpus_digest/LOADER_PLUGINS_ECHOMOD.md`)

- Byte loader `sub_140FA1FE0` (cresourcemanager.cpp:1040) fills an IO request from the merged manifest
  table; manifest mount `AddManifestPackage` = `sub_140FA09F0` **overwrites** an existing `(type,name)` slot
  in place and inserts new ones, and the engine itself calls it a third time at level load (DLC path).
  File leaves: `sub_1400E0CD0` FileExists, `sub_1400E15E0` FileOpen, `sub_1400E1AC0` FileRead.
- Their MinHook loader proved live: mounting an extra manifest/package pair at level load, redirecting
  `manifests/<H>` / `packages/<H>_N` opens, redirecting script DLLs. Loose raw bytes fail where the engine
  expects ZSTD frames (`"Unknown frame descriptor"`), so bulk data must be a real package. Overriding a
  boot-time global such as the menu canvases (main manifest `48037dc70b0ecab2`) is untested.
- Stock plugin loader: `bin\win10\plugins\*.dll` (`RadPlugin*` entry points; a DllMain-only DLL also runs).
  This install already uses it (`pass_frenzy.dll`).
- echomod (`D:/Echo/CustomMaps/source/echomod/`, CLI `rad_archive_tool.py`): byte-exact extract/repack of
  the whole archive, adds new `(type,name)` entries; has codecs for `CCanvasUICR`, `CGTextureResource`,
  `CScriptCR`; `CUICanvasResource` and `CR15UIPage2CR` are round-trip-only there (my decode in §3 goes
  further for the canvas).
- No manifest checksum or server-side package validation was found.
- **Client-side patch tool (2026-09-11): `echovr_patch.py`.** Rewrites `manifests/48037dc70b0ecab2`
  (header `u32 npkg | u32 0x80000 | 3 x (desc56 + u64)`, A/B/C arrays, signed sort) and appends one new
  package file `48037dc70b0ecab2_<npkg>` holding one zstd frame per patched resource; overrides existing
  `(type,name)` slots in place and adds new ones. Re-serializing the stock manifest reproduces it byte-exact.
  `build` / `verify` / `install` (backs up to `<id>.orig`) / `uninstall`. First test patch:
  `patches/test_swap_button_textures.json` swaps the GPU payloads of two 1024x1024 BC7 menu textures.
- **Validated 2026-09-11 (headless client on the server box, same build):** patched manifest + `_3` package
  load `mnu_master` and the global HUD with zero data errors. Visual tests run on the play PC via
  `dist/probe_A` / `dist/probe_B` (`install.ps1`/`uninstall.ps1`, plan in `dist/TEST_PLAN.md`): texture swap,
  canvas element-rect edit, script-DLL proxy (`script_proxy/`, replaces `3b3497ba718fd030.dll`, logs to
  `bin\win10\script_proxy.log`), and (B) a brand-new texture resource referenced from an edited canvas.

## 9. What "adding a Tools tab" requires — revised plan

**Milestone 1 — zero data edits (days).** Hook `FireDelegate` @ `0x1407269f0` in your plugin DLL; when
`delegateSym == 0x0c40e9da83afde25` and the behavior's element name matches a chosen existing button,
toggle the goalie trainer. Proves the plumbing end to end.

**Milestone 2 — reuse a stock page (cheap).** Pick one of the 55 `mnu_master` page actors, find its DLL via
`CScriptCR +0x20`, and replace that DLL with your own `setup_bindings` DLL (same export, same 480-B struct;
keep `lookup(R15UIPage2EnablePageNode)` so the page still opens). Your `update` runs native code every
frame while the page is enabled — put the trainer toggle and status text (`SETTEXT2NODE`) there. Loader
redirect for the DLL: Hook D on `sub_1400EB0F0`, or just overwrite the file.

**Milestone 3 — a real Tools tab (data + package).** Build with echomod:
1. `CUICanvasResource` of the tab-strip canvas: append a Sprite (+ Text) element (0xe0) and the behavior set
   (PointerInteract 16 B, StateControl 304 B, ElementAnimation, RadioButton linked to the siblings), bump
   the descriptor counts; data blobs follow the DEFERRED-DATA order.
2. New `CGTextureResource` pair for the icon: 256-B primary (`+0xc4/+0xc8` size, `+0xd8` DXGI 99,
   `+0xf4/+0xf8` sizes) + DX10 DDS as the `Win10GPU` sibling (`typesym beac1969cb7b8861`).
3. `CR15UIPage2CR` / `CR15UIPage2ElementCR`: add a page row (five descriptors at +32/+88/+144/+200/+272,
   pooled) in the tab exclusive group, plus the element-state rows; layout needs one more RE pass on the
   element structs (start from the 57 stock rows).
4. `CScriptCR`: one 720-B entry binding your DLL to the new page actor (+ StateStack marker) and the 4-B
   `CScriptResource` stub named by the hash.
5. Pack as a small manifest/package pair and mount it via `AddManifestPackage` before `mnu_master` loads
   (or rebuild the boot manifest if the "already been loaded" guard fires). Clients validate nothing.

Still open: which `mnu_master` canvas is the tab strip (candidates from `echovr_ui.py level mnu_master 3`,
confirm in-headset), the `UIPage2` element-struct fields, and how a script subscribes to UI delegates
(`sendevent@0x188` struct is unknown — not needed for milestones 1–2).

## 10. Address index

| VA | what |
|---|---|
| `0x1403246b0` | `CCanvasUICS` init: creates instances, calls `InitCanvasUICI`, then `fcn.14031f900` (behavior init + event table) |
| `0x14031d8e0` | per-CI: allocate `CUICanvasInstance` (0x3c0 B), Load(canvasSym) |
| `0x14031d1d0` | `CCanvasUICS::InitCanvasUICI` (transform, override model/texture, render target) |
| `0x1402a0460` | `CUICanvasInstance` ctor (vtable `0x1416f1298`) |
| `0x1407216e0` | `CUICanvasInstance::Load` |
| `0x14071fc90` | `CUICanvasInstance::OnLoaded` (blob → instance) |
| `0x1407235e0` | child canvases (element type 7) |
| `0x1407205c0` | `InitializeBehaviors` |
| `0x14071c240` | `ElementQuad` |
| `0x1407269f0` | `CUIBehavior::FireDelegate` |
| `0x140c47a80` | `CR15PointerInteractBehavior::Update` |
| `0x140c3fb30` | `CR15PointerInteractBehavior::Init` (vslot 7) |
| `0x14010e2d0` | `SUIBehaviorTypeItems<CR15PointerInteractBehavior>::Create` (type sym `54384993f5a47462`) |
| `0x1410474a0` | `CRadioButtonBehavior::Init` |
| `0x140a04d90` | `CR15PointerInterfaceCS` update (ray → canvas hit) |
| `0x140a00d20` | `CR15PointerCS` update (input → press bits) |
| `0x140a6a370` / `0x140a6c8d0` | `CR15UIPage2CS` init / exclusive groups |
| `0x140a697a0` | `UIPage2::EnablePage` |
| `0x140bfd1a0` | `R15UIPage2EnablePageNode` (script node) |
| `0x140c471a0` | element type → name |
| `0x14050fed0` / `0x140510060` | DispatchComponentEvent |
