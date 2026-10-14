// Copyright NiagaraParamExposer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraParamExposerTypes.generated.h"

/** Niagara stack script stage for a module (matches common Niagara editor stack names). */
UENUM(BlueprintType)
enum class ENiagaraParamExposerScriptUsage : uint8
{
	EmitterSpawn UMETA(DisplayName = "Emitter Spawn"),
	EmitterUpdate UMETA(DisplayName = "Emitter Update"),
	ParticleSpawn UMETA(DisplayName = "Particle Spawn"),
	ParticleUpdate UMETA(DisplayName = "Particle Update"),
	SystemSpawn UMETA(DisplayName = "System Spawn"),
	SystemUpdate UMETA(DisplayName = "System Update"),
	Any UMETA(DisplayName = "Search All Stages"),
};

/** How a module input is currently driven (inspection). */
UENUM(BlueprintType)
enum class ENiagaraParamExposerInputBindingMode : uint8
{
	Default UMETA(DisplayName = "Default"),
	Local UMETA(DisplayName = "Local"),
	Linked UMETA(DisplayName = "Linked"),
	Dynamic UMETA(DisplayName = "Dynamic"),
	Data UMETA(DisplayName = "Data"),
	Unknown UMETA(DisplayName = "Unknown"),
};

USTRUCT(BlueprintType)
struct FNiagaraParamExposerExposeRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString EmitterName;

	/** Module display name or internal script name (e.g. "Spawn Rate", "SpawnRate", "Initialize Particle"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString ModuleName;

	/** Module input pin name (e.g. SpawnRate, Lifetime). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString InputName;

	/** If empty, uses <EmitterName>_<InputName>. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString CustomUserParameterName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	ENiagaraParamExposerScriptUsage ScriptUsage = ENiagaraParamExposerScriptUsage::Any;

	/** When true (default), places the new User parameter under a hierarchy folder named after EmitterName. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	bool bAssignToEmitterCategory = true;
};

USTRUCT(BlueprintType)
struct FNiagaraParamExposerExposeResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	bool bSuccess = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString Message;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString EmitterName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString ModuleName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString InputName;

	/** Short name without User. prefix (e.g. SpiralFire_Spawnrate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString UserParameterName;

	/** Full Niagara parameter name (e.g. User.SpiralFire_Spawnrate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString LinkedParameterName;
};

USTRUCT(BlueprintType)
struct FNiagaraParamExposerUserParameterInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString ShortName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString TypeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString ValueAsString;
};

USTRUCT(BlueprintType)
struct FNiagaraParamExposerOperationResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	bool bSuccess = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString Message;
};

USTRUCT(BlueprintType)
struct FNiagaraParamExposerEmitterInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString EmitterName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString EmitterHandleId;
};

USTRUCT(BlueprintType)
struct FNiagaraParamExposerModuleInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString ModuleName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString ModuleDisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	ENiagaraParamExposerScriptUsage ScriptUsage = ENiagaraParamExposerScriptUsage::ParticleSpawn;
};

USTRUCT(BlueprintType)
struct FNiagaraParamExposerModuleInputInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString InputName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString TypeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString ValueAsString;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	ENiagaraParamExposerInputBindingMode BindingMode = ENiagaraParamExposerInputBindingMode::Unknown;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString LinkedParameterName;
};

/** Scalar material parameter exposed by a renderer's assigned material(s). */
USTRUCT(BlueprintType)
struct FNiagaraParamExposerRendererMaterialScalarInfo
{
	GENERATED_BODY()

	/** Index into the emitter handle's renderer list (FVersionedNiagaraEmitterData::GetRenderers). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	int32 RendererIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString RendererClassName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString MaterialPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NiagaraParamExposer")
	FString ScalarParameterName;
};
