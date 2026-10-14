// Copyright NiagaraParamExposer. All Rights Reserved.

#pragma once

#include "NiagaraParamExposerTypes.h"

class UNiagaraSystem;
class UNiagaraNodeFunctionCall;

/**
 * Core (non-UObject) Niagara editor operations. Kept separate from UNiagaraParamExposerLibrary so UI and
 * future automation can share logic without pulling Blueprint glue into Slate code.
 */
namespace NiagaraParamExposerCore
{
	bool IsValidSystem(UNiagaraSystem* NiagaraSystem, FString& OutError);

	TArray<FNiagaraParamExposerEmitterInfo> ListEmitters(UNiagaraSystem* NiagaraSystem);
	TArray<FNiagaraParamExposerModuleInfo> ListModules(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		ENiagaraParamExposerScriptUsage ScriptUsage);
	TArray<FNiagaraParamExposerModuleInputInfo> ListModuleInputs(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		const FString& ModuleName,
		ENiagaraParamExposerScriptUsage ScriptUsage);

	/** Resolves the stack module node for UI filtering (read-only). */
	UNiagaraNodeFunctionCall* FindModuleFunctionCall(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		const FString& ModuleName,
		ENiagaraParamExposerScriptUsage ScriptUsage,
		ENiagaraParamExposerScriptUsage& OutResolvedScriptUsage);

	FNiagaraParamExposerExposeResult ExposeModuleInput(
		UNiagaraSystem* NiagaraSystem,
		const FNiagaraParamExposerExposeRequest& Request,
		bool bUseTransaction);

	TArray<FNiagaraParamExposerExposeResult> ExposeModuleInputsBatch(
		UNiagaraSystem* NiagaraSystem,
		const TArray<FNiagaraParamExposerExposeRequest>& Requests);

	TArray<FNiagaraParamExposerExposeResult> ExposeCommonEmitterParameters(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		bool bIncludeSpawnRate,
		bool bIncludeLifetime,
		bool bIncludeSpriteSize,
		bool bIncludeColor,
		bool bIncludeVelocity);

 FNiagaraParamExposerOperationResult AddTypedSetParameterModule(
  UNiagaraSystem* NiagaraSystem, const FString& EmitterName,
  ENiagaraParamExposerScriptUsage ScriptUsage, const FString& ParameterName,
  const FString& TypeName, const FString& ValueAsString,
  const FString& LinkToUserParameterName, bool bUseTransaction, bool bAssignToEmitterCategory = true);

	FNiagaraParamExposerOperationResult AddSetParameterModule(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		ENiagaraParamExposerScriptUsage ScriptUsage,
		const FString& ParameterName,
		float DefaultValue,
		const FString& LinkToUserParameterName,
		bool bUseTransaction);

	FNiagaraParamExposerOperationResult LinkModuleInputToParameter(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		const FString& ModuleName,
		const FString& InputName,
		ENiagaraParamExposerScriptUsage ScriptUsage,
		const FString& ParameterName,
		bool bUseTransaction);

	TArray<FNiagaraParamExposerRendererMaterialScalarInfo> ListRendererMaterialScalarParameters(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName);

	FNiagaraParamExposerOperationResult BindRendererMaterialParameter(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		const FString& MaterialParameterName,
		const FString& NiagaraParameterName,
		int32 RendererIndex,
		bool bUseTransaction);

	FNiagaraParamExposerOperationResult SetUserParameterFloat(
		UNiagaraSystem* NiagaraSystem,
		const FString& UserParameterShortName,
		float Value,
		bool bUseTransaction);

	FNiagaraParamExposerOperationResult SetUserParameterVector2D(
		UNiagaraSystem* NiagaraSystem,
		const FString& UserParameterShortName,
		const FVector2D& Value,
		bool bUseTransaction);

	FNiagaraParamExposerOperationResult SetUserParameterVector(
		UNiagaraSystem* NiagaraSystem,
		const FString& UserParameterShortName,
		const FVector& Value,
		bool bUseTransaction);

	FNiagaraParamExposerOperationResult SetUserParameterColor(
		UNiagaraSystem* NiagaraSystem,
		const FString& UserParameterShortName,
		const FLinearColor& Value,
		bool bUseTransaction);

	TArray<FNiagaraParamExposerUserParameterInfo> GetUserParameters(UNiagaraSystem* NiagaraSystem);

	FNiagaraParamExposerOperationResult SetUserParameterCategory(
		UNiagaraSystem* NiagaraSystem,
		const FString& CategoryName,
		const TArray<FString>& UserParameterShortNames,
		bool bUseTransaction);

	TArray<FString> ListUserParameterCategories(UNiagaraSystem* NiagaraSystem);

	FNiagaraParamExposerOperationResult RenameEmitter(
		UNiagaraSystem* NiagaraSystem,
		const FString& OldName,
		const FString& NewName,
		bool bUseTransaction);

	FNiagaraParamExposerOperationResult RenameUserParameter(
		UNiagaraSystem* NiagaraSystem,
		const FString& OldShortName,
		const FString& NewShortName,
		bool bUseTransaction);

	FNiagaraParamExposerOperationResult CreateCustomUserParameter(
		UNiagaraSystem* NiagaraSystem,
		const FString& ShortName,
		const FString& TypeName,
		const FString& ValueAsString,
		const FString& OptionalCategoryName,
		bool bUseTransaction);

	/** Recompile Niagara system and mark its package dirty (never deletes assets). */
	void FinalizeSystemEdit(UNiagaraSystem* NiagaraSystem);
}
