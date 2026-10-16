# Engine compatibility

Checked on 2026-10-16, Windows x64.

- Unreal 5.8.3: packaged successfully and all 21 automation tests passed.
- Unreal 5.6.1: final compatibility source packaged successfully and all 21 automation tests passed again.
- Unreal 5.7.4: built against the installed 5.7.4 SDK and all 21 automation tests passed in an isolated editor project, including actual MP4 encoding, fixed EV100, fade-out frame counts, and cancellation.
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
-HandyCleanupFixtures -HandyNamingFixtures -HandyRenderFixtures -HandySanityFixtures
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

Presentation adds an editor dependency on LevelSequenceEditor. Schedule tests cover inherited/override durations and modes, reordering, and shifting later effects. Track tests cover lifecycle versus trigger, component binding, spawn windows, skeletal attachment, preserving unrelated bindings, rebuild and Undo/Redo.

HandyToolbox 2.5 adds the MovieRenderPipeline editor plugin dependency and Recording.Configuration, Recording.RenderAndEncode, and Recording.Cancel. The recording fixtures require -HandyRenderFixtures in an isolated editor project, FFmpeg with libx264, and ffprobe beside it. They render two actual MP4 files (Loop and Once), verify frame counts, resolution and FPS, test startup cancellation, and check stale-list, selected-only, invalid-settings and camera-binding validation. Existing camera cuts and selected-camera snapshots are covered. Early cancellation waits for shot initialization before asynchronous shutdown to avoid a UE 5.6 restoration assertion. No engine source edits are required.

The 2.5 final test runs passed 19/19 on both UE 5.6.1 and UE 5.8.3, including the real render/encode fixture and cancellation. Recording tests use small synthetic systems; final presentation appearance still depends on your camera, lighting and VFX setup.

HandyToolbox 2.6 extends the presentation and recording tests to cover unkeyed Spawned tracks, retaining the same actor across the slot boundary, recording without reintroducing spawn keys, fade-tail frame counts, unchanged activation boundaries, and fixed/viewport/camera exposure modes. The native render smoke test confirms that the fixed-EV100 setting is applied to actual render view families. Exposure uses UMoviePipelineViewFamilySetting and FExposureSettings, matching the viewport renderer override on both supported engines.

The 2.6 final builds passed 19/19 automation tests on UE 5.6.1 and UE 5.8.3, including actual MP4 rendering with fixed exposure and additional fade-out frames.

UE 5.7.4 verification required no source compatibility changes. Its Win64 DLL is packaged separately; use the matching ZIP rather than a 5.6 or 5.8 binary. See Verification/UE5.7.4-Win64.json for the test result summary.


## Pack Sanity Check 2.7

Sanity.Thresholds verifies 2K warnings, configurable thresholds, rectangular power-of-two acceptance, non-power-of-two warnings and triangle limits. Sanity.SavedPack uses saved assets in an isolated project to verify independent folder scopes, external references and local matches, naming, duplicates, unused-content protection, texture dimensions, actual static-mesh geometry, missing material parents, and unchanged references/package dirty state. The navigation regression includes the new sixth tab.

Run -HandySanityFixtures only in an isolated test project. The test temporarily restores that project's /Game mount because earlier integration tests mount source content for reading; it verifies fixture paths before saving. Test projects disable r.CEFGPUAcceleration to avoid an offscreen CEF renderer assertion in UE 5.7. This setting is not installed in user projects or plugin packages.

The scanner uses the public skeletal mesh rendering-resource API across engine versions. Compile results are cached and feature-level-specific; this is not full cook/platform validation. Texture and triangle thresholds are review warnings, not measured performance guarantees.
