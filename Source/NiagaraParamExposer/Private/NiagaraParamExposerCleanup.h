#pragma once
#include "NiagaraParamExposerTypes.h"
class UNiagaraSystem;
namespace NiagaraParamExposerCleanup
{
    FNiagaraParamExposerOperationResult Cleanup(UNiagaraSystem* System, bool bPreviewOnly = false);
}
