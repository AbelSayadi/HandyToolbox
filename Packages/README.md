# Prebuilt Windows packages

Choose the package matching your Unreal Engine version:

- [Unreal Engine 5.6.1](NiagaraParamExposer-UE5.6.1-Win64.zip)
- [Unreal Engine 5.8.3](NiagaraParamExposer-UE5.8.3-Win64.zip)

On GitHub, open the ZIP and select **Download raw file**.
Close Unreal, extract the ZIP, and copy its NiagaraParamExposer folder to
YourProject/Plugins/. Enable Niagara Param Exposer under Edit > Plugins if
needed, then open Tools > Niagara Param Exposer. Each ZIP includes INSTALL.txt.

These are precompiled Win64 editor plugins. They are not native UEFN packages.
Both builds passed all four regression suites; see ../COMPATIBILITY.md for
coverage and limitations. SHA256SUMS.txt contains archive checksums.
