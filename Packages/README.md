# HandyToolbox prebuilt Windows packages

Choose the package matching your Unreal Engine version:

- [Unreal Engine 5.6.1](NiagaraParamExposer-UE5.6.1-Win64.zip)
- [Unreal Engine 5.7.4](NiagaraParamExposer-UE5.7.4-Win64.zip)
- [Unreal Engine 5.8.3](NiagaraParamExposer-UE5.8.3-Win64.zip)

On GitHub, open the ZIP and select **Download raw file**.
Close Unreal, extract the ZIP, and copy its NiagaraParamExposer folder to
YourProject/Plugins/. Enable HandyToolbox under Edit > Plugins if
needed, then open Tools > HandyToolbox. Each ZIP includes INSTALL.txt.

These are precompiled Win64 editor plugins. They are not native UEFN packages.
All three builds passed the 21-test regression suite; see ../COMPATIBILITY.md for
coverage and limitations. SHA256SUMS.txt contains archive checksums.

Updated 2026-10-15: unused User parameter cleanup and Undo/Redo controls.

HandyToolbox 2.1 includes vertical Niagara Parameters and Asset Cleanup tabs. Archive and plugin folder names retain NiagaraParamExposer for upgrade compatibility. Asset cleanup scans selected project folders and protects Niagara dependencies and other retained project references. Empty parameter categories are removed with their last unused parameter and restored by Undo.

HandyToolbox 2.2 adds Reference Checker with highlighted outside-folder dependencies and reviewed, undoable relinking to verified local duplicates. See the main README for matching limits.

HandyToolbox 2.3 adds Naming Convention, collapsed reference groups, independent batch folder/asset scopes, and batch unused-parameter cleanup with a single Undo.

HandyToolbox 2.4 adds Presentation: ordered Niagara showcases in Sequencer, duration/gap controls, per-effect Loop/Once and attachment overrides, and undoable rebuild/clear.

HandyToolbox 2.5 adds MP4 recording inside Presentation: individual clips, all/selected VFX, camera selection, resolution/FPS/quality, loop warm-up, FFmpeg detection, progress/cancel, and optional PNG retention. FFmpeg with libx264 is required separately.

HandyToolbox 2.6 adds optional Spawned on/off keys, recording fade-out time, and viewport/fixed EV100 exposure controls. After changing Spawned keys, rebuild the presentation.

HandyToolbox 2.7 adds the read-only Pack Sanity Check tab with independent pack scopes, reference/naming/compile/unused/duplicate checks, configurable texture and mesh warnings, and saved review notes. All 21 tests passed on UE 5.6.1, 5.7.4 and 5.8.3 on 2026-10-16.
