// Copyright NiagaraParamExposer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FNiagaraParamExposerModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterContentBrowserExtensions();
	void UnregisterContentBrowserExtensions();

	FDelegateHandle ContentBrowserAssetExtenderHandle;
};
