# NiagaraParamExposer (UE 5.6.1 Editor Plugin)

Editor plugin for **Unreal Engine 5.6.1** (Win64) that exposes Niagara module stack inputs as **User Parameters**, with a **full editor tab UI**, Python/Blueprint API, and Content Browser shortcuts.

## Install

1. Copy `NiagaraParamExposer` into `YourProject/Plugins/`.
2. Regenerate project files and build the **Editor** target (Win64).
3. Enable **Niagara Param Exposer** in *Edit → Plugins*.
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
