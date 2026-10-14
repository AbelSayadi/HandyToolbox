// Copyright NiagaraParamExposer. All Rights Reserved.

#include "NiagaraParamExposerNiagaraHelpers.h"
#include "NiagaraParamExposerTypes.h"

#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNode.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraParamExposerLog.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSystem.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraDecalRendererProperties.h"
#include "NiagaraVolumeRendererProperties.h"
#include "NiagaraParameterMapHistory.h"
#include "NiagaraScriptVariable.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Parse.h"
#include "NiagaraRendererProperties.h"

namespace NiagaraParamExposerNiagaraHelpers
{
	namespace
	{
		static const FName NiagaraNodeParameterMapSetClassName(TEXT("NiagaraNodeParameterMapSet"));

		constexpr int32 MaxCalledGraphNodes = 512;
		constexpr int32 MaxPinWalkHops = 32;
		constexpr int32 MaxLinkWalkDepth = 16;
		constexpr int32 MaxMapSetPins = 256;
		constexpr int32 MaxRapidIterationNameVariants = 4;

		FString GetEmitterUniqueName(const FNiagaraEmitterHandle& Handle)
		{
			const FString InstanceName = Handle.GetUniqueInstanceName();
			if (!InstanceName.IsEmpty())
			{
				return InstanceName;
			}

			const FVersionedNiagaraEmitter Versioned = Handle.GetInstance();
			if (Versioned.Emitter)
			{
				return Versioned.Emitter->GetUniqueEmitterName();
			}
			return FString();
		}

		void CollectRapidIterationHostScripts(
			UNiagaraSystem& System,
			const FNiagaraEmitterHandle& Handle,
			ENiagaraScriptUsage StackUsage,
			TArray<UNiagaraScript*>& OutScripts)
		{
			OutScripts.Reset();
			FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
			if (EmitterData)
			{
				if (UNiagaraScript* EmitterScript = EmitterData->GetScript(StackUsage, FGuid()))
				{
					OutScripts.AddUnique(EmitterScript);
				}
			}

			switch (StackUsage)
			{
			case ENiagaraScriptUsage::SystemSpawnScript:
				if (UNiagaraScript* Script = System.GetSystemSpawnScript())
				{
					OutScripts.AddUnique(Script);
				}
				break;
			case ENiagaraScriptUsage::SystemUpdateScript:
				if (UNiagaraScript* Script = System.GetSystemUpdateScript())
				{
					OutScripts.AddUnique(Script);
				}
				break;
			default:
				break;
			}
		}

		bool PinLooksLikeParameterMapInput(const UEdGraphPin* Pin)
		{
			if (!Pin || Pin->Direction != EGPD_Input)
			{
				return false;
			}

			if (Pin->PinType.PinSubCategoryObject.Get() == FNiagaraTypeDefinition::GetParameterMapDef().GetScriptStruct()) return true;
			const FString Category = Pin->PinType.PinCategory.ToString();
			if (Category.Contains(TEXT("ParameterMap"), ESearchCase::IgnoreCase))
			{
				return true;
			}

			return Pin->PinName == TEXT("ParameterMap") || Pin->PinName == TEXT("Input");
		}

		UEdGraphPin* FindModuleParameterMapInputPin(const UNiagaraNodeFunctionCall& FunctionCall)
		{
			const int32 PinCount = FMath::Min(FunctionCall.Pins.Num(), MaxMapSetPins);
			for (int32 PinIndex = 0; PinIndex < PinCount; ++PinIndex)
			{
				UEdGraphPin* Pin = FunctionCall.Pins[PinIndex];
				if (PinLooksLikeParameterMapInput(Pin))
				{
					return Pin;
				}
			}
			return nullptr;
		}

		UEdGraphPin* FindOverridePinOnMapSetNode(UEdGraphNode& MapSetNode, const FString& HandleName, const FString& InputLeafName)
		{
			if (MapSetNode.GetClass()->GetFName() != NiagaraNodeParameterMapSetClassName)
			{
				return nullptr;
			}

			const FName HandlePinName(*HandleName);
			const FName LeafPinName(*InputLeafName);
			const int32 PinCount = FMath::Min(MapSetNode.Pins.Num(), MaxMapSetPins);
			for (int32 PinIndex = 0; PinIndex < PinCount; ++PinIndex)
			{
				UEdGraphPin* Pin = MapSetNode.Pins[PinIndex];
				if (!Pin || Pin->Direction != EGPD_Input)
				{
					continue;
				}

				if (Pin->PinName == HandlePinName)
				{
					return Pin;
				}
			}
			return nullptr;
		}

		bool CopyVariableDataFromPointer(const uint8* SourceData, FNiagaraVariable& OutVariable)
		{
			if (!SourceData)
			{
				return false;
			}
			if (!OutVariable.IsDataAllocated())
			{
				OutVariable.AllocateData();
			}
			FMemory::Memcpy(OutVariable.GetData(), SourceData, OutVariable.GetSizeInBytes());
			return true;
		}

		bool TryReadRapidIterationValue(
			UNiagaraScript& Script,
			const FString& EmitterUniqueName,
			const UNiagaraNodeFunctionCall& FunctionCall,
			const FNiagaraVariable& ModuleInputVariable,
			FNiagaraVariable& OutValue)
		{
			const FNiagaraTypeDefinition& InputType = ModuleInputVariable.GetType();
			FString LeafName = ModuleInputVariable.GetName().ToString();
			LeafName.RemoveFromStart(TEXT("Module."));
			const FName InputLeaf(*LeafName);

			TArray<FString> AliasedParameterNames;
			AliasedParameterNames.Add(FString::Printf(TEXT("%s.%s"), *FunctionCall.GetFunctionName(), *InputLeaf.ToString()));

			FString SanitizedModuleName = FunctionCall.GetFunctionName();
			SanitizedModuleName.ReplaceInline(TEXT(" "), TEXT(""));
			AliasedParameterNames.Add(FString::Printf(TEXT("%s.%s"), *SanitizedModuleName, *InputLeaf.ToString()));

			const FString ModuleTitle = FunctionCall.GetNodeTitle(ENodeTitleType::ListView).ToString();
			FString SanitizedTitle = ModuleTitle;
			SanitizedTitle.ReplaceInline(TEXT(" "), TEXT(""));
			if (!SanitizedTitle.IsEmpty())
			{
				AliasedParameterNames.Add(FString::Printf(TEXT("%s.%s"), *SanitizedTitle, *InputLeaf.ToString()));
			}

			AliasedParameterNames.Add(
				FString::Printf(TEXT("Constants.%s.%s.%s"), *EmitterUniqueName, *FunctionCall.GetFunctionName(), *InputLeaf.ToString()));
			AliasedParameterNames.Add(
				FString::Printf(TEXT("Constants.%s.%s.%s"), *EmitterUniqueName, *SanitizedModuleName, *InputLeaf.ToString()));

			const FNiagaraParameterHandle ModuleHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(InputLeaf);
			const FNiagaraParameterHandle AliasedHandle =
				FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(ModuleHandle, &FunctionCall);
			const FString HandleString = AliasedHandle.GetParameterHandleString().ToString();
			if (!HandleString.IsEmpty())
			{
				AliasedParameterNames.AddUnique(HandleString);
			}

			int32 NameVariantIndex = 0;
			for (const FString& AliasedName : AliasedParameterNames)
			{
				if (NameVariantIndex++ >= MaxRapidIterationNameVariants)
				{
					break;
				}

				FNiagaraVariable VariableForConversion(InputType, FName(*AliasedName));
				const FNiagaraVariable RapidIterationLookup =
					FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(
						VariableForConversion,
						*EmitterUniqueName,
						Script.GetUsage());

				if (const uint8* ParameterData = Script.RapidIterationParameters.GetParameterData(RapidIterationLookup))
				{
					return CopyVariableDataFromPointer(ParameterData, OutValue);
				}
			}
			return false;
		}

		UNiagaraGraph* GetCalledGraphReadOnly(const UNiagaraNodeFunctionCall& FunctionCall)
		{
			if (const UNiagaraScript* CalledScript = FunctionCall.FunctionScript)
			{
				if (const UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(CalledScript->GetLatestSource()))
				{
					return Source->NodeGraph;
				}
			}
			return nullptr;
		}

		bool InputLeafNamesMatch(const FString& A, const FString& B)
		{
			return A.Equals(B, ESearchCase::IgnoreCase);
		}

		FString InputUsageToString(const ENiagaraInputNodeUsage Usage)
		{
			if (const UEnum* UsageEnum = StaticEnum<ENiagaraInputNodeUsage>())
			{
				return UsageEnum->GetNameStringByValue(static_cast<int64>(Usage));
			}
			return TEXT("Unknown");
		}

		bool IsModuleNamespaceName(const FString& VariableName)
		{
			return VariableName.StartsWith(TEXT("Module."), ESearchCase::IgnoreCase);
		}

		void TryAddUniqueModuleInput(TArray<FNiagaraVariable>& OutInputs, const FNiagaraVariable& Candidate, const TCHAR* Source)
		{
			if (!Candidate.IsValid())
			{
				return;
			}

			for (const FNiagaraVariable& Existing : OutInputs)
			{
				if (Existing.GetName() == Candidate.GetName() && Existing.GetType() == Candidate.GetType())
				{
					return;
				}
			}

			UE_LOG(
				LogNiagaraParamExposer,
				Display,
				TEXT("EnumerateModuleInputVariablesReadOnly: accept name=%s type=%s source=%s"),
				*Candidate.GetName().ToString(),
				*Candidate.GetType().GetName(),
				Source);
			OutInputs.Add(Candidate);
		}

		UEdGraphPin* FindParameterMapInputPinOnNode(UEdGraphNode& Node)
		{
			const int32 PinCount = FMath::Min(Node.Pins.Num(), MaxMapSetPins);
			for (int32 PinIndex = 0; PinIndex < PinCount; ++PinIndex)
			{
				UEdGraphPin* Pin = Node.Pins[PinIndex];
				if (PinLooksLikeParameterMapInput(Pin))
				{
					return Pin;
				}
			}
			return nullptr;
		}

		bool TryParseOverridePinLiteral(const UEdGraphPin& OverridePin, const FNiagaraTypeDefinition& Type, FNiagaraVariable& OutValue)
		{
			FString DefaultString = OverridePin.GetDefaultAsString();
			if (DefaultString.IsEmpty())
			{
				DefaultString = OverridePin.DefaultValue;
			}
			DefaultString.TrimStartAndEndInline();
			if (DefaultString.IsEmpty())
			{
				return false;
			}

			if (!OutValue.IsDataAllocated())
			{
				OutValue.AllocateData();
			}

			if (Type == FNiagaraTypeDefinition::GetFloatDef())
			{
				double Parsed = 0.0;
				if (LexTryParseString(Parsed, *DefaultString))
				{
					const float Value = static_cast<float>(Parsed);
					FMemory::Memcpy(OutValue.GetData(), &Value, sizeof(float));
					return true;
				}
			}
			else if (Type == FNiagaraTypeDefinition::GetIntDef())
			{
				int32 Parsed = 0;
				if (LexTryParseString(Parsed, *DefaultString))
				{
					FMemory::Memcpy(OutValue.GetData(), &Parsed, sizeof(int32));
					return true;
				}
			}
			else if (Type == FNiagaraTypeDefinition::GetBoolDef())
			{
				bool Parsed = false;
				if (LexTryParseString(Parsed, *DefaultString))
				{
					FNiagaraBool BoolValue(Parsed);
					FMemory::Memcpy(OutValue.GetData(), &BoolValue, sizeof(FNiagaraBool));
					return true;
				}
			}

			return false;
		}
	}

	FString GetInputLeafName(const FNiagaraVariable& Variable)
	{
		FString ShortName = Variable.GetName().ToString();
		ShortName.RemoveFromStart(TEXT("Module."));
		ShortName.RemoveFromStart(TEXT("Emitter."));
		ShortName.RemoveFromStart(TEXT("Particles."));
		int32 DotIndex = INDEX_NONE;
		if (ShortName.FindLastChar(TEXT('.'), DotIndex))
		{
			ShortName = ShortName.Mid(DotIndex + 1);
		}
		return ShortName;
	}

	FNiagaraParameterHandle MakeAliasedModuleInputHandle(
		const UNiagaraNodeFunctionCall& FunctionCall,
		const FNiagaraVariable& InputVariable)
	{
        // Assignment modules expose Module.<full target>, e.g. Module.Emitter.Hueshift.
        // Dropping Emitter/Particles creates a different override that the native input never reads.
        FString InputName = InputVariable.GetName().ToString();
        if (Cast<UNiagaraNodeAssignment>(&FunctionCall))
            InputName.RemoveFromStart(TEXT("Module."));
        else if (!InputName.RemoveFromStart(TEXT("Module.")))
            InputName = GetInputLeafName(InputVariable);
        const FNiagaraParameterHandle ModuleHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(FName(*InputName));
		return FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(ModuleHandle, &FunctionCall);
	}

	UNiagaraGraph* GetEmitterGraphForUsage(const FNiagaraEmitterHandle& Handle, ENiagaraScriptUsage Usage)
	{
		FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
		if (!EmitterData)
		{
			return nullptr;
		}

		UNiagaraScript* Script = EmitterData->GetScript(Usage, FGuid());
		if (!Script)
		{
			return nullptr;
		}

		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
		return Source ? Source->NodeGraph : nullptr;
	}

	void GetStackTraversalNodes(
		const FNiagaraEmitterHandle& Handle,
		ENiagaraScriptUsage Usage,
		TArray<UNiagaraNode*>& OutNodes)
	{
		OutNodes.Reset();
		UNiagaraGraph* Graph = GetEmitterGraphForUsage(Handle, Usage);
		if (!Graph)
		{
			return;
		}

		Graph->BuildTraversal(OutNodes, Usage, FGuid(), false);
	}

	void CollectStackFunctionCalls(
		const FNiagaraEmitterHandle& Handle,
		ENiagaraScriptUsage Usage,
		TArray<UNiagaraNodeFunctionCall*>& OutFunctionCalls)
	{
		OutFunctionCalls.Reset();
		TArray<UNiagaraNode*> Traversed;
		GetStackTraversalNodes(Handle, Usage, Traversed);
		for (UNiagaraNode* Node : Traversed)
		{
			if (UNiagaraNodeFunctionCall* FunctionCall = Cast<UNiagaraNodeFunctionCall>(Node))
			{
				OutFunctionCalls.Add(FunctionCall);
			}
		}
	}

	void GatherKnownLinkParameters(
		UNiagaraSystem& System,
		const FNiagaraEmitterHandle* EmitterHandle,
		TSet<FNiagaraVariableBase>& OutParameters)
	{
		OutParameters.Reset();

		const FNiagaraUserRedirectionParameterStore& Exposed = System.GetExposedParameters();
		TArray<FNiagaraVariable> ExposedVariables;
		Exposed.GetParameters(ExposedVariables);
		for (const FNiagaraVariable& Variable : ExposedVariables)
		{
			FNiagaraVariableBase Namespaced(Variable);
			FNiagaraUserRedirectionParameterStore::MakeUserVariable(Namespaced);
			OutParameters.Add(Namespaced);
		}

		TArray<FNiagaraVariable> UserParameters;
		Exposed.GetUserParameters(UserParameters);
		for (const FNiagaraVariable& UserParameter : UserParameters)
		{
			FNiagaraVariableBase Namespaced(UserParameter);
			FNiagaraUserRedirectionParameterStore::MakeUserVariable(Namespaced);
			OutParameters.Add(Namespaced);
		}

		if (!EmitterHandle)
		{
			return;
		}

		const ENiagaraScriptUsage Usages[] = {
			ENiagaraScriptUsage::EmitterSpawnScript,
			ENiagaraScriptUsage::EmitterUpdateScript,
			ENiagaraScriptUsage::ParticleSpawnScript,
			ENiagaraScriptUsage::ParticleUpdateScript,
		};

		for (ENiagaraScriptUsage Usage : Usages)
		{
			TArray<UNiagaraNodeFunctionCall*> FunctionCalls;
			CollectStackFunctionCalls(*EmitterHandle, Usage, FunctionCalls);
			for (UNiagaraNodeFunctionCall* FunctionCall : FunctionCalls)
			{
				if (UNiagaraNodeAssignment* Assignment = Cast<UNiagaraNodeAssignment>(FunctionCall))
				{
					for (const FNiagaraVariable& Target : Assignment->GetAssignmentTargets())
					{
						OutParameters.Add(FNiagaraVariableBase(Target));
					}
				}
			}
		}
	}

	bool ParseFullParameterName(
		const FString& FullParameterName,
		const FNiagaraTypeDefinition& Type,
		FNiagaraVariableBase& OutParameter)
	{
		FString Trimmed = FullParameterName;
		Trimmed.TrimStartAndEndInline();
		if (Trimmed.IsEmpty())
		{
			return false;
		}

		int32 DotIndex = INDEX_NONE;
		if (!Trimmed.FindChar(TEXT('.'), DotIndex))
		{
			return false;
		}

		const FString Namespace = Trimmed.Left(DotIndex);
		const FString LeafName = Trimmed.Mid(DotIndex + 1);
		if (Namespace.IsEmpty() || LeafName.IsEmpty())
		{
			return false;
		}

		FNiagaraVariable NamespacedVar(Type, NAME_None);
		NamespacedVar.SetName(FName(*(Namespace + TEXT(".") + LeafName)));
		OutParameter = FNiagaraVariableBase(NamespacedVar);

		if (Namespace.Equals(TEXT("User"), ESearchCase::IgnoreCase))
		{
			FNiagaraUserRedirectionParameterStore::MakeUserVariable(OutParameter);
		}
		return true;
	}

	FString GetParameterDisplayName(const FNiagaraVariableBase& Parameter)
	{
		return Parameter.GetName().ToString();
	}

	FString VariableValueToString(const FNiagaraVariable& Variable)
	{
		if (!Variable.IsDataAllocated())
		{
			return FString();
		}

		const FNiagaraTypeDefinition& Type = Variable.GetType();
		if (Type == FNiagaraTypeDefinition::GetFloatDef())
		{
			float Value = 0.f;
			FMemory::Memcpy(&Value, Variable.GetData(), sizeof(float));
			return FString::SanitizeFloat(Value);
		}
		if (Type == FNiagaraTypeDefinition::GetIntDef())
		{
			int32 Value = 0;
			FMemory::Memcpy(&Value, Variable.GetData(), sizeof(int32));
			return FString::FromInt(Value);
		}
		if (Type == FNiagaraTypeDefinition::GetBoolDef())
		{
			FNiagaraBool Value;
			FMemory::Memcpy(&Value, Variable.GetData(), sizeof(FNiagaraBool));
			return Value.GetValue() ? TEXT("true") : TEXT("false");
		}
		if (Type == FNiagaraTypeDefinition::GetVec2Def())
		{
			FVector2f Vec;
			FMemory::Memcpy(&Vec, Variable.GetData(), sizeof(FVector2f));
			return FString::Printf(TEXT("(%s, %s)"), *FString::SanitizeFloat(Vec.X), *FString::SanitizeFloat(Vec.Y));
		}
		if (Type == FNiagaraTypeDefinition::GetVec3Def())
		{
			FVector3f Vec;
			FMemory::Memcpy(&Vec, Variable.GetData(), sizeof(FVector3f));
			return FString::Printf(TEXT("(%s, %s, %s)"), *FString::SanitizeFloat(Vec.X), *FString::SanitizeFloat(Vec.Y), *FString::SanitizeFloat(Vec.Z));
		}
		if (Type == FNiagaraTypeDefinition::GetVec4Def())
		{
			FVector4f Vec;
			FMemory::Memcpy(&Vec, Variable.GetData(), sizeof(FVector4f));
			return FString::Printf(
				TEXT("(%s, %s, %s, %s)"),
				*FString::SanitizeFloat(Vec.X),
				*FString::SanitizeFloat(Vec.Y),
				*FString::SanitizeFloat(Vec.Z),
				*FString::SanitizeFloat(Vec.W));
		}
		if (Type == FNiagaraTypeDefinition::GetColorDef())
		{
			FLinearColor Color;
			FMemory::Memcpy(&Color, Variable.GetData(), sizeof(FLinearColor));
			return FString::Printf(
				TEXT("(%s, %s, %s, %s)"),
				*FString::SanitizeFloat(Color.R),
				*FString::SanitizeFloat(Color.G),
				*FString::SanitizeFloat(Color.B),
				*FString::SanitizeFloat(Color.A));
		}
		return FString();
	}

	static UEdGraphPin* FindLinkedInputPin(const UEdGraphPin& OverridePin, int32 Depth = 0)
	{
		if (Depth >= MaxLinkWalkDepth)
		{
			return nullptr;
		}

		if (OverridePin.LinkedTo.Num() == 0)
		{
			return nullptr;
		}

		UEdGraphPin* LinkedPin = OverridePin.LinkedTo[0];
		if (!LinkedPin)
		{
			return nullptr;
		}

		if (UNiagaraNodeInput* InputNode = Cast<UNiagaraNodeInput>(LinkedPin->GetOwningNode()))
		{
			if (InputNode->Usage == ENiagaraInputNodeUsage::Parameter)
			{
				return LinkedPin;
			}
		}

		if (LinkedPin->LinkedTo.Num() > 0)
		{
			return FindLinkedInputPin(*LinkedPin, Depth + 1);
		}
		return nullptr;
	}

	bool TryReadLinkedParameterFromOverridePin(
		const UEdGraphPin& OverridePin,
		FString& OutLinkedParameterName,
		ENiagaraParamExposerInputBindingMode& OutBindingMode)
	{
		// Niagara uses ParameterMapGet outputs, not literal pin strings, for parameter links.
		// A numeric literal such as 1.0 must never be mistaken for a namespaced parameter.
		for (const UEdGraphPin* LinkedPin : OverridePin.LinkedTo)
		{
			if (LinkedPin && LinkedPin->Direction == EGPD_Output && LinkedPin->GetOwningNode() &&
				LinkedPin->GetOwningNode()->GetClass()->GetFName() == TEXT("NiagaraNodeParameterMapGet"))
			{
				OutLinkedParameterName = LinkedPin->PinName.ToString();
				OutBindingMode = ENiagaraParamExposerInputBindingMode::Linked;
				return true;
			}
		}

		if (UEdGraphPin* LinkedPin = FindLinkedInputPin(OverridePin))
		{
			if (UNiagaraNodeInput* InputNode = Cast<UNiagaraNodeInput>(LinkedPin->GetOwningNode()))
			{
				OutLinkedParameterName = InputNode->Input.GetName().ToString();
				OutBindingMode = ENiagaraParamExposerInputBindingMode::Linked;
				return true;
			}
		}

		if (OverridePin.LinkedTo.Num() > 0)
		{
			OutBindingMode = ENiagaraParamExposerInputBindingMode::Dynamic;
			return true;
		}

		return false;
	}

	bool IsOverridePinLinkedToParameter(
		const UEdGraphPin& OverridePin,
		const FNiagaraVariableBase& TargetParameter,
		const FNiagaraTypeDefinition& Type)
	{
		FString LinkedName;
		ENiagaraParamExposerInputBindingMode BindingMode = ENiagaraParamExposerInputBindingMode::Default;
		if (!TryReadLinkedParameterFromOverridePin(OverridePin, LinkedName, BindingMode))
		{
			return false;
		}
		if (BindingMode != ENiagaraParamExposerInputBindingMode::Linked)
		{
			return false;
		}

		FNiagaraVariableBase Parsed;
		if (ParseFullParameterName(LinkedName, Type, Parsed))
		{
			return Parsed == TargetParameter;
		}

		const FString TargetShort = TargetParameter.GetName().ToString();
		if (LinkedName.Equals(TargetShort, ESearchCase::IgnoreCase))
		{
			return true;
		}

		const FString TargetFull = FString::Printf(TEXT("User.%s"), *TargetShort);
		return LinkedName.Equals(TargetFull, ESearchCase::IgnoreCase);
	}

	bool ClearStackFunctionInputOverridePin(
		UNiagaraNodeFunctionCall& FunctionCall,
		UEdGraphPin& OverridePin,
		FString& OutError)
	{
		const bool bHadLinks = OverridePin.LinkedTo.Num() > 0;
		const bool bHadDefault = !OverridePin.DefaultValue.IsEmpty() || !OverridePin.GetDefaultAsString().IsEmpty();
		if (!bHadLinks && !bHadDefault)
		{
			return true;
		}

		FunctionCall.Modify();
		if (UEdGraph* Graph = FunctionCall.GetGraph())
		{
			Graph->Modify();
		}
		OverridePin.Modify();

		// FNiagaraStackGraphUtilities::RemoveNodesForStackFunctionInputOverridePin is not exported (no NIAGARAEDITOR_API) in UE 5.6;
		// rely on the manual fallback below (break links + destroy directly linked nodes).

		if (OverridePin.LinkedTo.Num() > 0)
		{
			TArray<UEdGraphNode*> LinkedNodes;
			for (UEdGraphPin* LinkedPin : OverridePin.LinkedTo)
			{
				if (LinkedPin && LinkedPin->GetOwningNode())
				{
					LinkedNodes.AddUnique(LinkedPin->GetOwningNode());
				}
			}

			OverridePin.BreakAllPinLinks();
			for (UEdGraphNode* LinkedNode : LinkedNodes)
			{
				if (LinkedNode && LinkedNode->GetGraph() == FunctionCall.GetGraph())
				{
					bool bUsedElsewhere = false;
					for (const UEdGraphPin* Pin : LinkedNode->Pins)
					{
						if (Pin && Pin->Direction == EGPD_Output && !Pin->LinkedTo.IsEmpty()) bUsedElsewhere = true;
					}
					if (!bUsedElsewhere && LinkedNode->GetClass()->GetFName() == TEXT("NiagaraNodeParameterMapGet"))
					{
						LinkedNode->Modify();
						LinkedNode->DestroyNode();
					}
				}
			}
		}

		OverridePin.DefaultValue.Reset();
		if (const UEdGraphSchema* Schema = OverridePin.GetSchema())
		{
			Schema->TrySetDefaultValue(OverridePin, FString());
		}

		if (OverridePin.LinkedTo.Num() > 0)
		{
			OutError = TEXT("Could not clear existing links on the module input override pin.");
			return false;
		}

		UE_LOG(
			LogNiagaraParamExposer,
			Display,
			TEXT("ClearStackFunctionInputOverridePin: module=%s hadLinks=%d hadDefault=%d"),
			*FunctionCall.GetFunctionName(),
			bHadLinks ? 1 : 0,
			bHadDefault ? 1 : 0);
		return true;
	}

	bool TryReadUserParameterValueFromSystem(
		UNiagaraSystem& System,
		const FString& LinkedParameterName,
		const FNiagaraTypeDefinition& Type,
		FNiagaraVariable& OutValue)
	{
		FNiagaraVariableBase LinkedParameter;
		if (!ParseFullParameterName(LinkedParameterName, Type, LinkedParameter))
		{
			return false;
		}

		FNiagaraVariable Lookup(LinkedParameter);
		if (const uint8* ParameterData = System.GetExposedParameters().GetParameterData(Lookup))
		{
			OutValue = FNiagaraVariable(Type, LinkedParameter.GetName());
			if (!OutValue.IsDataAllocated())
			{
				OutValue.AllocateData();
			}
			FMemory::Memcpy(OutValue.GetData(), ParameterData, OutValue.GetSizeInBytes());
			return true;
		}
		return false;
	}

	void CollectEmitterRenderers(const FNiagaraEmitterHandle& Handle, TArray<UNiagaraRendererProperties*>& OutRenderers)
	{
		OutRenderers.Reset();
		FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
		if (!EmitterData)
		{
			return;
		}

		for (UNiagaraRendererProperties* Renderer : EmitterData->GetRenderers())
		{
			if (Renderer)
			{
				OutRenderers.Add(Renderer);
			}
		}
	}

	void CollectRendererMaterials(UNiagaraRendererProperties& Renderer, TArray<UMaterialInterface*>& OutMaterials)
	{
		if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(&Renderer))
		{
			if (Sprite->Material)
			{
				OutMaterials.AddUnique(Sprite->Material);
			}
		}
		else if (UNiagaraRibbonRendererProperties* Ribbon = Cast<UNiagaraRibbonRendererProperties>(&Renderer))
		{
			if (Ribbon->Material)
			{
				OutMaterials.AddUnique(Ribbon->Material);
			}
		}
		else if (UNiagaraDecalRendererProperties* Decal = Cast<UNiagaraDecalRendererProperties>(&Renderer))
		{
			if (Decal->Material)
			{
				OutMaterials.AddUnique(Decal->Material);
			}
		}
		else if (UNiagaraMeshRendererProperties* Mesh = Cast<UNiagaraMeshRendererProperties>(&Renderer))
		{
			for (const FNiagaraMeshMaterialOverride& Override : Mesh->OverrideMaterials)
			{
				if (Override.ExplicitMat)
				{
					OutMaterials.AddUnique(Override.ExplicitMat);
				}
			}
		}
		else if (UNiagaraVolumeRendererProperties* Volume = Cast<UNiagaraVolumeRendererProperties>(&Renderer))
		{
			if (Volume->Material)
			{
				OutMaterials.AddUnique(Volume->Material);
			}
		}
	}

	FNiagaraRendererMaterialParameters* GetRendererMaterialParameters(UNiagaraRendererProperties& Renderer)
	{
		if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(&Renderer))
		{
			return &Sprite->MaterialParameters;
		}
		if (UNiagaraRibbonRendererProperties* Ribbon = Cast<UNiagaraRibbonRendererProperties>(&Renderer))
		{
			return &Ribbon->MaterialParameters;
		}
		if (UNiagaraDecalRendererProperties* Decal = Cast<UNiagaraDecalRendererProperties>(&Renderer))
		{
			return &Decal->MaterialParameters;
		}
		if (UNiagaraMeshRendererProperties* Mesh = Cast<UNiagaraMeshRendererProperties>(&Renderer))
		{
			return &Mesh->MaterialParameters;
		}
		if (UNiagaraVolumeRendererProperties* Volume = Cast<UNiagaraVolumeRendererProperties>(&Renderer))
		{
			return &Volume->MaterialParameters;
		}
		return nullptr;
	}

	void CollectMaterialScalarParameterNames(UMaterialInterface& Material, TArray<FName>& OutScalarNames)
	{
		TArray<FMaterialParameterInfo> ParameterInfos;
		TArray<FGuid> ParameterIds;
		Material.GetAllScalarParameterInfo(ParameterInfos, ParameterIds);
		for (const FMaterialParameterInfo& Info : ParameterInfos)
		{
			OutScalarNames.AddUnique(Info.Name);
		}
	}

	bool BindRendererMaterialAttribute(
		UNiagaraRendererProperties& Renderer,
		const UNiagaraEmitter& Emitter,
		FName MaterialParameterName,
		const FNiagaraVariableBase& NiagaraParameter)
	{
		FNiagaraRendererMaterialParameters* MaterialParameters = GetRendererMaterialParameters(Renderer);
		if (!MaterialParameters)
		{
			return false;
		}

		Renderer.Modify();

		FNiagaraMaterialAttributeBinding* ExistingBinding = nullptr;
		for (FNiagaraMaterialAttributeBinding& Binding : MaterialParameters->AttributeBindings)
		{
			if (Binding.MaterialParameterName == MaterialParameterName)
			{
				ExistingBinding = &Binding;
				break;
			}
		}

		if (ExistingBinding)
		{
			ExistingBinding->NiagaraVariable = NiagaraParameter;
			ExistingBinding->CacheValues(&Emitter);
		}
		else
		{
			FNiagaraMaterialAttributeBinding NewBinding;
			NewBinding.MaterialParameterName = MaterialParameterName;
			NewBinding.NiagaraVariable = NiagaraParameter;
			NewBinding.CacheValues(&Emitter);
			MaterialParameters->AttributeBindings.Add(NewBinding);
		}

		return true;
	}

	void EnumerateModuleInputVariablesReadOnly(
		const UNiagaraNodeFunctionCall& FunctionCall,
		TArray<FNiagaraVariable>& OutInputs)
	{
		OutInputs.Reset();
		UE_LOG(
			LogNiagaraParamExposer,
			Display,
			TEXT("EnumerateModuleInputVariablesReadOnly: begin module=%s"),
			*FunctionCall.GetFunctionName());

		if (const UNiagaraNodeAssignment* AssignmentNode = Cast<UNiagaraNodeAssignment>(&FunctionCall))
		{
			const TArray<FNiagaraVariable>& Targets = AssignmentNode->GetAssignmentTargets();
			const int32 TargetCount = FMath::Min(Targets.Num(), MaxCalledGraphNodes);
			for (int32 TargetIndex = 0; TargetIndex < TargetCount; ++TargetIndex)
			{
				const FNiagaraVariable& Target = Targets[TargetIndex];
				UE_LOG(
					LogNiagaraParamExposer,
					Display,
					TEXT("EnumerateModuleInputVariablesReadOnly: assignment target name=%s type=%s"),
					*Target.GetName().ToString(),
					*Target.GetType().GetName());
				TryAddUniqueModuleInput(OutInputs, Target, TEXT("assignment_target"));
			}

			UE_LOG(
				LogNiagaraParamExposer,
				Display,
				TEXT("EnumerateModuleInputVariablesReadOnly: end module=%s count=%d"),
				*FunctionCall.GetFunctionName(),
				OutInputs.Num());
			return;
		}

		UNiagaraGraph* CalledGraph = GetCalledGraphReadOnly(FunctionCall);
		if (!CalledGraph)
		{
			UE_LOG(
				LogNiagaraParamExposer,
				Warning,
				TEXT("EnumerateModuleInputVariablesReadOnly: no called graph for module=%s"),
				*FunctionCall.GetFunctionName());
			UE_LOG(LogNiagaraParamExposer, Display, TEXT("EnumerateModuleInputVariablesReadOnly: end (0 inputs)"));
			return;
		}

		UE_LOG(LogNiagaraParamExposer, Display, TEXT("EnumerateModuleInputVariablesReadOnly: phase graph metadata"));
		const UNiagaraGraph::FScriptVariableMap& MetaData = CalledGraph->GetAllMetaData();
		int32 MetaIndex = 0;
		for (const TPair<FNiagaraVariable, TObjectPtr<UNiagaraScriptVariable>>& Pair : MetaData)
		{
			if (MetaIndex++ >= MaxCalledGraphNodes)
			{
				break;
			}

			const FString VariableName = Pair.Key.GetName().ToString();
			UE_LOG(
				LogNiagaraParamExposer,
				Display,
				TEXT("EnumerateModuleInputVariablesReadOnly: metadata candidate name=%s type=%s"),
				*VariableName,
				*Pair.Key.GetType().GetName());
			if (IsModuleNamespaceName(VariableName))
			{
				TryAddUniqueModuleInput(OutInputs, Pair.Key, TEXT("graph_metadata"));
			}
		}

		UE_LOG(LogNiagaraParamExposer, Display, TEXT("EnumerateModuleInputVariablesReadOnly: phase input nodes"));
		const int32 NodeCount = FMath::Min(CalledGraph->Nodes.Num(), MaxCalledGraphNodes);
		for (int32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
		{
			UEdGraphNode* GraphNode = CalledGraph->Nodes[NodeIndex];
			const UNiagaraNodeInput* InputNode = Cast<UNiagaraNodeInput>(GraphNode);
			if (!InputNode)
			{
				continue;
			}

			const FString VariableName = InputNode->Input.GetName().ToString();
			UE_LOG(
				LogNiagaraParamExposer,
				Display,
				TEXT("EnumerateModuleInputVariablesReadOnly: input node name=%s usage=%s type=%s"),
				*VariableName,
				*InputUsageToString(InputNode->Usage),
				*InputNode->Input.GetType().GetName());

			if (InputNode->Usage != ENiagaraInputNodeUsage::Parameter || !IsModuleNamespaceName(VariableName))
			{
				continue;
			}

			TryAddUniqueModuleInput(OutInputs, InputNode->Input, TEXT("input_node_parameter"));
		}

		UE_LOG(
			LogNiagaraParamExposer,
			Display,
			TEXT("EnumerateModuleInputVariablesReadOnly: end module=%s count=%d"),
			*FunctionCall.GetFunctionName(),
			OutInputs.Num());
	}

	bool TryFindModuleInputVariableReadOnly(
		const UNiagaraNodeFunctionCall& FunctionCall,
		const FString& InputName,
		FNiagaraVariable& OutInputVariable)
	{
		auto NormalizeToken = [](FString Token)
		{
			Token.ReplaceInline(TEXT(" "), TEXT(""));
			Token.ReplaceInline(TEXT("_"), TEXT(""));
			return Token.ToLower();
		};

		auto NamesMatchLoose = [&](const FString& A, const FString& B)
		{
			if (A.Equals(B, ESearchCase::IgnoreCase))
			{
				return true;
			}
			return NormalizeToken(A) == NormalizeToken(B);
		};

		auto GetVariableLeafName = [](const FNiagaraVariable& Variable)
		{
			FString Name = Variable.GetName().ToString();
			int32 DotIndex = INDEX_NONE;
			if (Name.FindLastChar(TEXT('.'), DotIndex))
			{
				return Name.Mid(DotIndex + 1);
			}
			return Name;
		};

		TArray<FNiagaraVariable> Inputs;
		EnumerateModuleInputVariablesReadOnly(FunctionCall, Inputs);
		for (const FNiagaraVariable& Candidate : Inputs)
		{
			const FString LeafName = GetInputLeafName(Candidate);
			const FString LastSegment = GetVariableLeafName(Candidate);
			const FString FullName = Candidate.GetName().ToString();
			if (NamesMatchLoose(LeafName, InputName)
				|| NamesMatchLoose(LastSegment, InputName)
				|| NamesMatchLoose(FullName, InputName))
			{
				OutInputVariable = Candidate;
				return true;
			}
		}
		return false;
	}

	UEdGraphPin* FindExistingStackFunctionInputOverridePin(
		const UNiagaraNodeFunctionCall& FunctionCall,
		const FNiagaraVariable& ModuleInputVariable)
	{
		const FString InputLeafName = GetInputLeafName(ModuleInputVariable);
		const FString FullVariableName = ModuleInputVariable.GetName().ToString();
		UE_LOG(
			LogNiagaraParamExposer,
			Display,
			TEXT("FindExistingStackFunctionInputOverridePin: begin module=%s input=%s full=%s"),
			*FunctionCall.GetFunctionName(),
			*InputLeafName,
			*FullVariableName);

		if (InputLeafName.IsEmpty())
		{
			UE_LOG(LogNiagaraParamExposer, Warning, TEXT("FindExistingStackFunctionInputOverridePin: empty input leaf name"));
			return nullptr;
		}

		const FNiagaraParameterHandle AliasedHandle = MakeAliasedModuleInputHandle(FunctionCall, ModuleInputVariable);
		const FString HandleName = AliasedHandle.GetParameterHandleString().ToString();

		UEdGraphPin* WalkPin = FindModuleParameterMapInputPin(FunctionCall);
		if (!WalkPin)
		{
			UE_LOG(
				LogNiagaraParamExposer,
				Warning,
				TEXT("FindExistingStackFunctionInputOverridePin: no ParameterMap input pin on module=%s"),
				*FunctionCall.GetFunctionName());
			return nullptr;
		}

		for (int32 Hop = 0; Hop < MaxPinWalkHops; ++Hop)
		{
			if (!WalkPin || WalkPin->LinkedTo.Num() == 0)
			{
				break;
			}

			UEdGraphPin* UpstreamPin = WalkPin->LinkedTo[0];
			UEdGraphNode* UpstreamNode = UpstreamPin ? UpstreamPin->GetOwningNode() : nullptr;
			if (UpstreamNode)
			{
				UEdGraphPin* OverridePin = FindOverridePinOnMapSetNode(*UpstreamNode, HandleName, InputLeafName);
				if (!OverridePin)
				{
					OverridePin = FindOverridePinOnMapSetNode(*UpstreamNode, HandleName, FullVariableName);
				}
				if (OverridePin)
				{
					UE_LOG(
						LogNiagaraParamExposer,
						Display,
						TEXT("FindExistingStackFunctionInputOverridePin: found override pin hop=%d"),
						Hop);
					return OverridePin;
				}
			}

			if (!UpstreamNode)
			{
				break;
			}

			UEdGraphPin* NextWalkPin = nullptr;
			if (UpstreamNode->GetClass()->GetFName() == NiagaraNodeParameterMapSetClassName)
			{
				NextWalkPin = FindParameterMapInputPinOnNode(*UpstreamNode);
			}
			WalkPin = NextWalkPin;
		}

		UE_LOG(
			LogNiagaraParamExposer,
			Display,
			TEXT("FindExistingStackFunctionInputOverridePin: end (not found) module=%s input=%s"),
			*FunctionCall.GetFunctionName(),
			*InputLeafName);
		return nullptr;
	}

		bool TryReadCalledGraphScriptVariableDefault(
			const UNiagaraNodeFunctionCall& FunctionCall,
			const FNiagaraVariable& ModuleInputVariable,
			FNiagaraVariable& OutValue)
		{
			UNiagaraGraph* CalledGraph = GetCalledGraphReadOnly(FunctionCall);
			if (!CalledGraph)
			{
				return false;
			}

			const UNiagaraGraph::FScriptVariableMap& MetaData = CalledGraph->GetAllMetaData();
			const TObjectPtr<UNiagaraScriptVariable>* ScriptVariablePtr = MetaData.Find(ModuleInputVariable);
			if (!ScriptVariablePtr || !*ScriptVariablePtr)
			{
				return false;
			}

			const FNiagaraVariable& ScriptVar = (*ScriptVariablePtr)->Variable;
			if (!ScriptVar.IsDataAllocated())
			{
				return false;
			}

			if (!OutValue.IsDataAllocated())
			{
				OutValue.AllocateData();
			}
			FMemory::Memcpy(OutValue.GetData(), ScriptVar.GetData(), ScriptVar.GetSizeInBytes());
			return true;
		}

	void ResolveModuleInputCurrentValue(
		UNiagaraSystem& System,
		const FNiagaraEmitterHandle& Handle,
		ENiagaraScriptUsage StackUsage,
		UNiagaraNodeFunctionCall& FunctionCall,
		const FNiagaraVariable& ModuleInputVariable,
		const UEdGraphPin* OptionalOverridePin,
		FNiagaraParamExposerResolvedModuleInputValue& OutResolved)
	{
		const FString InputLeaf = GetInputLeafName(ModuleInputVariable);
		UE_LOG(
			LogNiagaraParamExposer,
			Display,
			TEXT("ResolveModuleInputCurrentValue: begin module=%s input=%s"),
			*FunctionCall.GetFunctionName(),
			*InputLeaf);

		OutResolved = FNiagaraParamExposerResolvedModuleInputValue();
		OutResolved.Value = ModuleInputVariable;
		if (!OutResolved.Value.IsDataAllocated())
		{
			OutResolved.Value.AllocateData();
		}

		FString LinkedParameterName;
		ENiagaraParamExposerInputBindingMode PinBindingMode = ENiagaraParamExposerInputBindingMode::Default;
		if (OptionalOverridePin)
		{
			TryReadLinkedParameterFromOverridePin(*OptionalOverridePin, LinkedParameterName, PinBindingMode);
		}

		UE_LOG(LogNiagaraParamExposer, Display, TEXT("ResolveModuleInputCurrentValue: phase rapid iteration"));
		const FString EmitterUniqueName = GetEmitterUniqueName(Handle);
		TArray<UNiagaraScript*> HostScripts;
		CollectRapidIterationHostScripts(System, Handle, StackUsage, HostScripts);
		for (UNiagaraScript* HostScript : HostScripts)
		{
			if (HostScript
				&& !EmitterUniqueName.IsEmpty()
				&& TryReadRapidIterationValue(*HostScript, EmitterUniqueName, FunctionCall, ModuleInputVariable, OutResolved.Value))
			{
				OutResolved.BindingMode = ENiagaraParamExposerInputBindingMode::Local;
				OutResolved.bHasResolvedValue = true;
				UE_LOG(LogNiagaraParamExposer, Display, TEXT("ResolveModuleInputCurrentValue: end (rapid iteration)"));
				return;
			}
		}

		UE_LOG(LogNiagaraParamExposer, Display, TEXT("ResolveModuleInputCurrentValue: phase override-pin literal"));
		if (OptionalOverridePin
			&& TryParseOverridePinLiteral(*OptionalOverridePin, ModuleInputVariable.GetType(), OutResolved.Value))
		{
			OutResolved.BindingMode = ENiagaraParamExposerInputBindingMode::Local;
			OutResolved.bHasResolvedValue = true;
			UE_LOG(LogNiagaraParamExposer, Display, TEXT("ResolveModuleInputCurrentValue: end (override literal)"));
			return;
		}

		UE_LOG(LogNiagaraParamExposer, Display, TEXT("ResolveModuleInputCurrentValue: phase called-graph script variable"));
		if (TryReadCalledGraphScriptVariableDefault(FunctionCall, ModuleInputVariable, OutResolved.Value))
		{
			OutResolved.BindingMode = ENiagaraParamExposerInputBindingMode::Default;
			OutResolved.bHasResolvedValue = true;
			UE_LOG(LogNiagaraParamExposer, Display, TEXT("ResolveModuleInputCurrentValue: end (graph script variable)"));
			return;
		}

		if (PinBindingMode == ENiagaraParamExposerInputBindingMode::Linked
			&& TryReadUserParameterValueFromSystem(System, LinkedParameterName, ModuleInputVariable.GetType(), OutResolved.Value))
		{
			OutResolved.BindingMode = ENiagaraParamExposerInputBindingMode::Linked;
			OutResolved.bHasResolvedValue = true;
			UE_LOG(LogNiagaraParamExposer, Display, TEXT("ResolveModuleInputCurrentValue: end (linked parameter value)"));
			return;
		}

		if (PinBindingMode == ENiagaraParamExposerInputBindingMode::Linked)
		{
			OutResolved.BindingMode = ENiagaraParamExposerInputBindingMode::Linked;
			OutResolved.Warning = TEXT("Input is linked; could not read linked parameter value.");
			return;
		}

		if (PinBindingMode == ENiagaraParamExposerInputBindingMode::Dynamic)
		{
			OutResolved.BindingMode = ENiagaraParamExposerInputBindingMode::Dynamic;
			OutResolved.Warning = TEXT("Input is driven by a dynamic input/curve.");
			return;
		}

		UE_LOG(LogNiagaraParamExposer, Display, TEXT("ResolveModuleInputCurrentValue: phase module input default"));
		if (ModuleInputVariable.IsDataAllocated())
		{
			FMemory::Memcpy(
				OutResolved.Value.GetData(),
				ModuleInputVariable.GetData(),
				ModuleInputVariable.GetSizeInBytes());
			OutResolved.BindingMode = ENiagaraParamExposerInputBindingMode::Default;
			OutResolved.bHasResolvedValue = true;
			UE_LOG(LogNiagaraParamExposer, Display, TEXT("ResolveModuleInputCurrentValue: end (module default)"));
			return;
		}

		FMemory::Memzero(OutResolved.Value.GetData(), OutResolved.Value.GetSizeInBytes());
		OutResolved.BindingMode = ENiagaraParamExposerInputBindingMode::Default;
		OutResolved.Warning = TEXT("Could not resolve module input value; defaulting to 0.");
		UE_LOG(
			LogNiagaraParamExposer,
			Warning,
			TEXT("ResolveModuleInputCurrentValue: end (fallback 0) module=%s input=%s"),
			*FunctionCall.GetFunctionName(),
			*InputLeaf);
	}
}
