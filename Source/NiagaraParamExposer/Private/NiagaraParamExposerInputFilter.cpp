// Copyright NiagaraParamExposer. All Rights Reserved.

#include "NiagaraParamExposerInputFilter.h"

#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScript.h"
#include "EdGraph/EdGraphPin.h"
#include "UObject/UnrealType.h"
#include "Misc/ConfigCacheIni.h"

namespace NiagaraParamExposerInputFilter
{
	namespace
	{
		TArray<FString> GImportantKeywords;
		TArray<FString> GSortPriorityKeywords;

		void EnsureDefaults()
		{
			if (GImportantKeywords.Num() == 0)
			{
				GImportantKeywords = {
					TEXT("spawnrate"),
					TEXT("spawn rate"),
					TEXT("spawncount"),
					TEXT("spawn count"),
					TEXT("spawnprobability"),
					TEXT("spawn probability"),
					TEXT("loopduration"),
					TEXT("loop duration"),
					TEXT("lifetime"),
					TEXT("lifetimemin"),
					TEXT("lifetimemax"),
					TEXT("size"),
					TEXT("spritesize"),
					TEXT("sprite size"),
					TEXT("uniformspritesize"),
					TEXT("uniform sprite size"),
					TEXT("scale"),
					TEXT("meshscale"),
					TEXT("mesh scale"),
					TEXT("color"),
					TEXT("opacity"),
					TEXT("alpha"),
					TEXT("hueshift"),
					TEXT("hue shift"),
					TEXT("velocity"),
					TEXT("force"),
				};
			}

			if (GSortPriorityKeywords.Num() == 0)
			{
				GSortPriorityKeywords = {
					TEXT("spawn"),
					TEXT("burst"),
					TEXT("probability"),
					TEXT("loop"),
					TEXT("duration"),
					TEXT("lifetime"),
					TEXT("size"),
					TEXT("scale"),
					TEXT("mesh"),
					TEXT("sprite"),
					TEXT("velocity"),
					TEXT("force"),
					TEXT("color"),
					TEXT("hue"),
					TEXT("opacity"),
					TEXT("alpha"),
				};
			}
		}

		FString Normalize(const FString& In)
		{
			FString Out = In;
			Out.ReplaceInline(TEXT(" "), TEXT(""));
			Out.ReplaceInline(TEXT("_"), TEXT(""));
			return Out.ToLower();
		}

		bool ContainsKeyword(const FString& Haystack, const FString& Keyword)
		{
			return Normalize(Haystack).Contains(Normalize(Keyword));
		}
	}

	void ReloadFromConfig()
	{
		GImportantKeywords.Reset();
		GSortPriorityKeywords.Reset();

		TArray<FString> ImportantLines;
		if (GConfig->GetSection(TEXT("/Script/NiagaraParamExposer.ImportantInputs"), ImportantLines, GEditorIni))
		{
			for (const FString& Line : ImportantLines)
			{
				FString Key;
				FString Value;
				if (Line.Split(TEXT("="), &Key, &Value))
				{
					const FString Token = Value.IsEmpty() ? Key : Value;
					if (!Token.IsEmpty())
					{
						GImportantKeywords.Add(Token);
					}
				}
				else if (!Line.IsEmpty())
				{
					GImportantKeywords.Add(Line);
				}
			}
		}

		TArray<FString> SortLines;
		if (GConfig->GetSection(TEXT("/Script/NiagaraParamExposer.SortPriority"), SortLines, GEditorIni))
		{
			int32 Index = 0;
			for (const FString& Line : SortLines)
			{
				FString Key;
				FString Value;
				FString Token;
				if (Line.Split(TEXT("="), &Key, &Value) && !Value.IsEmpty())
				{
					Token = Value;
				}
				else
				{
					Token = Key.IsEmpty() ? Line : Key;
				}
				if (!Token.IsEmpty())
				{
					GSortPriorityKeywords.Add(Token);
				}
				++Index;
			}
		}

		EnsureDefaults();
	}

	bool IsImportantInputName(const FString& InputName)
	{
		ReloadFromConfig();
		for (const FString& Keyword : GImportantKeywords)
		{
			if (ContainsKeyword(InputName, Keyword))
			{
				return true;
			}
		}
		return false;
	}

	bool ShouldHideFromDefaultView(const FString& TypeName, const FString& InputName)
	{
		if (TypeName.Contains(TEXT("DataInterface"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (TypeName.Contains(TEXT("Enum"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (InputName.Contains(TEXT("Mode"), ESearchCase::IgnoreCase)
			&& !InputName.Contains(TEXT("Model"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		return false;
	}

	bool MatchesSearchFilter(
		const FString& SearchText,
		const FString& ModuleName,
		const FString& InputName,
		const FString& UserParamName)
	{
		if (SearchText.IsEmpty())
		{
			return true;
		}
		const FString Needle = SearchText.ToLower();
		return ModuleName.ToLower().Contains(Needle)
			|| InputName.ToLower().Contains(Needle)
			|| UserParamName.ToLower().Contains(Needle);
	}

	int32 GetUserParameterSortPriority(const FString& ShortUserName, const FString& SourceInputName)
	{
		ReloadFromConfig();
		const FString Combined = ShortUserName + TEXT(" ") + SourceInputName;
		const FString Normalized = Normalize(Combined);

		int32 BestIndex = MAX_int32;
		for (int32 Index = 0; Index < GSortPriorityKeywords.Num(); ++Index)
		{
			if (Normalized.Contains(Normalize(GSortPriorityKeywords[Index])))
			{
				BestIndex = FMath::Min(BestIndex, Index);
			}
		}

		if (BestIndex == MAX_int32)
		{
			// Other forces/settings sit before color and opacity.
			BestIndex = GSortPriorityKeywords.IndexOfByPredicate([](const FString& Keyword)
			{
				return Normalize(Keyword).Contains(TEXT("color"));
			});
			BestIndex = BestIndex == INDEX_NONE ? 11 : FMath::Max(0, BestIndex - 1);
		}

		return BestIndex;
	}

	bool MatchesEmitterSearch(const FString& EmitterName, const FString& SearchText)
	{
		FString Query = SearchText.TrimStartAndEnd();
		return Query.IsEmpty() || EmitterName.Contains(Query, ESearchCase::IgnoreCase);
	}

	bool IsParticleStateLifetime(const UNiagaraNodeFunctionCall& FunctionCall, const FString& InputName)
	{
		return Normalize(InputName) == TEXT("lifetime") &&
			((FunctionCall.FunctionScript && Normalize(FunctionCall.FunctionScript->GetName()) == TEXT("particlestate")) ||
			 Normalize(FunctionCall.GetFunctionName()).StartsWith(TEXT("particlestate")));
	}

	bool ShouldHideModuleInputForDisplay(
		const UNiagaraNodeFunctionCall& FunctionCall,
		const FString& InputName,
		const FString& TypeName,
		const TArray<FString>& AllInputNamesOnModule)
	{
		if (ShouldHideFromDefaultView(TypeName, InputName))
		{
			return true;
		}

		auto HasInput = [&](const FString& Name)
		{
			for (const FString& Existing : AllInputNamesOnModule)
			{
				if (Existing.Equals(Name, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
			return false;
		};

		const FString Normalized = Normalize(InputName);
		if (IsParticleStateLifetime(FunctionCall, InputName)) return true;
		if (Normalized == TEXT("lifetime") || Normalized == TEXT("lifetimemin") || Normalized == TEXT("lifetimemax"))
		{
			// Read the real mode; the existence of Min/Max inputs says nothing about which mode is active.
			for (const UEdGraphPin* Pin : FunctionCall.Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Input || !Normalize(Pin->PinName.ToString()).EndsWith(TEXT("lifetimemode"))) continue;
				FString Mode = Pin->DefaultValue.IsEmpty() ? Pin->AutogeneratedDefaultValue : Pin->DefaultValue;
				if (const UEnum* Enum = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get()))
				{
					int64 Value = 0;
					if (!LexTryParseString(Value, *Mode)) Value = Enum->GetValueByNameString(Mode);
					if (Value != INDEX_NONE) Mode = Enum->GetDisplayNameTextByValue(Value).ToString();
				}
				Mode = Normalize(Mode);
				if (Mode.Contains(TEXT("direct"))) return Normalized != TEXT("lifetime");
				if (Mode.Contains(TEXT("random"))) return Normalized == TEXT("lifetime");
			}
			return false; // Unknown modes stay visible rather than hiding the correct input.
		}

		if (Normalized.Contains(TEXT("uniformspritesizemin")) || Normalized.Contains(TEXT("uniformspritesizemax")))
		{
			if (!HasInput(TEXT("Uniform Sprite Size")) && HasInput(TEXT("Sprite Size")))
			{
				return true;
			}
		}


		(void)FunctionCall;
		return false;
	}
}
