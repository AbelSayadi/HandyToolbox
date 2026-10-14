#pragma once
#include "NiagaraParamExposerTypes.h"
class UNiagaraStackFunctionInput;
class UNiagaraSystem;
class SWidget;
class SWindow;
namespace NiagaraParamExposerDirect
{
 FNiagaraParamExposerOperationResult ExposeInput(UNiagaraStackFunctionInput& Input);
 void CollectStackSelection(const TSharedRef<SWidget>& Widget, UNiagaraSystem* System, TArray<TWeakObjectPtr<UNiagaraStackFunctionInput>>& Out);
 void FindDetailStacks(const TSharedRef<SWidget>& Widget, UNiagaraSystem* System, TArray<TWeakObjectPtr<UNiagaraStackFunctionInput>>& Out);
 void CollectWindowSelection(const TSharedRef<SWindow>& Window, UNiagaraSystem* System, TArray<TWeakObjectPtr<UNiagaraStackFunctionInput>>& Out);
 void Register();
 void Unregister();
}
