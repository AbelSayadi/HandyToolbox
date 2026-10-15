// Copyright NiagaraParamExposer. All Rights Reserved.

#include "NiagaraParamExposerEditorTab.h"
#include "NiagaraSystem.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "NiagaraParamExposer"

static void OpenExposerForSystem(UNiagaraSystem* System)
{
	NiagaraParamExposer_OpenEditorTab(System);
}

static TSharedRef<FExtender> ExtendContentBrowserAssetSelection(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();

	TArray<UNiagaraSystem*> NiagaraSystems;
	for (const FAssetData& AssetData : SelectedAssets)
	{
		if (AssetData.AssetClassPath == UNiagaraSystem::StaticClass()->GetClassPathName())
		{
			if (UNiagaraSystem* System = Cast<UNiagaraSystem>(AssetData.GetAsset()))
			{
				NiagaraSystems.Add(System);
			}
		}
	}

	if (NiagaraSystems.Num() == 0)
	{
		return Extender;
	}

	Extender->AddMenuExtension(
		"GetAssetActions",
		EExtensionHook::After,
		nullptr,
		FMenuExtensionDelegate::CreateLambda([NiagaraSystems](FMenuBuilder& MenuBuilder)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("OpenExposer", "Expose Module Inputs..."),
				LOCTEXT("OpenExposerTooltip", "Open HandyToolbox Niagara Parameters for this system."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([NiagaraSystems]()
				{
					OpenExposerForSystem(NiagaraSystems[0]);
				})));
		}));

	return Extender;
}

void NiagaraParamExposer_RegisterContentBrowserExtensions(FDelegateHandle& OutHandle)
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TArray<FContentBrowserMenuExtender_SelectedAssets>& CBMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	CBMenuExtenderDelegates.Add(FContentBrowserMenuExtender_SelectedAssets::CreateStatic(&ExtendContentBrowserAssetSelection));
	OutHandle = CBMenuExtenderDelegates.Last().GetHandle();
}

void NiagaraParamExposer_UnregisterContentBrowserExtensions(FDelegateHandle& Handle)
{
	if (Handle.IsValid())
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		ContentBrowserModule.GetAllAssetViewContextMenuExtenders().RemoveAll([Handle](const FContentBrowserMenuExtender_SelectedAssets& Delegate)
		{
			return Delegate.GetHandle() == Handle;
		});
		Handle.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
