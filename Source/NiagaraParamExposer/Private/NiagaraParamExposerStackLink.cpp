// Copyright NiagaraParamExposer. All Rights Reserved.

#include "NiagaraParamExposerStackLink.h"

#include "NiagaraParamExposerNiagaraHelpers.h"
#include "NiagaraParamExposerLog.h"
#include "NiagaraSystem.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "NiagaraParameterMapHistory.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

#include "EdGraph/EdGraphPin.h"

namespace NiagaraParamExposerStackLink
{
	namespace
	{
		bool PrepareOverridePinForLink(
			UNiagaraNodeFunctionCall& FunctionCall,
			const FNiagaraVariable& ModuleInputVariable,
			const FNiagaraVariableBase& LinkedParameter,
			const FGuid& InputScriptVariableId,
			UEdGraphPin*& OutOverridePin,
			FString& OutError)
		{
			const FNiagaraParameterHandle InputHandle =
				NiagaraParamExposerNiagaraHelpers::MakeAliasedModuleInputHandle(FunctionCall, ModuleInputVariable);

			UEdGraphPin* ExistingOverride =
				NiagaraParamExposerNiagaraHelpers::FindExistingStackFunctionInputOverridePin(
					FunctionCall,
					ModuleInputVariable);
			if (ExistingOverride
				&& NiagaraParamExposerNiagaraHelpers::IsOverridePinLinkedToParameter(
					*ExistingOverride,
					LinkedParameter,
					ModuleInputVariable.GetType()))
			{
				UE_LOG(
					LogNiagaraParamExposer,
					Display,
					TEXT("PrepareOverridePinForLink: already linked to %s on module=%s"),
					*NiagaraParamExposerNiagaraHelpers::GetParameterDisplayName(LinkedParameter),
					*FunctionCall.GetFunctionName());
				OutOverridePin = ExistingOverride;
				return true;
			}

			UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
				FunctionCall,
				InputHandle,
				ModuleInputVariable.GetType(),
				InputScriptVariableId,
				FGuid());

			if (NiagaraParamExposerNiagaraHelpers::IsOverridePinLinkedToParameter(
					OverridePin,
					LinkedParameter,
					ModuleInputVariable.GetType()))
			{
				OutOverridePin = &OverridePin;
				return true;
			}

			if (OverridePin.LinkedTo.Num() > 0
				|| !OverridePin.DefaultValue.IsEmpty()
				|| !OverridePin.GetDefaultAsString().IsEmpty())
			{
				if (!NiagaraParamExposerNiagaraHelpers::ClearStackFunctionInputOverridePin(
						FunctionCall,
						OverridePin,
						OutError))
				{
					return false;
				}
			}

			if (OverridePin.LinkedTo.Num() > 0)
			{
				OutError = TEXT(
					"Module input override pin still has graph links after reset; cannot set a new linked parameter.");
				return false;
			}

			OutOverridePin = &OverridePin;
			return true;
		}
	}

	bool LinkModuleInputToParameter(
		UNiagaraNodeFunctionCall& FunctionCall,
		UNiagaraSystem& System,
		const FNiagaraEmitterHandle* EmitterHandle,
		const FNiagaraVariable& ModuleInputVariable,
		const FString& FullParameterName,
		const FGuid& InputScriptVariableId,
		FString& OutError)
	{
		FNiagaraVariableBase LinkedParameter;
		if (!NiagaraParamExposerNiagaraHelpers::ParseFullParameterName(
				FullParameterName,
				ModuleInputVariable.GetType(),
				LinkedParameter))
		{
			OutError = FString::Printf(TEXT("Could not parse parameter name '%s' (expected Namespace.Name)."), *FullParameterName);
			return false;
		}

		UEdGraphPin* OverridePin = nullptr;
		if (!PrepareOverridePinForLink(
				FunctionCall,
				ModuleInputVariable,
				LinkedParameter,
				InputScriptVariableId,
				OverridePin,
				OutError))
		{
			return false;
		}

		if (NiagaraParamExposerNiagaraHelpers::IsOverridePinLinkedToParameter(
				*OverridePin,
				LinkedParameter,
				ModuleInputVariable.GetType()))
		{
			return true;
		}

		if (OverridePin->LinkedTo.Num() > 0)
		{
			OutError = TEXT("Refusing to link: override pin is not empty after reset.");
			return false;
		}

		TSet<FNiagaraVariableBase> KnownParameters;
		NiagaraParamExposerNiagaraHelpers::GatherKnownLinkParameters(System, EmitterHandle, KnownParameters);
		KnownParameters.Add(LinkedParameter);

		FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(
			*OverridePin,
			LinkedParameter,
			KnownParameters,
			ENiagaraDefaultMode::FailIfPreviouslyNotSet);

		UE_LOG(
			LogNiagaraParamExposer,
			Display,
			TEXT("LinkModuleInputToParameter: linked %s on module=%s"),
			*FullParameterName,
			*FunctionCall.GetFunctionName());
		return true;
	}

	bool LinkModuleInputToUserParameter(
		UNiagaraNodeFunctionCall& FunctionCall,
		UNiagaraSystem& System,
		const FNiagaraEmitterHandle* EmitterHandle,
		const FNiagaraVariable& ModuleInputVariable,
		const FString& UserParameterShortName,
		UNiagaraScriptVariable* UserScriptVariable,
		FString& OutError)
	{
		FNiagaraVariableBase LinkedParameter(ModuleInputVariable.GetType(), FName(*UserParameterShortName));
		FNiagaraUserRedirectionParameterStore::MakeUserVariable(LinkedParameter);

		const FGuid ScriptVariableId =
			UserScriptVariable != nullptr ? UNiagaraScriptVariable::GenerateStableGuid(UserScriptVariable) : FGuid();

		const FString FullName = FString::Printf(TEXT("User.%s"), *UserParameterShortName);
		return LinkModuleInputToParameter(
			FunctionCall,
			System,
			EmitterHandle,
			ModuleInputVariable,
			FullName,
			ScriptVariableId,
			OutError);
	}

}
