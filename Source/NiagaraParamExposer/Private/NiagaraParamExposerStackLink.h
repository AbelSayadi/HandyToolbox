// Copyright NiagaraParamExposer. All Rights Reserved.

#pragma once

#include "NiagaraTypes.h"

class UNiagaraNodeFunctionCall;
class UNiagaraScriptVariable;
class UNiagaraSystem;
struct FNiagaraEmitterHandle;

namespace NiagaraParamExposerStackLink
{
	bool LinkModuleInputToUserParameter(
		UNiagaraNodeFunctionCall& FunctionCall,
		UNiagaraSystem& System,
		const FNiagaraEmitterHandle* EmitterHandle,
		const FNiagaraVariable& ModuleInputVariable,
		const FString& UserParameterShortName,
		UNiagaraScriptVariable* UserScriptVariable,
		FString& OutError);

	bool LinkModuleInputToParameter(
		UNiagaraNodeFunctionCall& FunctionCall,
		UNiagaraSystem& System,
		const FNiagaraEmitterHandle* EmitterHandle,
		const FNiagaraVariable& ModuleInputVariable,
		const FString& FullParameterName,
		const FGuid& InputScriptVariableId,
		FString& OutError);
}
