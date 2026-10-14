// Copyright NiagaraParamExposer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NiagaraParamExposerTypes.h"
#include "NiagaraParamExposerLibrary.generated.h"

class UNiagaraSystem;

/**
 * Editor-only Niagara helper library exposed to Blueprint and Python (unreal.NiagaraParamExposerLibrary).
 * Mutating calls participate in editor transactions when invoked from the UI; Python callers may wrap
 * unreal.EditorLoadingAndSavingUtils or scoped transactions externally if needed.
 */
UCLASS()
class NIAGARAPARAMEXPOSER_API UNiagaraParamExposerLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Inspection")
	static TArray<FNiagaraParamExposerEmitterInfo> ListEmitters(UNiagaraSystem* NiagaraSystem);

	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Inspection")
	static TArray<FNiagaraParamExposerModuleInfo> ListModules(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		ENiagaraParamExposerScriptUsage ScriptUsage = ENiagaraParamExposerScriptUsage::Any);

	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Inspection")
	static TArray<FNiagaraParamExposerModuleInputInfo> ListModuleInputs(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		const FString& ModuleName,
		ENiagaraParamExposerScriptUsage ScriptUsage = ENiagaraParamExposerScriptUsage::Any);

	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Inspection")
	static TArray<FNiagaraParamExposerRendererMaterialScalarInfo> ListRendererMaterialScalarParameters(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName);

	/** Expose a single module input as a User parameter and link the input to it. */
	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Expose")
	static FNiagaraParamExposerExposeResult ExposeModuleInput(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		const FString& ModuleName,
		const FString& InputName,
		const FString& CustomUserParameterName = TEXT(""),
		ENiagaraParamExposerScriptUsage ScriptUsage = ENiagaraParamExposerScriptUsage::Any,
		bool bAssignToEmitterCategory = true);

	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Expose")
	static TArray<FNiagaraParamExposerExposeResult> ExposeModuleInputsBatch(
		UNiagaraSystem* NiagaraSystem,
		const TArray<FNiagaraParamExposerExposeRequest>& Requests);

	/**
	 * Best-effort convenience: tries common inputs on typical modules (Spawn Rate, Initialize Particle, etc.).
	 * Missing modules/inputs are reported as failures in the result array but do not stop other items.
	 */
	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Expose")
	static TArray<FNiagaraParamExposerExposeResult> ExposeCommonEmitterParameters(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		bool bIncludeSpawnRate = true,
		bool bIncludeLifetime = true,
		bool bIncludeSpriteSize = true,
		bool bIncludeColor = true,
		bool bIncludeVelocity = true);

	/** Adds a Set Parameters module that writes a float attribute (e.g. Emitter.HueShift). Pass LinkToUserParameterName = "auto" to create/link User.<Emitter>_<Leaf>. */
	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Stack")
	static FNiagaraParamExposerOperationResult AddSetParameterModule(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		ENiagaraParamExposerScriptUsage ScriptUsage,
		const FString& ParameterName,
		float DefaultValue = 0.f,
		const FString& LinkToUserParameterName = TEXT(""));

	/** Link a module input to any namespaced parameter (Emitter.*, Particles.*, User.*). */
	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Stack")
	static FNiagaraParamExposerOperationResult LinkModuleInputToParameter(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		const FString& ModuleName,
		const FString& InputName,
		const FString& ParameterName,
		ENiagaraParamExposerScriptUsage ScriptUsage = ENiagaraParamExposerScriptUsage::Any);

	/** Binds a material scalar to a Niagara parameter via the renderer Material Parameter Bindings (AttributeBindings). */
	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Materials")
	static FNiagaraParamExposerOperationResult BindRendererMaterialParameter(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		const FString& MaterialParameterName,
		const FString& NiagaraParameterName,
		int32 RendererIndex = -1);

	/** Sets an exposed user float parameter value (short name without User. prefix). */
	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Expose")
	static FNiagaraParamExposerOperationResult SetUserParameterFloat(
		UNiagaraSystem* NiagaraSystem,
		const FString& UserParameterShortName,
		float Value);

	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Expose")
	static FNiagaraParamExposerOperationResult SetUserParameterVector2D(
		UNiagaraSystem* NiagaraSystem,
		const FString& UserParameterShortName,
		FVector2D Value);

	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Expose")
	static FNiagaraParamExposerOperationResult SetUserParameterVector(
		UNiagaraSystem* NiagaraSystem,
		const FString& UserParameterShortName,
		FVector Value);

	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Expose")
	static FNiagaraParamExposerOperationResult SetUserParameterColor(
		UNiagaraSystem* NiagaraSystem,
		const FString& UserParameterShortName,
		FLinearColor Value);

	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Inspection")
	static TArray<FNiagaraParamExposerUserParameterInfo> GetUserParameters(UNiagaraSystem* NiagaraSystem);

	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Expose")
	static FNiagaraParamExposerOperationResult SetUserParameterCategory(
		UNiagaraSystem* NiagaraSystem,
		const FString& CategoryName,
		const TArray<FString>& UserParameterShortNames);

	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Inspection")
	static TArray<FString> ListUserParameterCategories(UNiagaraSystem* NiagaraSystem);

	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Stack")
	static FNiagaraParamExposerOperationResult RenameEmitter(
		UNiagaraSystem* NiagaraSystem,
		const FString& OldName,
		const FString& NewName);

	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Expose")
	static FNiagaraParamExposerOperationResult RenameUserParameter(
		UNiagaraSystem* NiagaraSystem,
		const FString& OldShortName,
		const FString& NewShortName);

	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Expose")
	static FNiagaraParamExposerOperationResult CreateCustomUserParameter(
		UNiagaraSystem* NiagaraSystem,
		const FString& ShortName,
		const FString& TypeName,
		const FString& ValueAsString,
		const FString& OptionalCategoryName = TEXT(""));

	/** Group existing parameters by their <Emitter>_<Input> prefix without changing values or links. */
	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Expose")
	static FNiagaraParamExposerOperationResult RepairEmitterParameterCategories(UNiagaraSystem* NiagaraSystem);

	/** Read-only rows: short parameter name | category | identity/ownership validity. */
	UFUNCTION(BlueprintCallable, Category = "NiagaraParamExposer|Inspection")
	static TArray<FString> GetUserParameterCategoryAssignments(UNiagaraSystem* NiagaraSystem);
};
