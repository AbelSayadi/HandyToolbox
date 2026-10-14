#include "NiagaraParamExposerTypedValue.h"
// Copyright NiagaraParamExposer. All Rights Reserved.

#include "NiagaraParamExposerCore.h"
#include "NiagaraParamExposerStackLink.h"
#include "NiagaraParamExposerNiagaraHelpers.h"
#include "NiagaraParamExposerUserHierarchy.h"
#include "NiagaraParamExposerNaming.h"
#include "NiagaraParamExposerInputFilter.h"
#include "NiagaraParamExposerLog.h"

#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraEmitter.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSystemEditorData.h"
#include "NiagaraParameterMapHistory.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

#include "EdGraph/EdGraphPin.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"
#include "Misc/Parse.h"

namespace NiagaraParamExposerCore
{
	FNiagaraParamExposerOperationResult MakeOpFailure(const FString& Message);

	namespace
	{
		FString NormalizeToken(FString In)
		{
			In.ReplaceInline(TEXT(" "), TEXT(""));
			In.ReplaceInline(TEXT("_"), TEXT(""));
			return In.ToLower();
		}

		bool StringsMatchLoose(const FString& A, const FString& B)
		{
			if (A.Equals(B, ESearchCase::IgnoreCase))
			{
				return true;
			}
			return NormalizeToken(A) == NormalizeToken(B);
		}

		ENiagaraScriptUsage ToNiagaraScriptUsage(ENiagaraParamExposerScriptUsage Usage)
		{
			switch (Usage)
			{
			case ENiagaraParamExposerScriptUsage::EmitterSpawn:
				return ENiagaraScriptUsage::EmitterSpawnScript;
			case ENiagaraParamExposerScriptUsage::EmitterUpdate:
				return ENiagaraScriptUsage::EmitterUpdateScript;
			case ENiagaraParamExposerScriptUsage::ParticleSpawn:
				return ENiagaraScriptUsage::ParticleSpawnScript;
			case ENiagaraParamExposerScriptUsage::ParticleUpdate:
				return ENiagaraScriptUsage::ParticleUpdateScript;
			case ENiagaraParamExposerScriptUsage::SystemSpawn:
				return ENiagaraScriptUsage::SystemSpawnScript;
			case ENiagaraParamExposerScriptUsage::SystemUpdate:
				return ENiagaraScriptUsage::SystemUpdateScript;
			default:
				return ENiagaraScriptUsage::ParticleSpawnScript;
			}
		}

		ENiagaraParamExposerScriptUsage FromNiagaraScriptUsage(ENiagaraScriptUsage Usage)
		{
			switch (Usage)
			{
			case ENiagaraScriptUsage::EmitterSpawnScript:
				return ENiagaraParamExposerScriptUsage::EmitterSpawn;
			case ENiagaraScriptUsage::EmitterUpdateScript:
				return ENiagaraParamExposerScriptUsage::EmitterUpdate;
			case ENiagaraScriptUsage::ParticleSpawnScript:
				return ENiagaraParamExposerScriptUsage::ParticleSpawn;
			case ENiagaraScriptUsage::ParticleUpdateScript:
				return ENiagaraParamExposerScriptUsage::ParticleUpdate;
			case ENiagaraScriptUsage::SystemSpawnScript:
				return ENiagaraParamExposerScriptUsage::SystemSpawn;
			case ENiagaraScriptUsage::SystemUpdateScript:
				return ENiagaraParamExposerScriptUsage::SystemUpdate;
			default:
				return ENiagaraParamExposerScriptUsage::Any;
			}
		}

		TArray<ENiagaraScriptUsage> GetUsagesToSearch(ENiagaraParamExposerScriptUsage Usage)
		{
			if (Usage != ENiagaraParamExposerScriptUsage::Any)
			{
				return {ToNiagaraScriptUsage(Usage)};
			}

			return {
				ENiagaraScriptUsage::EmitterSpawnScript,
				ENiagaraScriptUsage::EmitterUpdateScript,
				ENiagaraScriptUsage::ParticleSpawnScript,
				ENiagaraScriptUsage::ParticleUpdateScript,
				ENiagaraScriptUsage::SystemSpawnScript,
				ENiagaraScriptUsage::SystemUpdateScript,
			};
		}

		const FNiagaraEmitterHandle* FindEmitterHandle(const UNiagaraSystem* System, const FString& EmitterName)
		{
			if (!System)
			{
				return nullptr;
			}

			for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
			{
				if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
				{
					return &Handle;
				}
			}
			return nullptr;
		}

		FString GetModuleLabel(const UNiagaraNodeFunctionCall& FunctionCall)
		{
			const FString ListTitle = FunctionCall.GetNodeTitle(ENodeTitleType::ListView).ToString();
			if (!ListTitle.IsEmpty())
			{
				return ListTitle;
			}
			if (!FunctionCall.GetFunctionName().IsEmpty())
			{
				return FunctionCall.GetFunctionName();
			}
			return FunctionCall.GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		}

		bool ModuleMatches(const UNiagaraNodeFunctionCall& FunctionCall, const FString& ModuleName)
		{
			if (StringsMatchLoose(GetModuleLabel(FunctionCall), ModuleName))
			{
				return true;
			}

			const FString FullTitle = FunctionCall.GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			if (StringsMatchLoose(FullTitle, ModuleName))
			{
				return true;
			}

			if (FullTitle.Contains(ModuleName, ESearchCase::IgnoreCase) || ModuleName.Contains(FullTitle, ESearchCase::IgnoreCase))
			{
				return true;
			}

			if (const UNiagaraScript* CalledScript = FunctionCall.FunctionScript)
			{
				if (StringsMatchLoose(CalledScript->GetName(), ModuleName))
				{
					return true;
				}
				if (CalledScript->GetOutermost())
				{
					const FString AssetName = CalledScript->GetOutermost()->GetName();
					if (StringsMatchLoose(AssetName, ModuleName))
					{
						return true;
					}
				}
			}
			return false;
		}

		UNiagaraNodeFunctionCall* FindModuleNode(
			UNiagaraSystem* System,
			const FString& EmitterName,
			const FString& ModuleName,
			ENiagaraParamExposerScriptUsage ScriptUsage,
			ENiagaraScriptUsage& OutResolvedUsage)
		{
			const FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
			if (!Handle)
			{
				return nullptr;
			}

			for (ENiagaraScriptUsage Usage : GetUsagesToSearch(ScriptUsage))
			{
				TArray<UNiagaraNodeFunctionCall*> FunctionCalls;
				NiagaraParamExposerNiagaraHelpers::CollectStackFunctionCalls(*Handle, Usage, FunctionCalls);
				for (UNiagaraNodeFunctionCall* FunctionCall : FunctionCalls)
				{
					if (FunctionCall && ModuleMatches(*FunctionCall, ModuleName))
					{
						OutResolvedUsage = Usage;
						return FunctionCall;
					}
				}
			}
			return nullptr;
		}

		FString GetParameterLeafName(const FString& FullParameterName)
		{
			int32 DotIndex = INDEX_NONE;
			if (FullParameterName.FindChar(TEXT('.'), DotIndex))
			{
				return FullParameterName.Mid(DotIndex + 1);
			}
			return FullParameterName;
		}

		bool TryGetStackInputVariable(
			const UNiagaraNodeFunctionCall& FunctionCall,
			const FString& InputName,
			FNiagaraVariable& OutInputVariable)
		{
			if (NiagaraParamExposerNiagaraHelpers::TryFindModuleInputVariableReadOnly(FunctionCall, InputName, OutInputVariable))
			{
				return true;
			}

			TArray<FNiagaraVariable> Inputs;
			NiagaraParamExposerNiagaraHelpers::EnumerateModuleInputVariablesReadOnly(FunctionCall, Inputs);
			for (const FNiagaraVariable& Candidate : Inputs)
			{
				const FString LeafName = NiagaraParamExposerNiagaraHelpers::GetInputLeafName(Candidate);
				if (StringsMatchLoose(LeafName, InputName) || StringsMatchLoose(Candidate.GetName().ToString(), InputName))
				{
					OutInputVariable = Candidate;
					return true;
				}
			}
			return false;
		}

		FString MakeDefaultUserParameterShortName(const FString& EmitterName, const FString& InputName)
		{
			return NiagaraParamExposerNaming::MakeAutoUserParameterName(EmitterName, InputName);
		}

		FNiagaraVariable MakeUserParameterVariable(
			const FNiagaraVariable& ModuleInputVariable,
			const FNiagaraVariable& ResolvedValue,
			const FString& ShortUserName)
		{
			const FNiagaraTypeDefinition& Type = ModuleInputVariable.GetType();
			FNiagaraVariable UserVar(Type, FName(*ShortUserName));
			UserVar.AllocateData();

			if (ResolvedValue.IsDataAllocated() && ResolvedValue.GetType() == Type)
			{
				FMemory::Memcpy(UserVar.GetData(), ResolvedValue.GetData(), UserVar.GetSizeInBytes());
			}
			else if (ModuleInputVariable.IsDataAllocated() && ModuleInputVariable.GetType() == Type)
			{
				FMemory::Memcpy(UserVar.GetData(), ModuleInputVariable.GetData(), UserVar.GetSizeInBytes());
			}

			return UserVar;
		}

		void WriteUserParameterDefaults(UNiagaraSystem* System, const FNiagaraVariable& UserVariable)
		{
			if (!System || !UserVariable.IsValid() || !UserVariable.IsDataAllocated())
			{
				return;
			}

			FNiagaraUserRedirectionParameterStore& Exposed = System->GetExposedParameters();
			FNiagaraVariable StoreLookup(UserVariable);
			if (Exposed.IndexOf(StoreLookup) != INDEX_NONE)
			{
				Exposed.SetParameterData(UserVariable.GetData(), StoreLookup, true);
			}

			UNiagaraSystemEditorData* SystemEditorData = Cast<UNiagaraSystemEditorData>(System->GetEditorData());
			if (!SystemEditorData)
			{
				return;
			}

			FNiagaraVariable UserLookup(UserVariable);
			FNiagaraUserRedirectionParameterStore::MakeUserVariable(UserLookup);
			if (UNiagaraScriptVariable* ScriptVar = SystemEditorData->FindOrAddUserScriptVariable(UserLookup, *System))
			{
				ScriptVar->Modify();
				if (ScriptVar->Variable.GetType() != UserVariable.GetType())
				{
					ScriptVar->Variable = FNiagaraVariable(UserVariable.GetType(), UserVariable.GetName());
				}
				if (!ScriptVar->Variable.IsDataAllocated())
				{
					ScriptVar->Variable.AllocateData();
				}
				FMemory::Memcpy(
					ScriptVar->Variable.GetData(),
					UserVariable.GetData(),
					UserVariable.GetSizeInBytes());
			}
		}

		UNiagaraScriptVariable* EnsureUserParameterOnSystem(
			UNiagaraSystem* System,
			const FNiagaraVariable& UserVariable,
			const FString& CategoryName,
			FString& OutError)
		{
			FNiagaraUserRedirectionParameterStore& Exposed = System->GetExposedParameters();

			if (Exposed.IndexOf(UserVariable) != INDEX_NONE)
			{
				if (UserVariable.IsDataAllocated())
				{
					Exposed.SetParameterData(UserVariable.GetData(), UserVariable, true);
				}
			}
			else
			{
				FNiagaraVariable ParameterToAdd = UserVariable;
				if (!Exposed.AddParameter(ParameterToAdd, true, true))
				{
					OutError = TEXT("FNiagaraUserRedirectionParameterStore::AddParameter failed for exposed user parameter.");
					return nullptr;
				}
			}

			UNiagaraSystemEditorData* SystemEditorData = Cast<UNiagaraSystemEditorData>(System->GetEditorData());
			if (!SystemEditorData)
			{
				OutError = TEXT("UNiagaraSystemEditorData is missing on the Niagara system.");
				return nullptr;
			}

			FNiagaraVariable UserLookup = UserVariable;
			FNiagaraUserRedirectionParameterStore::MakeUserVariable(UserLookup);

			UNiagaraScriptVariable* ScriptVar = SystemEditorData->FindOrAddUserScriptVariable(UserLookup, *System);
			if (!ScriptVar)
			{
				OutError = TEXT("FindOrAddUserScriptVariable failed for new user parameter.");
				return nullptr;
			}

			ScriptVar->Modify();
			FNiagaraVariable ScriptCopy(UserVariable.GetType(), UserVariable.GetName());
			if (UserVariable.IsDataAllocated())
			{
				if (!ScriptCopy.IsDataAllocated())
				{
					ScriptCopy.AllocateData();
				}
				FMemory::Memcpy(ScriptCopy.GetData(), UserVariable.GetData(), UserVariable.GetSizeInBytes());
			}
			ScriptVar->Variable = ScriptCopy;

			SystemEditorData->SyncUserScriptVariables(System);

			if (UserVariable.IsDataAllocated())
			{
				FNiagaraVariable ExposedLookup(UserVariable);
				if (Exposed.IndexOf(ExposedLookup) != INDEX_NONE)
				{
					Exposed.SetParameterData(UserVariable.GetData(), ExposedLookup, true);
				}
			}
			WriteUserParameterDefaults(System, UserVariable);

			(void)CategoryName;
			return ScriptVar;
		}

		FNiagaraParamExposerExposeResult MakeFailure(
			const FNiagaraParamExposerExposeRequest& Request,
			const FString& Message)
		{
			FNiagaraParamExposerExposeResult Result;
			Result.bSuccess = false;
			Result.Message = Message;
			Result.EmitterName = Request.EmitterName;
			Result.ModuleName = Request.ModuleName;
			Result.InputName = Request.InputName;
			return Result;
		}
	}

	bool IsValidSystem(UNiagaraSystem* NiagaraSystem, FString& OutError)
	{
		if (!NiagaraSystem)
		{
			OutError = TEXT("NiagaraSystem is null.");
			return false;
		}
		if (!NiagaraSystem->IsValid())
		{
			OutError = TEXT("Niagara system is not valid.");
			return false;
		}
		return true;
	}

	TArray<FNiagaraParamExposerEmitterInfo> ListEmitters(UNiagaraSystem* NiagaraSystem)
	{
		TArray<FNiagaraParamExposerEmitterInfo> Out;
		FString Error;
		if (!IsValidSystem(NiagaraSystem, Error))
		{
			return Out;
		}

		for (const FNiagaraEmitterHandle& Handle : NiagaraSystem->GetEmitterHandles())
		{
			FNiagaraParamExposerEmitterInfo Info;
			Info.EmitterName = Handle.GetName().ToString();
			Info.EmitterHandleId = Handle.GetId().ToString();
			Out.Add(Info);
		}
		return Out;
	}

	TArray<FNiagaraParamExposerModuleInfo> ListModules(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		ENiagaraParamExposerScriptUsage ScriptUsage)
	{
		TArray<FNiagaraParamExposerModuleInfo> Out;
		FString Error;
		if (!IsValidSystem(NiagaraSystem, Error))
		{
			return Out;
		}

		const FNiagaraEmitterHandle* Handle = FindEmitterHandle(NiagaraSystem, EmitterName);
		if (!Handle)
		{
			return Out;
		}

		for (ENiagaraScriptUsage Usage : GetUsagesToSearch(ScriptUsage))
		{
			TArray<UNiagaraNodeFunctionCall*> FunctionCalls;
			NiagaraParamExposerNiagaraHelpers::CollectStackFunctionCalls(*Handle, Usage, FunctionCalls);
			for (UNiagaraNodeFunctionCall* FunctionCall : FunctionCalls)
			{
				if (!FunctionCall)
				{
					continue;
				}

				FNiagaraParamExposerModuleInfo Info;
				Info.ModuleName = GetModuleLabel(*FunctionCall);
				Info.ModuleDisplayName = FunctionCall->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
				Info.ScriptUsage = FromNiagaraScriptUsage(Usage);
				Out.Add(Info);
			}
		}
		return Out;
	}

	TArray<FNiagaraParamExposerModuleInputInfo> ListModuleInputs(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		const FString& ModuleName,
		ENiagaraParamExposerScriptUsage ScriptUsage)
	{
		TArray<FNiagaraParamExposerModuleInputInfo> Out;
		FString Error;
		if (!IsValidSystem(NiagaraSystem, Error))
		{
			return Out;
		}

		ENiagaraScriptUsage ResolvedUsage = ENiagaraScriptUsage::ParticleSpawnScript;
		UNiagaraNodeFunctionCall* FunctionCall =
			FindModuleNode(NiagaraSystem, EmitterName, ModuleName, ScriptUsage, ResolvedUsage);
		if (!FunctionCall)
		{
			return Out;
		}

		const FNiagaraEmitterHandle* EmitterHandle = FindEmitterHandle(NiagaraSystem, EmitterName);

		UE_LOG(
			LogNiagaraParamExposer,
			Display,
			TEXT("ListModuleInputs: begin emitter=%s module=%s"),
			*EmitterName,
			*ModuleName);

		TArray<FNiagaraVariable> Inputs;
		NiagaraParamExposerNiagaraHelpers::EnumerateModuleInputVariablesReadOnly(*FunctionCall, Inputs);

		for (const FNiagaraVariable& InputVariable : Inputs)
		{
			FNiagaraParamExposerModuleInputInfo Info;
			Info.InputName = NiagaraParamExposerNiagaraHelpers::GetInputLeafName(InputVariable);
			Info.TypeName = InputVariable.GetType().GetName();
			UEdGraphPin* OverridePin = NiagaraParamExposerNiagaraHelpers::FindExistingStackFunctionInputOverridePin(
				*FunctionCall,
				InputVariable);

			FNiagaraParamExposerResolvedModuleInputValue Resolved;
			if (EmitterHandle)
			{
				NiagaraParamExposerNiagaraHelpers::ResolveModuleInputCurrentValue(
					*NiagaraSystem,
					*EmitterHandle,
					ResolvedUsage,
					*FunctionCall,
					InputVariable,
					OverridePin,
					Resolved);
			}

			Info.BindingMode = Resolved.BindingMode;
			Info.ValueAsString = NiagaraParamExposerNiagaraHelpers::VariableValueToString(Resolved.Value);

			FString LinkedName;
			ENiagaraParamExposerInputBindingMode PinBindingMode = ENiagaraParamExposerInputBindingMode::Default;
			if (OverridePin
				&& NiagaraParamExposerNiagaraHelpers::TryReadLinkedParameterFromOverridePin(
					*OverridePin,
					LinkedName,
					PinBindingMode))
			{
				Info.BindingMode = PinBindingMode;
				if (PinBindingMode == ENiagaraParamExposerInputBindingMode::Linked)
				{
					Info.LinkedParameterName = LinkedName;
					FNiagaraVariable LinkedValue;
					if (NiagaraParamExposerNiagaraHelpers::TryReadUserParameterValueFromSystem(
							*NiagaraSystem,
							LinkedName,
							InputVariable.GetType(),
							LinkedValue))
					{
						Info.ValueAsString = NiagaraParamExposerNiagaraHelpers::VariableValueToString(LinkedValue);
					}
				}
			}

			Out.Add(Info);
		}

		UE_LOG(
			LogNiagaraParamExposer,
			Display,
			TEXT("ListModuleInputs: end emitter=%s module=%s count=%d"),
			*EmitterName,
			*ModuleName,
			Out.Num());
		return Out;
	}

	UNiagaraNodeFunctionCall* FindModuleFunctionCall(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		const FString& ModuleName,
		ENiagaraParamExposerScriptUsage ScriptUsage,
		ENiagaraParamExposerScriptUsage& OutResolvedScriptUsage)
	{
		ENiagaraScriptUsage ResolvedUsage = ENiagaraScriptUsage::ParticleSpawnScript;
		UNiagaraNodeFunctionCall* FunctionCall =
			FindModuleNode(NiagaraSystem, EmitterName, ModuleName, ScriptUsage, ResolvedUsage);
		OutResolvedScriptUsage = FromNiagaraScriptUsage(ResolvedUsage);
		return FunctionCall;
	}

	static FNiagaraParamExposerExposeResult ExposeModuleInputInternal(
		UNiagaraSystem* NiagaraSystem,
		const FNiagaraParamExposerExposeRequest& Request,
		bool bUseTransaction,
		bool bDeferFinalizeAndCategory,
		TArray<FNiagaraVariable>* OutPersistDefaults,
		TMap<FString, TSet<FString>>* CategoryAccumulator)
	{
		FString Error;
		if (!IsValidSystem(NiagaraSystem, Error))
		{
			return MakeFailure(Request, Error);
		}

		TUniquePtr<FScopedTransaction> Transaction;
		if (bUseTransaction)
		{
			Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("NiagaraParamExposer", "ExposeInput", "Expose Niagara Module Input"));
			NiagaraSystem->Modify();
		}

		ENiagaraScriptUsage ResolvedUsage = ENiagaraScriptUsage::ParticleSpawnScript;
		UNiagaraNodeFunctionCall* FunctionCall =
			FindModuleNode(NiagaraSystem, Request.EmitterName, Request.ModuleName, Request.ScriptUsage, ResolvedUsage);
		if (!FunctionCall)
		{
			return MakeFailure(Request, FString::Printf(TEXT("Module '%s' not found on emitter '%s'."), *Request.ModuleName, *Request.EmitterName));
		}

		if (NiagaraParamExposerInputFilter::IsParticleStateLifetime(*FunctionCall, Request.InputName))
		{
			return MakeFailure(Request, TEXT("Use Initialize Particle > Point Attributes > Lifetime. Particle State must read Particles.Lifetime."));
		}

		const FNiagaraEmitterHandle* EmitterHandle = FindEmitterHandle(NiagaraSystem, Request.EmitterName);

		FNiagaraVariable ModuleInputVariable;
		if (!TryGetStackInputVariable(*FunctionCall, Request.InputName, ModuleInputVariable))
		{
			return MakeFailure(Request, FString::Printf(TEXT("Input '%s' not found on module '%s'."), *Request.InputName, *Request.ModuleName));
		}

		const FString ShortUserName = Request.CustomUserParameterName.IsEmpty()
			? MakeDefaultUserParameterShortName(Request.EmitterName, Request.InputName)
			: Request.CustomUserParameterName;

		UEdGraphPin* OverridePin = NiagaraParamExposerNiagaraHelpers::FindExistingStackFunctionInputOverridePin(
			*FunctionCall,
			ModuleInputVariable);

		FNiagaraParamExposerResolvedModuleInputValue Resolved;
		if (EmitterHandle)
		{
			NiagaraParamExposerNiagaraHelpers::ResolveModuleInputCurrentValue(
				*NiagaraSystem,
				*EmitterHandle,
				ResolvedUsage,
				*FunctionCall,
				ModuleInputVariable,
				OverridePin,
				Resolved);
		}

		NiagaraParamExposerUserHierarchy::PruneStaleUserParameterHierarchy(*NiagaraSystem);

		FNiagaraVariable UserVariable = MakeUserParameterVariable(ModuleInputVariable, Resolved.Value, ShortUserName);

		UNiagaraScriptVariable* UserScriptVariable =
			EnsureUserParameterOnSystem(NiagaraSystem, UserVariable, Request.EmitterName, Error);
		if (!UserScriptVariable)
		{
			return MakeFailure(Request, Error);
		}

		WriteUserParameterDefaults(NiagaraSystem, UserVariable);

		if (!NiagaraParamExposerStackLink::LinkModuleInputToUserParameter(
				*FunctionCall,
				*NiagaraSystem,
				EmitterHandle,
				ModuleInputVariable,
				ShortUserName,
				UserScriptVariable,
				Error))
		{
			return MakeFailure(Request, Error);
		}

		WriteUserParameterDefaults(NiagaraSystem, UserVariable);

		FString CategoryWarning;
		if (Request.bAssignToEmitterCategory)
		{
			if (CategoryAccumulator)
			{
				CategoryAccumulator->FindOrAdd(NiagaraParamExposerUserHierarchy::CategoryForParameter(ShortUserName, Request.EmitterName)).Add(ShortUserName);
			}
			else if (!bDeferFinalizeAndCategory)
			{
				FString CategoryError;
				if (!NiagaraParamExposerUserHierarchy::AssignUserParametersToCategory(
						*NiagaraSystem,
						NiagaraParamExposerUserHierarchy::CategoryForParameter(ShortUserName, Request.EmitterName),
						{ShortUserName},
						CategoryError))
				{
					CategoryWarning = CategoryError;
				}
			}
		}

		if (OutPersistDefaults)
		{
			OutPersistDefaults->Add(UserVariable);
		}

		if (!bDeferFinalizeAndCategory)
		{
			if (Request.bAssignToEmitterCategory)
			{
				FString RepairError;
				if (!NiagaraParamExposerUserHierarchy::RepairEmitterCategories(*NiagaraSystem, RepairError)) CategoryWarning += RepairError;
			}
			FinalizeSystemEdit(NiagaraSystem);
			WriteUserParameterDefaults(NiagaraSystem, UserVariable);
		}

		FNiagaraParamExposerExposeResult Result;
		Result.bSuccess = true;
		FString SuccessMessage = TEXT("Exposed and linked.");
		if (!Resolved.Warning.IsEmpty())
		{
			SuccessMessage += TEXT(" ") + Resolved.Warning;
		}
		if (!CategoryWarning.IsEmpty())
		{
			SuccessMessage += FString::Printf(TEXT(" Category: %s"), *CategoryWarning);
		}
		Result.Message = SuccessMessage;
		Result.EmitterName = Request.EmitterName;
		Result.ModuleName = Request.ModuleName;
		Result.InputName = Request.InputName;
		Result.UserParameterName = ShortUserName;
		Result.LinkedParameterName = FString::Printf(TEXT("User.%s"), *ShortUserName);
		return Result;
	}

	FNiagaraParamExposerExposeResult ExposeModuleInput(
		UNiagaraSystem* NiagaraSystem,
		const FNiagaraParamExposerExposeRequest& Request,
		bool bUseTransaction)
	{
		return ExposeModuleInputInternal(NiagaraSystem, Request, bUseTransaction, false, nullptr, nullptr);
	}

	TArray<FNiagaraParamExposerExposeResult> ExposeModuleInputsBatch(
		UNiagaraSystem* NiagaraSystem,
		const TArray<FNiagaraParamExposerExposeRequest>& Requests)
	{
		TArray<FNiagaraParamExposerExposeResult> Results;
		FScopedTransaction Transaction(NSLOCTEXT("NiagaraParamExposer", "ExposeBatch", "Expose Niagara Module Inputs (Batch)"));
		if (NiagaraSystem)
		{
			NiagaraSystem->Modify();
			NiagaraParamExposerUserHierarchy::PruneStaleUserParameterHierarchy(*NiagaraSystem);
		}

		TArray<FNiagaraVariable> PersistDefaults;
		TMap<FString, TSet<FString>> CategoryAccumulator;

		for (const FNiagaraParamExposerExposeRequest& Request : Requests)
		{
			Results.Add(ExposeModuleInputInternal(
				NiagaraSystem,
				Request,
				false,
				true,
				&PersistDefaults,
				&CategoryAccumulator));
		}

		if (NiagaraSystem)
		{
			for (const TPair<FString, TSet<FString>>& CategoryPair : CategoryAccumulator)
			{
				TArray<FString> ShortNames = CategoryPair.Value.Array();
				ShortNames.Sort();
				FString CategoryError;
				UE_LOG(
					LogNiagaraParamExposer,
					Display,
					TEXT("ExposeModuleInputsBatch: assigning category '%s' with %d parameter(s)"),
					*CategoryPair.Key,
					ShortNames.Num());
				const bool bCategoryOk = NiagaraParamExposerUserHierarchy::AssignUserParametersToCategory(
					*NiagaraSystem,
					CategoryPair.Key,
					ShortNames,
					CategoryError);
				if (!bCategoryOk)
				{
					UE_LOG(LogNiagaraParamExposer, Warning, TEXT("ExposeModuleInputsBatch: category '%s' failed: %s"),
						*CategoryPair.Key,
						*CategoryError);
				}
			}

			if (CategoryAccumulator.Num() > 0)
			{
				FString RepairError;
				if (!NiagaraParamExposerUserHierarchy::RepairEmitterCategories(*NiagaraSystem, RepairError))
				{
					for (auto& Result : Results) Result.Message += TEXT(" Category repair: ") + RepairError;
				}
			}
			FinalizeSystemEdit(NiagaraSystem);

			for (const FNiagaraVariable& UserVariable : PersistDefaults)
			{
				WriteUserParameterDefaults(NiagaraSystem, UserVariable);
			}
		}
		return Results;
	}

	TArray<FNiagaraParamExposerExposeResult> ExposeCommonEmitterParameters(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		bool bIncludeSpawnRate,
		bool bIncludeLifetime,
		bool bIncludeSpriteSize,
		bool bIncludeColor,
		bool bIncludeVelocity)
	{
		TArray<FNiagaraParamExposerExposeRequest> Requests;

		auto AddRequest = [&](const TCHAR* ModuleName, const TCHAR* InputName, ENiagaraParamExposerScriptUsage Usage)
		{
			FNiagaraParamExposerExposeRequest Req;
			Req.EmitterName = EmitterName;
			Req.ModuleName = ModuleName;
			Req.InputName = InputName;
			Req.ScriptUsage = Usage;
			Requests.Add(Req);
		};

		if (bIncludeSpawnRate)
		{
			AddRequest(TEXT("Spawn Rate"), TEXT("SpawnRate"), ENiagaraParamExposerScriptUsage::EmitterUpdate);
			AddRequest(TEXT("SpawnRate"), TEXT("SpawnRate"), ENiagaraParamExposerScriptUsage::EmitterUpdate);
		}
		if (bIncludeLifetime)
		{
			AddRequest(TEXT("Initialize Particle"), TEXT("Lifetime"), ENiagaraParamExposerScriptUsage::ParticleSpawn);
			AddRequest(TEXT("InitializeParticle"), TEXT("Lifetime"), ENiagaraParamExposerScriptUsage::ParticleSpawn);
		}
		if (bIncludeSpriteSize)
		{
			AddRequest(TEXT("Initialize Particle"), TEXT("Sprite Size"), ENiagaraParamExposerScriptUsage::ParticleSpawn);
			AddRequest(TEXT("Initialize Particle"), TEXT("Uniform Sprite Size"), ENiagaraParamExposerScriptUsage::ParticleSpawn);
		}
		if (bIncludeColor)
		{
			AddRequest(TEXT("Initialize Particle"), TEXT("Color"), ENiagaraParamExposerScriptUsage::ParticleSpawn);
			AddRequest(TEXT("Scale Color"), TEXT("Scale"), ENiagaraParamExposerScriptUsage::ParticleUpdate);
		}
		if (bIncludeVelocity)
		{
			AddRequest(TEXT("Add Velocity"), TEXT("Velocity"), ENiagaraParamExposerScriptUsage::ParticleSpawn);
			AddRequest(TEXT("Initialize Particle"), TEXT("Velocity"), ENiagaraParamExposerScriptUsage::ParticleSpawn);
		}

		TArray<FNiagaraParamExposerExposeResult> Results;
		TSet<FString> SeenKeys;

		for (const FNiagaraParamExposerExposeRequest& Request : Requests)
		{
			const FString Key = FString::Printf(TEXT("%s|%s|%s"), *Request.ModuleName, *Request.InputName, *UEnum::GetValueAsString(Request.ScriptUsage));
			if (SeenKeys.Contains(Key))
			{
				continue;
			}
			SeenKeys.Add(Key);

			FNiagaraParamExposerExposeResult Result = ExposeModuleInput(NiagaraSystem, Request, true);
			if (Result.bSuccess)
			{
				Results.Add(Result);
			}
		}

		if (NiagaraSystem)
		{
			FinalizeSystemEdit(NiagaraSystem);
		}
		return Results;
	}

	void FinalizeSystemEdit(UNiagaraSystem* NiagaraSystem)
	{
		if (!NiagaraSystem)
		{
			return;
		}

		NiagaraSystem->MarkPackageDirty();
		NiagaraSystem->RequestCompile(false);
	}

	namespace
	{
		FNiagaraParamExposerOperationResult SetUserParameterVariable(
			UNiagaraSystem* NiagaraSystem,
			const FString& UserParameterShortName,
			const FNiagaraVariable& UserVariable,
			const FString& ValueDescription,
			bool bUseTransaction)
		{
			FString Error;
			if (!IsValidSystem(NiagaraSystem, Error))
			{
				return MakeOpFailure(Error);
			}

			TUniquePtr<FScopedTransaction> Transaction;
			if (bUseTransaction)
			{
				Transaction = MakeUnique<FScopedTransaction>(
					NSLOCTEXT("NiagaraParamExposer", "SetUserParam", "Set Niagara User Parameter"));
				NiagaraSystem->Modify();
			}

			if (!EnsureUserParameterOnSystem(NiagaraSystem, UserVariable, TEXT(""), Error))
			{
				return MakeOpFailure(Error);
			}

			FinalizeSystemEdit(NiagaraSystem);

			FNiagaraParamExposerOperationResult Result;
			Result.bSuccess = true;
			Result.Message = FString::Printf(TEXT("Set User.%s to %s."), *UserParameterShortName, *ValueDescription);
			return Result;
		}
	}

	FNiagaraParamExposerOperationResult SetUserParameterFloat(
		UNiagaraSystem* NiagaraSystem,
		const FString& UserParameterShortName,
		float Value,
		bool bUseTransaction)
	{
		FNiagaraVariable UserVariable(FNiagaraTypeDefinition::GetFloatDef(), FName(*UserParameterShortName));
		UserVariable.AllocateData();
		FMemory::Memcpy(UserVariable.GetData(), &Value, sizeof(float));
		return SetUserParameterVariable(
			NiagaraSystem,
			UserParameterShortName,
			UserVariable,
			FString::SanitizeFloat(Value),
			bUseTransaction);
	}

	FNiagaraParamExposerOperationResult SetUserParameterVector2D(
		UNiagaraSystem* NiagaraSystem,
		const FString& UserParameterShortName,
		const FVector2D& Value,
		bool bUseTransaction)
	{
		FNiagaraVariable UserVariable(FNiagaraTypeDefinition::GetVec2Def(), FName(*UserParameterShortName));
		UserVariable.AllocateData();
		const FVector2f ValueF(Value);
		FMemory::Memcpy(UserVariable.GetData(), &ValueF, sizeof(FVector2f));
		const FString Description = FString::Printf(TEXT("(%s, %s)"), *FString::SanitizeFloat(Value.X), *FString::SanitizeFloat(Value.Y));
		return SetUserParameterVariable(NiagaraSystem, UserParameterShortName, UserVariable, Description, bUseTransaction);
	}

	FNiagaraParamExposerOperationResult SetUserParameterVector(
		UNiagaraSystem* NiagaraSystem,
		const FString& UserParameterShortName,
		const FVector& Value,
		bool bUseTransaction)
	{
		FNiagaraVariable UserVariable(FNiagaraTypeDefinition::GetVec3Def(), FName(*UserParameterShortName));
		UserVariable.AllocateData();
		const FVector3f ValueF(Value);
		FMemory::Memcpy(UserVariable.GetData(), &ValueF, sizeof(FVector3f));
		const FString Description = FString::Printf(
			TEXT("(%s, %s, %s)"),
			*FString::SanitizeFloat(Value.X),
			*FString::SanitizeFloat(Value.Y),
			*FString::SanitizeFloat(Value.Z));
		return SetUserParameterVariable(NiagaraSystem, UserParameterShortName, UserVariable, Description, bUseTransaction);
	}

	FNiagaraParamExposerOperationResult SetUserParameterColor(
		UNiagaraSystem* NiagaraSystem,
		const FString& UserParameterShortName,
		const FLinearColor& Value,
		bool bUseTransaction)
	{
		FNiagaraVariable UserVariable(FNiagaraTypeDefinition::GetColorDef(), FName(*UserParameterShortName));
		UserVariable.AllocateData();
		FMemory::Memcpy(UserVariable.GetData(), &Value, sizeof(FLinearColor));
		const FString Description = FString::Printf(
			TEXT("(%s, %s, %s, %s)"),
			*FString::SanitizeFloat(Value.R),
			*FString::SanitizeFloat(Value.G),
			*FString::SanitizeFloat(Value.B),
			*FString::SanitizeFloat(Value.A));
		return SetUserParameterVariable(NiagaraSystem, UserParameterShortName, UserVariable, Description, bUseTransaction);
	}

	TArray<FNiagaraParamExposerUserParameterInfo> GetUserParameters(UNiagaraSystem* NiagaraSystem)
	{
		TArray<FNiagaraParamExposerUserParameterInfo> Out;
		FString Error;
		if (!IsValidSystem(NiagaraSystem, Error))
		{
			return Out;
		}

		TArray<FNiagaraVariable> UserParameters;
		NiagaraSystem->GetExposedParameters().GetUserParameters(UserParameters);
		for (const FNiagaraVariable& UserParameter : UserParameters)
		{
			FNiagaraParamExposerUserParameterInfo Info;
			Info.ShortName = UserParameter.GetName().ToString();
			Info.TypeName = UserParameter.GetType().GetName();

			FNiagaraVariable ValueVariable(UserParameter);
			if (const uint8* ParameterData = NiagaraSystem->GetExposedParameters().GetParameterData(ValueVariable))
			{
				ValueVariable.AllocateData();
				FMemory::Memcpy(ValueVariable.GetData(), ParameterData, ValueVariable.GetSizeInBytes());
			}
			Info.ValueAsString = NiagaraParamExposerNiagaraHelpers::VariableValueToString(ValueVariable);
			Out.Add(Info);
		}
		return Out;
	}

	FNiagaraParamExposerOperationResult SetUserParameterCategory(
		UNiagaraSystem* NiagaraSystem,
		const FString& CategoryName,
		const TArray<FString>& UserParameterShortNames,
		bool bUseTransaction)
	{
		FString Error;
		if (!IsValidSystem(NiagaraSystem, Error))
		{
			return MakeOpFailure(Error);
		}

		TUniquePtr<FScopedTransaction> Transaction;
		if (bUseTransaction)
		{
			Transaction = MakeUnique<FScopedTransaction>(
				NSLOCTEXT("NiagaraParamExposer", "SetUserParamCategory", "Set Niagara User Parameter Category"));
			NiagaraSystem->Modify();
		}

		if (!NiagaraParamExposerUserHierarchy::AssignUserParametersToCategory(
				*NiagaraSystem,
				CategoryName,
				UserParameterShortNames,
				Error))
		{
			return MakeOpFailure(Error);
		}

		FinalizeSystemEdit(NiagaraSystem);

		FNiagaraParamExposerOperationResult Result;
		Result.bSuccess = true;
		Result.Message = FString::Printf(TEXT("Assigned %d parameter(s) to category '%s'."), UserParameterShortNames.Num(), *CategoryName);
		return Result;
	}

	TArray<FString> ListUserParameterCategories(UNiagaraSystem* NiagaraSystem)
	{
		FString Error;
		if (!IsValidSystem(NiagaraSystem, Error))
		{
			return {};
		}
		return NiagaraParamExposerUserHierarchy::ListUserParameterCategoryNames(*NiagaraSystem);
	}

	FNiagaraParamExposerOperationResult RenameUserParameter(
		UNiagaraSystem* NiagaraSystem,
		const FString& OldShortName,
		const FString& NewShortName,
		bool bUseTransaction)
	{
		FString Error;
		if (!IsValidSystem(NiagaraSystem, Error))
		{
			return MakeOpFailure(Error);
		}

		if (NewShortName.IsEmpty())
		{
			return MakeOpFailure(TEXT("NewShortName is empty."));
		}

		TArray<FNiagaraVariable> UserParameters;
		NiagaraSystem->GetExposedParameters().GetUserParameters(UserParameters);

		const FNiagaraVariable* FoundOld = nullptr;
		for (const FNiagaraVariable& UserParameter : UserParameters)
		{
			if (UserParameter.GetName().ToString().Equals(OldShortName, ESearchCase::IgnoreCase))
			{
				FoundOld = &UserParameter;
				break;
			}
		}

		if (!FoundOld)
		{
			return MakeOpFailure(FString::Printf(TEXT("User parameter '%s' not found."), *OldShortName));
		}

		TUniquePtr<FScopedTransaction> Transaction;
		if (bUseTransaction)
		{
			Transaction = MakeUnique<FScopedTransaction>(
				NSLOCTEXT("NiagaraParamExposer", "RenameUserParam", "Rename Niagara User Parameter"));
			NiagaraSystem->Modify();
		}

		FNiagaraVariable OldVariable(*FoundOld);
		FNiagaraVariable NewVariable(FoundOld->GetType(), FName(*NewShortName));
		NewVariable.AllocateData();
		if (const uint8* OldData = NiagaraSystem->GetExposedParameters().GetParameterData(OldVariable))
		{
			FMemory::Memcpy(NewVariable.GetData(), OldData, NewVariable.GetSizeInBytes());
		}

		FNiagaraUserRedirectionParameterStore& Exposed = NiagaraSystem->GetExposedParameters();
		Exposed.RemoveParameter(OldVariable);
		Exposed.AddParameter(NewVariable, true, true);

		UNiagaraSystemEditorData* EditorData = Cast<UNiagaraSystemEditorData>(NiagaraSystem->GetEditorData());
		if (EditorData)
		{
			FNiagaraVariable OldLookup(OldVariable);
			FNiagaraUserRedirectionParameterStore::MakeUserVariable(OldLookup);
			EditorData->RenameUserScriptVariable(OldLookup, FName(*NewShortName));
			EditorData->SyncUserScriptVariables(NiagaraSystem);
		}

		FinalizeSystemEdit(NiagaraSystem);

		FNiagaraParamExposerOperationResult Result;
		Result.bSuccess = true;
		Result.Message = FString::Printf(TEXT("Renamed User.%s to User.%s."), *OldShortName, *NewShortName);
		return Result;
	}

	FNiagaraParamExposerOperationResult CreateCustomUserParameter(
		UNiagaraSystem* NiagaraSystem,
		const FString& ShortName,
		const FString& TypeName,
		const FString& ValueAsString,
		const FString& OptionalCategoryName,
		bool bUseTransaction)
	{
		FString Error;
		if (!IsValidSystem(NiagaraSystem, Error))
		{
			return MakeOpFailure(Error);
		}

		if (ShortName.IsEmpty())
		{
			return MakeOpFailure(TEXT("ShortName is empty."));
		}

		auto MakeType = [&](const FString& Type) -> FNiagaraTypeDefinition
		{
			if (Type.Equals(TEXT("float"), ESearchCase::IgnoreCase))
			{
				return FNiagaraTypeDefinition::GetFloatDef();
			}
			if (Type.Equals(TEXT("int"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("int32"), ESearchCase::IgnoreCase))
			{
				return FNiagaraTypeDefinition::GetIntDef();
			}
			if (Type.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
			{
				return FNiagaraTypeDefinition::GetBoolDef();
			}
			if (Type.Equals(TEXT("Vector2D"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("Vec2"), ESearchCase::IgnoreCase))
			{
				return FNiagaraTypeDefinition::GetVec2Def();
			}
			if (Type.Equals(TEXT("Vector"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("Vector3"), ESearchCase::IgnoreCase)
				|| Type.Equals(TEXT("Vec3"), ESearchCase::IgnoreCase))
			{
				return FNiagaraTypeDefinition::GetVec3Def();
			}
			if (Type.Equals(TEXT("LinearColor"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("Color"), ESearchCase::IgnoreCase))
			{
				return FNiagaraTypeDefinition::GetColorDef();
			}
			return FNiagaraTypeDefinition::GetFloatDef();
		};

		FNiagaraVariable UserVariable(MakeType(TypeName), FName(*ShortName));
		UserVariable.AllocateData();

		const FString TrimmedValue = ValueAsString.TrimStartAndEnd();
		if (UserVariable.GetType() == FNiagaraTypeDefinition::GetFloatDef())
		{
			double Parsed = 0.0;
			LexTryParseString(Parsed, *TrimmedValue);
			const float Value = static_cast<float>(Parsed);
			FMemory::Memcpy(UserVariable.GetData(), &Value, sizeof(float));
		}
		else if (UserVariable.GetType() == FNiagaraTypeDefinition::GetIntDef())
		{
			int32 Value = 0;
			LexTryParseString(Value, *TrimmedValue);
			FMemory::Memcpy(UserVariable.GetData(), &Value, sizeof(int32));
		}
		else if (UserVariable.GetType() == FNiagaraTypeDefinition::GetBoolDef())
		{
			bool Value = TrimmedValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || TrimmedValue == TEXT("1");
			const FNiagaraBool BoolValue(Value);
			FMemory::Memcpy(UserVariable.GetData(), &BoolValue, sizeof(FNiagaraBool));
		}

		TUniquePtr<FScopedTransaction> Transaction;
		if (bUseTransaction)
		{
			Transaction = MakeUnique<FScopedTransaction>(
				NSLOCTEXT("NiagaraParamExposer", "CreateUserParam", "Create Niagara User Parameter"));
			NiagaraSystem->Modify();
		}

		if (!EnsureUserParameterOnSystem(NiagaraSystem, UserVariable, TEXT(""), Error))
		{
			return MakeOpFailure(Error);
		}

		if (!OptionalCategoryName.IsEmpty())
		{
			FString CategoryError;
			NiagaraParamExposerUserHierarchy::AssignUserParametersToCategory(
				*NiagaraSystem,
				OptionalCategoryName,
				{ShortName},
				CategoryError);
		}

		FinalizeSystemEdit(NiagaraSystem);

		FNiagaraParamExposerOperationResult Result;
		Result.bSuccess = true;
		Result.Message = FString::Printf(TEXT("Created User.%s (%s)."), *ShortName, *TypeName);
		return Result;
	}

	FNiagaraParamExposerOperationResult RenameEmitter(
		UNiagaraSystem* NiagaraSystem,
		const FString& OldName,
		const FString& NewName,
		bool bUseTransaction)
	{
		FString Error;
		if (!IsValidSystem(NiagaraSystem, Error))
		{
			return MakeOpFailure(Error);
		}

		if (NewName.IsEmpty())
		{
			return MakeOpFailure(TEXT("NewName is empty."));
		}

		TUniquePtr<FScopedTransaction> Transaction;
		if (bUseTransaction)
		{
			Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("NiagaraParamExposer", "RenameEmitter", "Rename Niagara Emitter"));
			NiagaraSystem->Modify();
		}

		bool bRenamed = false;
		const int32 EmitterCount = NiagaraSystem->GetNumEmitters();
		for (int32 EmitterIndex = 0; EmitterIndex < EmitterCount; ++EmitterIndex)
		{
			FNiagaraEmitterHandle Handle = NiagaraSystem->GetEmitterHandle(EmitterIndex);
			if (!Handle.GetName().ToString().Equals(OldName, ESearchCase::IgnoreCase))
			{
				continue;
			}

			Handle.SetName(FName(*NewName), *NiagaraSystem);
			bRenamed = true;
			break;
		}

		if (!bRenamed)
		{
			return MakeOpFailure(FString::Printf(TEXT("Emitter '%s' not found."), *OldName));
		}

		FinalizeSystemEdit(NiagaraSystem);

		FNiagaraParamExposerOperationResult Result;
		Result.bSuccess = true;
		Result.Message = FString::Printf(TEXT("Renamed emitter '%s' to '%s'."), *OldName, *NewName);
		return Result;
	}

	FNiagaraParamExposerOperationResult MakeOpFailure(const FString& Message)
	{
		FNiagaraParamExposerOperationResult Result;
		Result.bSuccess = false;
		Result.Message = Message;
		return Result;
	}

 FNiagaraParamExposerOperationResult AddSetParameterModule(
  UNiagaraSystem* System, const FString& Emitter, ENiagaraParamExposerScriptUsage Usage,
  const FString& Name, float Value, const FString& Link, bool Transaction)
 {
  return AddTypedSetParameterModule(System, Emitter, Usage, Name, TEXT("float"), FString::SanitizeFloat(Value), Link, Transaction);
 }

	FNiagaraParamExposerOperationResult AddTypedSetParameterModule(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		ENiagaraParamExposerScriptUsage ScriptUsage,
		const FString& ParameterName,
		const FString& TypeName,
        const FString& ValueAsString,
		const FString& LinkToUserParameterName,
		bool bUseTransaction)
	{
        FNiagaraVariable TypedValue;
        FString PinDefault;
        if (!NiagaraParamExposerTypedValue::Parse(TypeName, ValueAsString, TypedValue, PinDefault))
            return MakeOpFailure(TEXT("Invalid value for selected type. Use comma-separated components for vectors and R,G,B,A for color."));
		FString Error;
		if (!IsValidSystem(NiagaraSystem, Error))
		{
			return MakeOpFailure(Error);
		}

		if (ScriptUsage == ENiagaraParamExposerScriptUsage::Any)
		{
			return MakeOpFailure(TEXT("ScriptUsage must be a specific stack stage for add_set_parameter_module."));
		}

		const FNiagaraEmitterHandle* Handle = FindEmitterHandle(NiagaraSystem, EmitterName);
		if (!Handle)
		{
			return MakeOpFailure(FString::Printf(TEXT("Emitter '%s' not found."), *EmitterName));
		}

		TUniquePtr<FScopedTransaction> Transaction;
		if (bUseTransaction)
		{
			Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("NiagaraParamExposer", "AddSetParam", "Add Niagara Set Parameters Module"));
			NiagaraSystem->Modify();
		}

		const ENiagaraScriptUsage Usage = ToNiagaraScriptUsage(ScriptUsage);
		UNiagaraGraph* Graph = NiagaraParamExposerNiagaraHelpers::GetEmitterGraphForUsage(*Handle, Usage);
		if (!Graph)
		{
			return MakeOpFailure(TEXT("Emitter graph not found for the requested script usage."));
		}

		UNiagaraNodeOutput* OutputNode = Graph->FindEquivalentOutputNode(Usage, FGuid());
		if (!OutputNode)
		{
			return MakeOpFailure(TEXT("Output node not found for the requested script usage."));
		}

		FNiagaraVariableBase ParsedParameter;
		if (!NiagaraParamExposerNiagaraHelpers::ParseFullParameterName(
				ParameterName,
				TypedValue.GetType(),
				ParsedParameter))
		{
			return MakeOpFailure(FString::Printf(TEXT("Invalid parameter name '%s'."), *ParameterName));
		}

		FNiagaraVariable AssignmentVar(ParsedParameter);
        AssignmentVar.SetData(TypedValue.GetData());
        if (!LinkToUserParameterName.IsEmpty())
        {
            FString Target = LinkToUserParameterName.Equals(TEXT("auto"), ESearchCase::IgnoreCase)
                ? MakeDefaultUserParameterShortName(EmitterName, GetParameterLeafName(ParameterName)) : LinkToUserParameterName;
            Target.RemoveFromStart(TEXT("User."));
            TArray<FNiagaraVariable> Existing;
            NiagaraSystem->GetExposedParameters().GetUserParameters(Existing);
            for (const auto& Var : Existing)
            {
                FString Name = Var.GetName().ToString(); Name.RemoveFromStart(TEXT("User."));
                if (Name == Target) return MakeOpFailure(TEXT("User.") + Target + TEXT(" already exists. Choose a new name to preserve the existing parameter."));
            }
        }
        Graph->Modify();
		TArray<FNiagaraVariable> Variables;
		Variables.Add(AssignmentVar);
		TArray<FString> DefaultStrings;
		DefaultStrings.Add(PinDefault);

		UNiagaraNodeAssignment* AssignmentNode = FNiagaraStackGraphUtilities::AddParameterModuleToStack(
			Variables,
			*OutputNode,
			INDEX_NONE,
			DefaultStrings);
		if (!AssignmentNode)
		{
			return MakeOpFailure(TEXT("AddParameterModuleToStack failed."));
		}

		if (!LinkToUserParameterName.IsEmpty())
		{
			const FString LeafName = GetParameterLeafName(ParameterName);
			const FString ShortUserName = LinkToUserParameterName.Equals(TEXT("auto"), ESearchCase::IgnoreCase)
				? MakeDefaultUserParameterShortName(EmitterName, LeafName)
				: LinkToUserParameterName;

			FNiagaraVariable UserVariable = MakeUserParameterVariable(AssignmentVar, AssignmentVar, ShortUserName);
			WriteUserParameterDefaults(NiagaraSystem, UserVariable);
			UNiagaraScriptVariable* UserScriptVariable =
				EnsureUserParameterOnSystem(NiagaraSystem, UserVariable, EmitterName, Error);
			if (!UserScriptVariable)
			{
				return MakeOpFailure(Error);
			}

			FNiagaraVariable AssignmentInput;
			if (!TryGetStackInputVariable(*AssignmentNode, LeafName, AssignmentInput))
			{
				TryGetStackInputVariable(*AssignmentNode, ParameterName, AssignmentInput);
			}

			if (!AssignmentInput.IsValid())
			{
				return MakeOpFailure(FString::Printf(TEXT("Could not resolve Set Parameters input for '%s'."), *LeafName));
			}

			if (!NiagaraParamExposerStackLink::LinkModuleInputToUserParameter(
					*AssignmentNode,
					*NiagaraSystem,
					Handle,
					AssignmentInput,
					ShortUserName,
					UserScriptVariable,
					Error))
			{
				return MakeOpFailure(Error);
			}
		}

		FinalizeSystemEdit(NiagaraSystem);

		FNiagaraParamExposerOperationResult Result;
		Result.bSuccess = true;
		Result.Message = FString::Printf(TEXT("Set Parameters module added (%s)."), *TypeName);
		return Result;
	}

	FNiagaraParamExposerOperationResult LinkModuleInputToParameter(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		const FString& ModuleName,
		const FString& InputName,
		ENiagaraParamExposerScriptUsage ScriptUsage,
		const FString& ParameterName,
		bool bUseTransaction)
	{
		FString Error;
		if (!IsValidSystem(NiagaraSystem, Error))
		{
			return MakeOpFailure(Error);
		}

		TUniquePtr<FScopedTransaction> Transaction;
		if (bUseTransaction)
		{
			Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("NiagaraParamExposer", "LinkParam", "Link Niagara Module Input"));
			NiagaraSystem->Modify();
		}

		ENiagaraScriptUsage ResolvedUsage = ENiagaraScriptUsage::ParticleSpawnScript;
		UNiagaraNodeFunctionCall* FunctionCall =
			FindModuleNode(NiagaraSystem, EmitterName, ModuleName, ScriptUsage, ResolvedUsage);
		if (!FunctionCall)
		{
			return MakeOpFailure(FString::Printf(TEXT("Module '%s' not found on emitter '%s'."), *ModuleName, *EmitterName));
		}

		const FNiagaraEmitterHandle* EmitterHandle = FindEmitterHandle(NiagaraSystem, EmitterName);

		FNiagaraVariable ModuleInputVariable;
		if (!TryGetStackInputVariable(*FunctionCall, InputName, ModuleInputVariable))
		{
			return MakeOpFailure(FString::Printf(TEXT("Input '%s' not found on module '%s'."), *InputName, *ModuleName));
		}

		if (!NiagaraParamExposerStackLink::LinkModuleInputToParameter(
				*FunctionCall,
				*NiagaraSystem,
				EmitterHandle,
				ModuleInputVariable,
				ParameterName,
				FGuid(),
				Error))
		{
			return MakeOpFailure(Error);
		}

		FinalizeSystemEdit(NiagaraSystem);

		FNiagaraParamExposerOperationResult Result;
		Result.bSuccess = true;
		Result.Message = TEXT("Linked module input.");
		return Result;
	}

	TArray<FNiagaraParamExposerRendererMaterialScalarInfo> ListRendererMaterialScalarParameters(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName)
	{
		TArray<FNiagaraParamExposerRendererMaterialScalarInfo> Out;
		FString Error;
		if (!IsValidSystem(NiagaraSystem, Error))
		{
			return Out;
		}

		const FNiagaraEmitterHandle* Handle = FindEmitterHandle(NiagaraSystem, EmitterName);
		if (!Handle)
		{
			return Out;
		}

		TArray<UNiagaraRendererProperties*> Renderers;
		NiagaraParamExposerNiagaraHelpers::CollectEmitterRenderers(*Handle, Renderers);
		for (int32 RendererIndex = 0; RendererIndex < Renderers.Num(); ++RendererIndex)
		{
			UNiagaraRendererProperties* Renderer = Renderers[RendererIndex];
			if (!Renderer)
			{
				continue;
			}

			TArray<UMaterialInterface*> Materials;
			NiagaraParamExposerNiagaraHelpers::CollectRendererMaterials(*Renderer, Materials);
			const FString RendererClassName = Renderer->GetClass()->GetName();
			for (UMaterialInterface* Material : Materials)
			{
				if (!Material)
				{
					continue;
				}

				TArray<FName> ScalarNames;
				NiagaraParamExposerNiagaraHelpers::CollectMaterialScalarParameterNames(*Material, ScalarNames);
				const FString MaterialPath = Material->GetPathName();
				for (const FName& ScalarName : ScalarNames)
				{
					FNiagaraParamExposerRendererMaterialScalarInfo Info;
					Info.RendererIndex = RendererIndex;
					Info.RendererClassName = RendererClassName;
					Info.MaterialPath = MaterialPath;
					Info.ScalarParameterName = ScalarName.ToString();
					Out.Add(Info);
				}
			}
		}

		return Out;
	}

	FNiagaraParamExposerOperationResult BindRendererMaterialParameter(
		UNiagaraSystem* NiagaraSystem,
		const FString& EmitterName,
		const FString& MaterialParameterName,
		const FString& NiagaraParameterName,
		int32 RendererIndex,
		bool bUseTransaction)
	{
		FString Error;
		if (!IsValidSystem(NiagaraSystem, Error))
		{
			return MakeOpFailure(Error);
		}

		const FNiagaraEmitterHandle* Handle = FindEmitterHandle(NiagaraSystem, EmitterName);
		if (!Handle)
		{
			return MakeOpFailure(FString::Printf(TEXT("Emitter '%s' not found."), *EmitterName));
		}

		const FVersionedNiagaraEmitter VersionedEmitter = Handle->GetInstance();
		UNiagaraEmitter* Emitter = VersionedEmitter.Emitter;
		if (!Emitter)
		{
			return MakeOpFailure(TEXT("Emitter instance is missing on the system handle."));
		}

		FNiagaraVariableBase NiagaraParameter;
		if (!NiagaraParamExposerNiagaraHelpers::ParseFullParameterName(
				NiagaraParameterName,
				FNiagaraTypeDefinition::GetFloatDef(),
				NiagaraParameter))
		{
			return MakeOpFailure(
				FString::Printf(TEXT("Invalid Niagara parameter name '%s' (expected Namespace.Name)."), *NiagaraParameterName));
		}

		TUniquePtr<FScopedTransaction> Transaction;
		if (bUseTransaction)
		{
			Transaction = MakeUnique<FScopedTransaction>(
				NSLOCTEXT("NiagaraParamExposer", "BindMatParam", "Bind Niagara Renderer Material Parameter"));
			NiagaraSystem->Modify();
			Emitter->Modify();
		}

		const FName MaterialParamName(*MaterialParameterName);
		TArray<UNiagaraRendererProperties*> Renderers;
		NiagaraParamExposerNiagaraHelpers::CollectEmitterRenderers(*Handle, Renderers);
		int32 BoundCount = 0;

		auto BindOne = [&](UNiagaraRendererProperties* Renderer) -> bool
		{
			if (!Renderer)
			{
				return false;
			}
			return NiagaraParamExposerNiagaraHelpers::BindRendererMaterialAttribute(
				*Renderer,
				*Emitter,
				MaterialParamName,
				NiagaraParameter);
		};

		if (RendererIndex >= 0)
		{
			if (!Renderers.IsValidIndex(RendererIndex))
			{
				return MakeOpFailure(
					FString::Printf(TEXT("Renderer index %d is out of range (emitter has %d renderers)."), RendererIndex, Renderers.Num()));
			}
			if (BindOne(Renderers[RendererIndex]))
			{
				BoundCount = 1;
			}
		}
		else
		{
			for (UNiagaraRendererProperties* Renderer : Renderers)
			{
				if (BindOne(Renderer))
				{
					++BoundCount;
				}
			}
		}

		if (BoundCount == 0)
		{
			return MakeOpFailure(
				TEXT("No renderer with Material Parameter Bindings was updated (unsupported renderer type or no matching renderers)."));
		}

		FinalizeSystemEdit(NiagaraSystem);

		FNiagaraParamExposerOperationResult Result;
		Result.bSuccess = true;
		Result.Message = FString::Printf(
			TEXT("Bound material scalar '%s' to %s on %d renderer(s)."),
			*MaterialParameterName,
			*NiagaraParameterName,
			BoundCount);
		return Result;
	}
}
