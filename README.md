# HandyToolbox (Unreal Engine Editor Plugin)

Editor plugin tested on **Unreal Engine 5.6.1, 5.7.4, and 5.8.3** (Win64) that exposes Niagara module stack inputs as **User Parameters**, with a **full editor tab UI**, Python/Blueprint API, and Content Browser shortcuts.

## Install

1. Copy `NiagaraParamExposer` into `YourProject/Plugins/`.
2. Regenerate project files and build the **Editor** target (Win64).
3. Enable **HandyToolbox** in *Edit → Plugins*.
4. Restart the editor.

## Editor UI

Open from **Tools → Niagara Param Exposer** (nomad tab), or right-click a **Niagara System** in the Content Browser → **Expose Module Inputs...**

The tab lets you:

1. Pick a Niagara System (asset picker or Content Browser selection).
2. Select an emitter; use the **search bar** to filter by stage, module, input, or proposed user name.
3. Inputs are grouped **per module**: **important** inputs (spawn, lifetime, size, color, etc.) show by default; expand **Show all inputs** for the rest. Data interfaces, enums, and mode pins are hidden unless you search for them.
4. Check inputs to expose; edit proposed **User** names (auto-naming toggle uses `<Emitter>_<ShortInput>` rules).
5. Toggle **Put in emitter category** (hierarchy folder named after the emitter; batch expose is additive and keeps existing category members).
6. **Create custom User parameters** (float/int/bool/Vector2D/Vector3/LinearColor) with optional category.
7. **Add Set Parameters** modules (e.g. `Emitter.HueShift` + optional `auto` user link).
8. **Rename emitters** from the UI.
9. View a **status log** (includes **readback** of exposed user parameter values after batch expose); all actions use **undo** (`FScopedTransaction`) and mark the package dirty (no auto-save, no deletes).

### Short-name mapping

Default rules live in `Config/DefaultEditor.ini` under `[/Script/NiagaraParamExposer.Naming]` (`Uniform Sprite Size Min=SizeMin`, etc.). Reloaded at runtime via `NiagaraParamExposerNaming`.

### Important inputs and category sort order

`[/Script/NiagaraParamExposer.ImportantInputs]` — keywords for the default visible list in the tab.

`[/Script/NiagaraParamExposer.SortPriority]` — lower index = earlier in the emitter category (spawn → lifetime → size → velocity → color → opacity). Min/Max ordering is applied within a group.

## Python API

`unreal.NiagaraParamExposerLibrary` — all UI actions are available from Python:

- Inspection: `list_emitters`, `list_modules`, `list_module_inputs`, `get_user_parameters`, `list_user_parameter_categories`
- Expose: `expose_module_input` (`b_assign_to_emitter_category`), `expose_module_inputs_batch`, `expose_common_emitter_parameters`
- User params: `set_user_parameter_float` / `vector2d` / `vector` / `color`, `create_custom_user_parameter`, `rename_user_parameter`, `set_user_parameter_category`
- Stack: `add_set_parameter_module`, `link_module_input_to_parameter`, `rename_emitter`
- Materials: `list_renderer_material_scalar_parameters`, `bind_renderer_material_parameter`

## Technical notes

- **Inspection** does not call `GetStackFunctionInputs` or `GetOrCreateStackFunctionInputOverridePin`.
- Module inputs: assignment targets, `UNiagaraGraph::GetAllMetaData()` (`Module.*`), and `UNiagaraNodeInput` (`Parameter` + `Module.*`).
- Values: rapid iteration on the **emitter script for the stack usage**, `Constants.<Emitter>.<Module>.<Input>`, override pin literals, graph script-variable defaults.
- Stack links: before linking a module input to a User parameter, stale override-pin links are cleared (`RemoveNodesForStackFunctionInputOverridePin` with manual fallback) so re-expose after deleting User params cannot trip Niagara editor asserts.
- Hierarchy: `UNiagaraSystemEditorData::UserParameterHierarchy` via **DataHierarchyEditor** (Build.cs module dep only).
- `UNiagaraHierarchyUserParameter::Initialize` is **not** exported — hierarchy items are wired via reflection + `SetIdentity`.

## Debugging

Filter **Output Log** by `LogNiagaraParamExposer` for enumeration/value-resolution phases.

## Category repair (2026-10-14)

Use **Repair Emitter Categories** in the plugin tab, or call
`unreal.NiagaraParamExposerLibrary.repair_emitter_parameter_categories(system)`.
Existing `<Emitter>_<Input>` parameters are grouped under the exact `<Emitter>`
prefix (split at the final underscore). Names, values, types and graph links are
preserved. Unprefixed parameters and category containers are retained. Existing
misspellings in prefixes remain distinct groups; this operation does not rename
parameters. Category-enabled expose operations also reconcile existing groups.

Hierarchy entries now use the script variable metadata GUID expected by Unreal,
normalize the optional `User.` namespace, and retain correct UObject ownership
when moved. Undo/redo is supported. Repair marks the asset dirty; it does not save
assets or request compilation.

`get_user_parameter_category_assignments(system)` is a read-only inspection API.
Each row contains `short_name|category|valid` (or `invalid` for a broken identity,
metadata reference or parent ownership).

Regression suite: `NiagaraParamExposer.Categories.Regression` in Unreal Automation.
Covers legacy GUID repair, additive grouping, refresh, deletion/recreation,
namespace normalization, Min/Max ordering, LinearColor/default preservation,
large categories, object duplication, and undo/redo.

## Faster exposure from Niagara (2026-10-14)

In the Niagara System editor, select a value input row and click **Expose Selected
Input** in the toolbar, or press **Ctrl+Alt+E**. This uses the actual selected
Niagara input, copies its current local value and type, and creates
`User.<Emitter>_<ShortInput>` in the matching prefix category. Existing unrelated
parameters are never overwritten. For dynamic expressions, select the local
value within the expression instead of replacing the whole expression.
This action changes the system asset through an undoable transaction and does
not save it automatically. No engine source modifications are required.

The plugin panel now has a separate **Search emitters** field. The Lifetime
control comes from **Initialize Particle > Point Attributes**, with filtering
based on the actual Lifetime Mode when it can be identified. Particle State's
Lifetime input is excluded from exposure; it should read `Particles.Lifetime`.

## Native selection and typed Set Parameters

**Expose Selected Input** is available in the Niagara toolbar and under
**Asset → Expose Selected Input**, with **Ctrl+Alt+E** as its shortcut.
Select an attribute row in the Selection panel after opening its module.
Selection detection includes floating Niagara editor windows.

**Add Set Parameters module** offers Float, Vector2D, Vector, Color, Int, and
Bool. Enter vector components separated by commas (for example `1,2,3`),
and color as `R,G,B,A` (for example `0.2,0.4,0.6,1`). Invalid values are rejected.
The `auto` field creates a User parameter with the emitter naming convention;
leave it empty for a local assignment. Existing User parameter names are
rejected to protect their values. The legacy Python `add_set_parameter_module`
API remains float-only; type selection is available in the editor panel.

## Validation

Built and tested on UE 5.6.1, Win64. Four Unreal automation suites passed:

- `NiagaraParamExposer.Categories.Regression`
- `NiagaraParamExposer.Inputs.AssetIntegration`
- `NiagaraParamExposer.Inputs.NativeEditor`
- `NiagaraParamExposer.Inputs.SearchAndLifetime`

The asset integration and native editor tests require `-NPETestContent` and
`-NPETestAsset` arguments pointing to the FireAura test fixture containing the
FireAuraFeet emitter. That project asset is not included in this repository.
Without those arguments, the fixture-dependent tests skip their checks.
Run the native editor test with the full editor window backend, such as
`UnrealEditor.exe -RenderOffscreen`, rather than the headless `-NullRHI` backend.
Tests make their changes on transient copies of the fixture.

### Category checkbox for creation

With **Put in emitter category** checked, both **Create custom User parameter**
and **Add Set Parameters module** put the new User parameter in the selected
emitter's category. This also applies to a custom name without an emitter prefix.
The selected emitter category takes precedence over the optional custom category
field. With the checkbox unchecked, the custom category field is used as before,
and Set Parameters does not assign the new User parameter to a category.

### Assignment links

When Add Set Parameters creates a User parameter (`auto` or an explicit User
name), the module input is linked to it immediately. The link preserves the full
assignment target namespace, including Emitter or Particles. The native-stack
regression checks linked mode and the exact User target for all six supported
types. Leaving the User-link field empty still creates a local assignment.

Expose Selected Input appears at the end of the Niagara System toolbar, after
Scalability. Its Asset menu entry and Ctrl+Alt+E shortcut remain available.

### Custom types and contextual names

Custom User parameters use the same Float, Vector2D, Vector, Color, Int, and Bool
selector as Set Parameters. Vector/color defaults preserve every component.

Native exposure uses the owning attribute for nested Color and Scale Curve
inputs: Color beneath Particles.Color2 becomes EmitterName_Color2, and Color
beneath Particles.ColorMask becomes EmitterName_ColorMask. Scale Alpha and its
nested curve scale use EmitterName_Alpha. Existing unrelated User parameters
are still protected against overwrites; existing names are not renamed.

## Engine versions

See [COMPATIBILITY.md](COMPATIBILITY.md) for verified engine versions, separate binary builds, and the UEFN asset workflow. HandyToolbox 2.6 passes all 19 automation tests on UE 5.6.1, 5.7.4, and 5.8.3.

## Download prebuilt packages

Ready-to-copy Windows packages for Unreal 5.6.1, 5.7.4, and 5.8.3 are in [Packages](Packages/README.md). Each includes installation instructions.

### Cleanup and Undo / Redo

**Clean Up Unused User Parameters** removes unreferenced User parameters from
this system. It protects parameters whose names start with an existing emitter
name and parameters grouped under an existing emitter category, even if unlinked.
It scans remaining emitter/system graphs, nested inputs and renderer/data-interface
bindings, including disabled emitters. A shared parameter stays when another
emitter references it. Ambiguous substring references are kept conservatively.

Cleanup removes the deleted parameters' hierarchy entries. It removes categories made empty by this cleanup. It does not run
category repair or sort remaining entries. The panel log
lists removed names. The asset is marked modified but is never saved automatically.
References outside the Niagara System (such as Blueprint-only controls) are not
part of this system-local usage check.

**Undo** and **Redo** use Unreal Editor's shared transaction history (Ctrl+Z / redo),
including cleanup, exposure, custom creation, module creation, emitter rename and
category repair. Undo restores the cleanup as one action, including parameter
values, types and hierarchy positions. These buttons act on the latest editor
action; they do not skip intervening edits in other editor windows.

The panel header contains Undo/Redo for all editor actions. Cleanup sits at the very bottom below the log. Editing buttons use a muted blue accent; cleanup uses muted amber.

## HandyToolbox: Asset Cleanup

Open Tools > HandyToolbox. Vertical navigation keeps Niagara Parameters and Asset
Cleanup in separate pages. The internal plugin/module ID remains NiagaraParamExposer
so existing project configuration and scripting calls continue to work.

Select folders in the Content Browser, click Use Selected Folders, then Scan.
Only selected project Content (/Game) folders are eligible for deletion. Engine
and plugin content, Niagara Systems, emitters, and other unsupported asset types
are never deletion candidates. Supported types are materials/material instances,
material functions, textures, static meshes and skeletal meshes.

The scanner follows saved hard/soft/package/management dependencies from every
Niagara System found by the Asset Registry. It also protects anything referenced
by retained project content, including referencers outside the selected folders.
An isolated unused material and its otherwise unused textures can be selected
as a group. A shared texture stays protected. Unselected assets are protected
again when checking the requested deletion subset.

Unsaved content or maps block scans: save or discard changes yourself first.
The tool never auto-saves. Review the paths and reasons, select candidates, then
choose Review and Delete Selected. References are rescanned; changed or unsafe
selections are rejected. A warning and Unreal's standard deletion dialog precede
any file deletion. No force-delete API is used. Asset deletion is not covered by
the Niagara parameter Undo/Redo controls. Keep source control or backups. Code,
configuration and string-based runtime loads may not appear in the Asset Registry.

Parameter cleanup now removes categories made empty by deleting their last
parameter, including newly empty parent categories. It preserves the order of
remaining categories and parameters. Undo restores the removed categories too.

Asset Cleanup uses resizable Asset Name, Type, Keep / Remove, Directory and Reason columns. Blue KEEP rows are protected; amber REMOVE rows are eligible, and only checked rows are submitted for deletion. Hover truncated cells for full details. Search also matches readable asset types.

## Reference Checker (HandyToolbox 2.2)

Open Tools > HandyToolbox > Reference Checker. Select one project Content folder,
click Use Selected Folder, save/discard pending edits, then Scan References.
The table lists outgoing package dependencies, including chains through outside
assets. Amber rows leave the selected folder; /Engine and /Niagara are allowed.
Hover cells for full paths. Details identify material expression objects and
properties, material-instance texture parameter names, and Niagara emitter names
when the serialized reference can be resolved. Unresolved package/soft references
are explicitly marked for manual review; arbitrary path strings in code/config
cannot be discovered by an asset dependency scan.

Select Verified Matches and Relink Selected presents a confirmation with each
source, old reference, and replacement. Only referring assets inside the chosen
folder change. External originals are not deleted, and nothing is automatically
saved. Undo/Redo use the editor transaction history. Review, save, and rescan.

Duplicate matching is deliberately conservative: ordinary Texture2D assets need
identical source mip pixels and persistent settings; native materials, constant
material instances and material functions need matching authoring data, owned
objects and external dependencies. Asset names are not evidence of equality.
Multiple matches, meshes, other texture types, custom classes and unverified
material graphs require manual review. Differences in dependency paths can
prevent a match even where the rendered appearance looks the same. The checker
is not a guarantee of marketplace compliance or runtime string-reference closure.

## HandyToolbox 2.3: batch tools and naming

Reference Checker shows External references highlighted by default. Local and Engine / Niagara allowed references are collapsed and expandable. Multiple selected folders remain independent scopes: an asset in another selected folder is still external. Add Selected Assets supports individual assets as well.

Asset Cleanup accepts multiple folders and individual assets while protecting references from retained project content. Niagara Parameters includes a collapsed Batch unused-parameter cleanup section: select folders or Niagara systems, preview, then clean the batch. One Undo restores the entire batch, including removed empty categories. Normal parameter editing remains focused on one system.

Naming Convention is the fourth vertical tab. Select folders or assets, edit the enabled type/prefix/suffix rules, save rules per project, and Preview Names. Defaults include MI_, M_, MF_, T_, NS_, SM_, CC_, CA_, NM_, plus other common types. Conflicting names are skipped. Rename and Fix Redirectors uses Unreal's native rename manager and may save referring packages outside the selected folders. Review Unreal's redirector report if shown; unresolved redirectors are retained. Revert Last Rename reverses the last successful batch in the current session; it is separate from ordinary editor Undo. Save other pending content before renaming. Text paths in code/configuration are not guaranteed to update automatically.

## HandyToolbox 2.4: Presentation

The Presentation tab builds an ordered VFX showcase in a Level Sequence. Choose a sequence from the asset picker, use the open Sequencer, or create a new sequence through Unreal's asset dialog. Add selected Niagara systems or selected Content Browser folders, then use Up/Down to reorder the list.

Set the shared duration, gap between effects, and starting offset in seconds. A row duration of 0 inherits the shared duration; another positive duration overrides that row. Later start times are recalculated automatically, preserving the gap. Each row may inherit or override the shared Loop/Once mode and attachment choice. Loop creates Niagara Component > System Life Cycle (Tick Delta Time). Once creates an FX System toggle track with one Trigger key at the row's start. Spawn tracks bound each effect to its presentation window, including cutting remaining particles at the end, so effects do not spill into the gap or next item.

Attachment defaults on: select a skeletal-mesh actor in the level and choose Use Selected SKM Actor, or choose an existing skeletal actor binding from the sequence dropdown. An optional bone/socket is validated before building. Skeletal-mesh sampling detection is a hint, not a guarantee of an effect's attachment needs; custom explicit data-interface or User parameter sources may still require setup in Niagara. No Niagara System asset is modified. World effects can disable attachment globally or for individual rows.

Build / Update generates spawnable Niagara actors and component tracks. Rebuilding replaces this tool's generated VFX tracks; manual edits to those tracks are replaced, so edit duration and order in the Presentation list. Existing cameras, skeletal meshes, animation and other unrelated bindings are retained. Clear Generated Tracks removes only generated VFX tracks and keeps the list for rebuilding. Clear List only clears the editable list until a later build. Build and Clear support editor Undo/Redo. Save the Level Sequence to persist the list/settings; opening it in Presentation reloads them. The sequence playback range is updated to end at the final effect.


## Presentation recording (2.5)

Record separate MP4 clips directly in **Presentation > Record MP4 Clips**.
Build the presentation first, save your level once, then choose a camera and recording settings. Click **Record Individual MP4 Clips**. Each clip uses its VFX duration; the spacing between effects is excluded. Duration changes require rebuilding the presentation before recording.

- Record all VFX, or enable **Selected VFX only** and use the row Record checkboxes.
- Choose a selected level camera or use the sequence camera cuts. Each VFX needs a valid camera cut covering its whole duration. For nested or multiple cuts within one effect, use the selected camera override.
- Set resolution, FPS, quality, output folder, and loop warm-up frames. Once effects receive no warm-up so their opening trigger is retained.
- Detect or browse to FFmpeg with the libx264 encoder, then check it. FFmpeg is required for MP4 encoding and is not bundled with the plugin.
- Progress, Cancel Recording, and Open Output are in this tab. Keep PNG frames is optional; failed or canceled work retains diagnostic files.

Recording uses private temporary sequence copies and Unreal Movie Render Queue. The presentation sequence, skeletal mesh animation, and existing user render queue are preserved. Other generated presentation VFX are excluded from each clip. Frame ranges are rounded to the selected FPS. Every run gets a unique output folder; finished clips are named by list order and Niagara asset. Encoding uses temporary partial filenames until successful completion. Recording requires the editor to be outside Play mode.


## Natural fades and viewport exposure (2.6)

In Presentation, turn **Add Spawned on/off keys** off and click **Build / Update Presentation** to retain VFX actors after their slot ends. Unreal still needs a Spawned track for a sequence-owned actor: it stays checked with no keys. Auto-activation remains off; Loop/Once tracks control activation. Loop sections stop spawning at their normal end while existing particles can finish. Attachment remains active during the fade. Actors are still removed when the whole sequence stops; allow enough timeline time for the final fade.

In Record MP4 Clips, **Fade-out seconds** adds recording time after each slot without moving the next effect or extending its activation. Turn Spawned keys off to capture natural tails. Other presentation VFX are removed from each recording copy, including effects whose slots would overlap the tail. Existing camera cuts must cover the additional tail, or use a selected camera. Mesh animation must also cover the time you want to record.

Exposure defaults to **Match viewport exposure**. Focus your perspective level viewport before recording: its fixed EV100 override is captured when the batch starts. If the viewport uses Game Settings, recording follows camera/scene exposure. **Fixed EV100** lets you enter an explicit value; **Camera / scene exposure** deliberately uses the original scene settings. Fixed EV100 uses the same renderer exposure override as the viewport, without modifying camera assets, post-process volumes, global exposure variables, or the source sequence.
