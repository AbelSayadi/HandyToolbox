# Engine compatibility

Checked on 2026-10-15, Windows x64.

- Unreal 5.8.3: packaged successfully and all nine automation suites passed.
- Unreal 5.6.1: final compatibility source packaged successfully and all nine automation suites passed again.
- Unreal 5.7: not verified. No 5.7 installation was available. Passing on 5.6 and 5.8 is not a substitute for a 5.7 build and native-editor test.
- UEFN: this native C++ editor plugin is not offered as a UEFN plugin. The checked Fortnite/UEFN installation does not provide the engine build SDK needed for this module. Do not copy an UnrealEditor DLL into UEFN and assume compatibility.

## Cross-version fixes

- Use Algo::StableSort on hierarchy object-pointer arrays to avoid the changed TObjectPtr predicate dereferencing behavior in newer engine versions.
- Include the library's matching header first to satisfy stricter build checks.
- Check UObject lifetime when accepting a system for editing. Niagara simulation/compile readiness does not determine whether an editor asset can be inspected or repaired.

## Verification

Build a separate package for each engine using that engine's RunUAT.bat:

```powershell
& "$EngineRoot/Engine/Build/BatchFiles/RunUAT.bat" BuildPlugin "-Plugin=$PluginRoot/NiagaraParamExposer.uplugin" "-Package=$NewPackageDirectory" -TargetPlatforms=Win64 -Rocket
```

Use a fresh output directory and an isolated test project with the matching package.
Run the full editor with -RenderOffscreen -unattended and these automation arguments:

```text
-ExecCmds="Automation RunTests NiagaraParamExposer"
-HandyCleanupFixtures
-TestExit="Automation Test Queue Empty"
-NPETestContent=<fixture-project-content-directory>/
-NPETestAsset=/Game/FireAura/Particles/NS_FireAura_Emitters
-abslog=<absolute-test-log-path>
```

Require Success for Categories.Regression, Inputs.AssetIntegration,
Inputs.NativeEditor, Inputs.SearchAndLifetime, Cleanup.Regression, Cleanup.AssetIntegration, AssetCleanup.Dependencies, AssetCleanup.Navigation, and AssetCleanup.Registry. The fixture-dependent tests
skip when fixture arguments are missing, so an empty-project run alone does not
verify full compatibility. Check the log results, not just the editor exit code.
The native-window test requires the full window backend, not -NullRHI.

Do not reuse binaries between engine minor versions. Rebuild from source or use
the package made for the target engine. No EngineVersion restriction was added
to the source descriptor, and no engine source files were modified.

## UEFN asset workflow

Use the plugin in supported desktop Unreal Engine to prepare standard Niagara
User parameters and links, save those assets, and migrate the Niagara System
and its dependencies into the UEFN project's Content folder. Then validate and
playtest in UEFN. The plugin is editor-only and does not add runtime parameter
classes, but this does not guarantee that every emitter, renderer, module,
material, or other dependency is allowed in UEFN.

Epic's migration documentation states that migration does not validate target
compatibility and that newer-version assets cannot be moved backward to older
versions. Confirm the destination's supported asset version before choosing a
source engine. Migration compatibility has not been tested on a UEFN project here.

Reference: https://dev.epicgames.com/documentation/fortnite/migrating-assets-from-unreal-engine-to-unreal-editor-for-fortnite

Cleanup tests cover exact hierarchy order, restored types/values, shared references, disabled emitters, renderer-only bindings, emitter deletion, and module/custom creation and emitter rename undo.

Run -HandyCleanupFixtures only in an isolated test project: it creates saved test assets there to verify the Asset Registry dependency scan. It never invokes deletion. AssetCleanup.Navigation opens both HandyToolbox pages. Parameter cleanup tests include removal and undo restoration of newly empty categories.

HandyToolbox 2.2 adds References.IdentityAndUndo and a third navigation page.
The reference tests exercise saved dependency inspection, material parameter detail,
engine exceptions, exact texture pixels/settings, renamed material graphs, material
instance differences, scoped replacement and Undo/Redo. They use a dedicated fixture
mount inside the temporary test project and do not save any original project assets.
Automatic duplicate matching excludes unsupported/custom asset payloads; unresolved
soft/package references remain manual-review rows.

HandyToolbox 2.3 adds Naming.Rules, Naming.RenameAndRedirectors, and Cleanup.BatchUndo. Naming fixtures require -HandyNamingFixtures in an isolated project; they verify saved external material references survive rename and revert. Reference tests also cover independent folder scopes and default collapsed local/allowed sections. Native rename handles asset references, but arbitrary text paths in code/configuration still require review.
