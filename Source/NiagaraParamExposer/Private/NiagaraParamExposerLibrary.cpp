#include "NiagaraParamExposerLibrary.h"
#include "NiagaraParamExposerUserHierarchy.h"
#include "NiagaraSystem.h"
#include "ScopedTransaction.h"
// Copyright NiagaraParamExposer. All Rights Reserved.


#include "NiagaraParamExposerCore.h"
#include "NiagaraSystem.h"

TArray<FNiagaraParamExposerEmitterInfo> UNiagaraParamExposerLibrary::ListEmitters(UNiagaraSystem* NiagaraSystem)
{
	return NiagaraParamExposerCore::ListEmitters(NiagaraSystem);
}

TArray<FNiagaraParamExposerModuleInfo> UNiagaraParamExposerLibrary::ListModules(
	UNiagaraSystem* NiagaraSystem,
	const FString& EmitterName,
	ENiagaraParamExposerScriptUsage ScriptUsage)
{
	return NiagaraParamExposerCore::ListModules(NiagaraSystem, EmitterName, ScriptUsage);
}

TArray<FNiagaraParamExposerModuleInputInfo> UNiagaraParamExposerLibrary::ListModuleInputs(
	UNiagaraSystem* NiagaraSystem,
	const FString& EmitterName,
	const FString& ModuleName,
	ENiagaraParamExposerScriptUsage ScriptUsage)
{
	return NiagaraParamExposerCore::ListModuleInputs(NiagaraSystem, EmitterName, ModuleName, ScriptUsage);
}

TArray<FNiagaraParamExposerRendererMaterialScalarInfo> UNiagaraParamExposerLibrary::ListRendererMaterialScalarParameters(
	UNiagaraSystem* NiagaraSystem,
	const FString& EmitterName)
{
	return NiagaraParamExposerCore::ListRendererMaterialScalarParameters(NiagaraSystem, EmitterName);
}

FNiagaraParamExposerExposeResult UNiagaraParamExposerLibrary::ExposeModuleInput(
	UNiagaraSystem* NiagaraSystem,
	const FString& EmitterName,
	const FString& ModuleName,
	const FString& InputName,
	const FString& CustomUserParameterName,
	ENiagaraParamExposerScriptUsage ScriptUsage,
	bool bAssignToEmitterCategory)
{
	FNiagaraParamExposerExposeRequest Request;
	Request.EmitterName = EmitterName;
	Request.ModuleName = ModuleName;
	Request.InputName = InputName;
	Request.CustomUserParameterName = CustomUserParameterName;
	Request.ScriptUsage = ScriptUsage;
	Request.bAssignToEmitterCategory = bAssignToEmitterCategory;
	return NiagaraParamExposerCore::ExposeModuleInput(NiagaraSystem, Request, true);
}

TArray<FNiagaraParamExposerExposeResult> UNiagaraParamExposerLibrary::ExposeModuleInputsBatch(
	UNiagaraSystem* NiagaraSystem,
	const TArray<FNiagaraParamExposerExposeRequest>& Requests)
{
	return NiagaraParamExposerCore::ExposeModuleInputsBatch(NiagaraSystem, Requests);
}

TArray<FNiagaraParamExposerExposeResult> UNiagaraParamExposerLibrary::ExposeCommonEmitterParameters(
	UNiagaraSystem* NiagaraSystem,
	const FString& EmitterName,
	bool bIncludeSpawnRate,
	bool bIncludeLifetime,
	bool bIncludeSpriteSize,
	bool bIncludeColor,
	bool bIncludeVelocity)
{
	return NiagaraParamExposerCore::ExposeCommonEmitterParameters(
		NiagaraSystem,
		EmitterName,
		bIncludeSpawnRate,
		bIncludeLifetime,
		bIncludeSpriteSize,
		bIncludeColor,
		bIncludeVelocity);
}

FNiagaraParamExposerOperationResult UNiagaraParamExposerLibrary::AddSetParameterModule(
	UNiagaraSystem* NiagaraSystem,
	const FString& EmitterName,
	ENiagaraParamExposerScriptUsage ScriptUsage,
	const FString& ParameterName,
	float DefaultValue,
	const FString& LinkToUserParameterName)
{
	return NiagaraParamExposerCore::AddSetParameterModule(
		NiagaraSystem,
		EmitterName,
		ScriptUsage,
		ParameterName,
		DefaultValue,
		LinkToUserParameterName,
		true);
}

FNiagaraParamExposerOperationResult UNiagaraParamExposerLibrary::LinkModuleInputToParameter(
	UNiagaraSystem* NiagaraSystem,
	const FString& EmitterName,
	const FString& ModuleName,
	const FString& InputName,
	const FString& ParameterName,
	ENiagaraParamExposerScriptUsage ScriptUsage)
{
	return NiagaraParamExposerCore::LinkModuleInputToParameter(
		NiagaraSystem,
		EmitterName,
		ModuleName,
		InputName,
		ScriptUsage,
		ParameterName,
		true);
}

FNiagaraParamExposerOperationResult UNiagaraParamExposerLibrary::SetUserParameterFloat(
	UNiagaraSystem* NiagaraSystem,
	const FString& UserParameterShortName,
	float Value)
{
	return NiagaraParamExposerCore::SetUserParameterFloat(NiagaraSystem, UserParameterShortName, Value, true);
}

FNiagaraParamExposerOperationResult UNiagaraParamExposerLibrary::SetUserParameterVector2D(
	UNiagaraSystem* NiagaraSystem,
	const FString& UserParameterShortName,
	FVector2D Value)
{
	return NiagaraParamExposerCore::SetUserParameterVector2D(NiagaraSystem, UserParameterShortName, Value, true);
}

FNiagaraParamExposerOperationResult UNiagaraParamExposerLibrary::SetUserParameterVector(
	UNiagaraSystem* NiagaraSystem,
	const FString& UserParameterShortName,
	FVector Value)
{
	return NiagaraParamExposerCore::SetUserParameterVector(NiagaraSystem, UserParameterShortName, Value, true);
}

FNiagaraParamExposerOperationResult UNiagaraParamExposerLibrary::SetUserParameterColor(
	UNiagaraSystem* NiagaraSystem,
	const FString& UserParameterShortName,
	FLinearColor Value)
{
	return NiagaraParamExposerCore::SetUserParameterColor(NiagaraSystem, UserParameterShortName, Value, true);
}

TArray<FNiagaraParamExposerUserParameterInfo> UNiagaraParamExposerLibrary::GetUserParameters(UNiagaraSystem* NiagaraSystem)
{
	return NiagaraParamExposerCore::GetUserParameters(NiagaraSystem);
}

FNiagaraParamExposerOperationResult UNiagaraParamExposerLibrary::SetUserParameterCategory(
	UNiagaraSystem* NiagaraSystem,
	const FString& CategoryName,
	const TArray<FString>& UserParameterShortNames)
{
	return NiagaraParamExposerCore::SetUserParameterCategory(NiagaraSystem, CategoryName, UserParameterShortNames, true);
}

TArray<FString> UNiagaraParamExposerLibrary::ListUserParameterCategories(UNiagaraSystem* NiagaraSystem)
{
	return NiagaraParamExposerCore::ListUserParameterCategories(NiagaraSystem);
}

FNiagaraParamExposerOperationResult UNiagaraParamExposerLibrary::RenameEmitter(
	UNiagaraSystem* NiagaraSystem,
	const FString& OldName,
	const FString& NewName)
{
	return NiagaraParamExposerCore::RenameEmitter(NiagaraSystem, OldName, NewName, true);
}

FNiagaraParamExposerOperationResult UNiagaraParamExposerLibrary::RenameUserParameter(
	UNiagaraSystem* NiagaraSystem,
	const FString& OldShortName,
	const FString& NewShortName)
{
	return NiagaraParamExposerCore::RenameUserParameter(NiagaraSystem, OldShortName, NewShortName, true);
}

FNiagaraParamExposerOperationResult UNiagaraParamExposerLibrary::CreateCustomUserParameter(
	UNiagaraSystem* NiagaraSystem,
	const FString& ShortName,
	const FString& TypeName,
	const FString& ValueAsString,
	const FString& OptionalCategoryName)
{
	return NiagaraParamExposerCore::CreateCustomUserParameter(
		NiagaraSystem,
		ShortName,
		TypeName,
		ValueAsString,
		OptionalCategoryName,
		true);
}

FNiagaraParamExposerOperationResult UNiagaraParamExposerLibrary::BindRendererMaterialParameter(
	UNiagaraSystem* NiagaraSystem,
	const FString& EmitterName,
	const FString& MaterialParameterName,
	const FString& NiagaraParameterName,
	int32 RendererIndex)
{
	return NiagaraParamExposerCore::BindRendererMaterialParameter(
		NiagaraSystem,
		EmitterName,
		MaterialParameterName,
		NiagaraParameterName,
		RendererIndex,
		true);
}

FNiagaraParamExposerOperationResult UNiagaraParamExposerLibrary::RepairEmitterParameterCategories(UNiagaraSystem* NiagaraSystem)
{
	FNiagaraParamExposerOperationResult Result;
	if (!IsValid(NiagaraSystem)) { Result.Message = TEXT("Select a Niagara system first."); return Result; }
	FScopedTransaction Transaction(NSLOCTEXT("NiagaraParamExposer", "RepairCategories", "Repair Niagara Emitter Categories"));
	NiagaraSystem->Modify();
	Result.bSuccess = NiagaraParamExposerUserHierarchy::RepairEmitterCategories(*NiagaraSystem, Result.Message);
	if (Result.bSuccess) Result.Message = TEXT("Grouped existing parameters by emitter prefix. Values and links preserved; save the asset when ready.");
	return Result;
}

TArray<FString> UNiagaraParamExposerLibrary::GetUserParameterCategoryAssignments(UNiagaraSystem* NiagaraSystem)
{
	return IsValid(NiagaraSystem) ? NiagaraParamExposerUserHierarchy::GetCategoryAssignments(*NiagaraSystem) : TArray<FString>();
}
