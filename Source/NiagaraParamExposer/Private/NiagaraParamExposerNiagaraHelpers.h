// Copyright NiagaraParamExposer. All Rights Reserved.

#pragma once

#include "NiagaraTypes.h"
#include "NiagaraCommon.h"
#include "NiagaraParamExposerTypes.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"

class UEdGraphPin;
class UNiagaraGraph;
class UNiagaraNode;
class UNiagaraNodeFunctionCall;
class UNiagaraRendererProperties;
class UNiagaraSystem;
class UMaterialInterface;
struct FNiagaraEmitterHandle;
struct FNiagaraRendererMaterialParameters;

/** Resolved effective value for a stack module input (for expose defaults and inspection). */
struct FNiagaraParamExposerResolvedModuleInputValue
{
	FNiagaraVariable Value;
	ENiagaraParamExposerInputBindingMode BindingMode = ENiagaraParamExposerInputBindingMode::Default;
	FString Warning;
	bool bHasResolvedValue = false;
};

namespace NiagaraParamExposerNiagaraHelpers
{
	FString GetInputLeafName(const FNiagaraVariable& Variable);

	FNiagaraParameterHandle MakeAliasedModuleInputHandle(
		const UNiagaraNodeFunctionCall& FunctionCall,
		const FNiagaraVariable& InputVariable);

	UNiagaraGraph* GetEmitterGraphForUsage(const FNiagaraEmitterHandle& Handle, ENiagaraScriptUsage Usage);

	void GetStackTraversalNodes(
		const FNiagaraEmitterHandle& Handle,
		ENiagaraScriptUsage Usage,
		TArray<UNiagaraNode*>& OutNodes);

	void CollectStackFunctionCalls(
		const FNiagaraEmitterHandle& Handle,
		ENiagaraScriptUsage Usage,
		TArray<UNiagaraNodeFunctionCall*>& OutFunctionCalls);

	void GatherKnownLinkParameters(
		UNiagaraSystem& System,
		const FNiagaraEmitterHandle* EmitterHandle,
		TSet<FNiagaraVariableBase>& OutParameters);

	bool ParseFullParameterName(
		const FString& FullParameterName,
		const FNiagaraTypeDefinition& Type,
		FNiagaraVariableBase& OutParameter);

	FString GetParameterDisplayName(const FNiagaraVariableBase& Parameter);

	FString VariableValueToString(const FNiagaraVariable& Variable);

	bool TryReadLinkedParameterFromOverridePin(
		const UEdGraphPin& OverridePin,
		FString& OutLinkedParameterName,
		ENiagaraParamExposerInputBindingMode& OutBindingMode);

	bool IsOverridePinLinkedToParameter(
		const UEdGraphPin& OverridePin,
		const FNiagaraVariableBase& TargetParameter,
		const FNiagaraTypeDefinition& Type);

	/** Clears override-pin links/literals so SetLinkedParameterValueForFunctionInput can run (no engine check). */
	bool ClearStackFunctionInputOverridePin(
		UNiagaraNodeFunctionCall& FunctionCall,
		UEdGraphPin& OverridePin,
		FString& OutError);

	void CollectEmitterRenderers(const FNiagaraEmitterHandle& Handle, TArray<UNiagaraRendererProperties*>& OutRenderers);

	void CollectRendererMaterials(UNiagaraRendererProperties& Renderer, TArray<UMaterialInterface*>& OutMaterials);

	FNiagaraRendererMaterialParameters* GetRendererMaterialParameters(UNiagaraRendererProperties& Renderer);

	void CollectMaterialScalarParameterNames(UMaterialInterface& Material, TArray<FName>& OutScalarNames);

	bool BindRendererMaterialAttribute(
		UNiagaraRendererProperties& Renderer,
		const UNiagaraEmitter& Emitter,
		FName MaterialParameterName,
		const FNiagaraVariableBase& NiagaraParameter);

	/** Bounded read-only module input list (called script graph UNiagaraNodeInput nodes). */
	void EnumerateModuleInputVariablesReadOnly(
		const UNiagaraNodeFunctionCall& FunctionCall,
		TArray<FNiagaraVariable>& OutInputs);

	/** Case-insensitive leaf-name match against EnumerateModuleInputVariablesReadOnly. */
	bool TryFindModuleInputVariableReadOnly(
		const UNiagaraNodeFunctionCall& FunctionCall,
		const FString& InputName,
		FNiagaraVariable& OutInputVariable);

	/** Walk upstream from the module parameter-map input pin only (never scans the whole graph). */
	UEdGraphPin* FindExistingStackFunctionInputOverridePin(
		const UNiagaraNodeFunctionCall& FunctionCall,
		const FNiagaraVariable& ModuleInputVariable);

	bool TryReadUserParameterValueFromSystem(
		UNiagaraSystem& System,
		const FString& LinkedParameterName,
		const FNiagaraTypeDefinition& Type,
		FNiagaraVariable& OutValue);

	void ResolveModuleInputCurrentValue(
		UNiagaraSystem& System,
		const FNiagaraEmitterHandle& Handle,
		ENiagaraScriptUsage StackUsage,
		UNiagaraNodeFunctionCall& FunctionCall,
		const FNiagaraVariable& ModuleInputVariable,
		const UEdGraphPin* OptionalOverridePin,
		FNiagaraParamExposerResolvedModuleInputValue& OutResolved);
}
