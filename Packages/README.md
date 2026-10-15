# HandyToolbox prebuilt Windows packages

Choose the package matching your Unreal Engine version:

- [Unreal Engine 5.6.1](NiagaraParamExposer-UE5.6.1-Win64.zip)
- [Unreal Engine 5.8.3](NiagaraParamExposer-UE5.8.3-Win64.zip)

On GitHub, open the ZIP and select **Download raw file**.
Close Unreal, extract the ZIP, and copy its NiagaraParamExposer folder to
YourProject/Plugins/. Enable HandyToolbox under Edit > Plugins if
needed, then open Tools > HandyToolbox. Each ZIP includes INSTALL.txt.

These are precompiled Win64 editor plugins. They are not native UEFN packages.
Both builds passed the existing regression suites; see ../COMPATIBILITY.md for
coverage and limitations. SHA256SUMS.txt contains archive checksums.

Updated 2026-10-15: unused User parameter cleanup and Undo/Redo controls.

HandyToolbox 2.1 includes vertical Niagara Parameters and Asset Cleanup tabs. Archive and plugin folder names retain NiagaraParamExposer for upgrade compatibility. Asset cleanup scans selected project folders and protects Niagara dependencies and other retained project references. Empty parameter categories are removed with their last unused parameter and restored by Undo.

HandyToolbox 2.2 adds Reference Checker with highlighted outside-folder dependencies and reviewed, undoable relinking to verified local duplicates. See the main README for matching limits.

HandyToolbox 2.3 adds Naming Convention, collapsed reference groups, independent batch folder/asset scopes, and batch unused-parameter cleanup with a single Undo.
