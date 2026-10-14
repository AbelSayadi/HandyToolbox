// Copyright NiagaraParamExposer. All Rights Reserved.

#include "NiagaraParamExposerModule.h"
#include "NiagaraParamExposerLog.h"
#include "NiagaraParamExposerDirect.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogNiagaraParamExposer);

void NiagaraParamExposer_RegisterContentBrowserExtensions(FDelegateHandle& OutHandle);
void NiagaraParamExposer_UnregisterContentBrowserExtensions(FDelegateHandle& Handle);
void NiagaraParamExposer_RegisterEditorTab();
void NiagaraParamExposer_UnregisterEditorTab();

void FNiagaraParamExposerModule::StartupModule()
{
	NiagaraParamExposer_RegisterEditorTab();
	RegisterContentBrowserExtensions();
	NiagaraParamExposerDirect::Register();
}

void FNiagaraParamExposerModule::ShutdownModule()
{
	NiagaraParamExposerDirect::Unregister();
	UnregisterContentBrowserExtensions();
	NiagaraParamExposer_UnregisterEditorTab();
}

void FNiagaraParamExposerModule::RegisterContentBrowserExtensions()
{
	NiagaraParamExposer_RegisterContentBrowserExtensions(ContentBrowserAssetExtenderHandle);
}

void FNiagaraParamExposerModule::UnregisterContentBrowserExtensions()
{
	NiagaraParamExposer_UnregisterContentBrowserExtensions(ContentBrowserAssetExtenderHandle);
}

IMPLEMENT_MODULE(FNiagaraParamExposerModule, NiagaraParamExposer)
