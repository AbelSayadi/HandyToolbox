// Copyright NiagaraParamExposer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UNiagaraSystem;

void NiagaraParamExposer_RegisterEditorTab();
void NiagaraParamExposer_UnregisterEditorTab();
void NiagaraParamExposer_OpenEditorTab(UNiagaraSystem* NiagaraSystem);
