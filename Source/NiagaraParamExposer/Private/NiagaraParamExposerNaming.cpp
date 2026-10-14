// Copyright NiagaraParamExposer. All Rights Reserved.

#include "NiagaraParamExposerNaming.h"

#include "Misc/ConfigCacheIni.h"

namespace NiagaraParamExposerNaming
{
	namespace
	{
		struct FPhraseReplacement
		{
			FString From;
			FString To;
		};

		TArray<FPhraseReplacement> GPhraseReplacements;

		void EnsureDefaultRules()
		{
			if (GPhraseReplacements.Num() > 0)
			{
				return;
			}

			GPhraseReplacements = {
				{TEXT("Uniform Sprite Size Min"), TEXT("SizeMin")},
				{TEXT("Uniform Sprite Size Max"), TEXT("SizeMax")},
				{TEXT("Sprite Size Min"), TEXT("SizeMin")},
				{TEXT("Sprite Size Max"), TEXT("SizeMax")},
				{TEXT("Lifetime Min"), TEXT("LifetimeMin")},
				{TEXT("Lifetime Max"), TEXT("LifetimeMax")},
				{TEXT("Spawn Rate"), TEXT("Spawnrate")},
			};
		}

		FString ToPascalCaseNoSpaces(FString In)
		{
			In.TrimStartAndEndInline();
			In.ReplaceInline(TEXT(" "), TEXT(""));
			In.ReplaceInline(TEXT("_"), TEXT(""));
			if (In.IsEmpty())
			{
				return In;
			}
			return FName(*In).ToString();
		}
	}

	void ReloadRulesFromConfig()
	{
		GPhraseReplacements.Reset();
		TArray<FString> Lines;
		if (GConfig->GetSection(TEXT("/Script/NiagaraParamExposer.Naming"), Lines, GEditorIni))
		{
			for (const FString& Line : Lines)
			{
				FString Key;
				FString Value;
				if (Line.Split(TEXT("="), &Key, &Value))
				{
					Key.TrimStartAndEndInline();
					Value.TrimStartAndEndInline();
					if (!Key.IsEmpty() && !Value.IsEmpty())
					{
						GPhraseReplacements.Add({Key, Value});
					}
				}
			}
		}
		EnsureDefaultRules();
	}

	FString MakeShortInputToken(const FString& RawInputName)
	{
		ReloadRulesFromConfig();
		FString Working = RawInputName;
		Working.TrimStartAndEndInline();

		for (const FPhraseReplacement& Rule : GPhraseReplacements)
		{
			if (Working.Equals(Rule.From, ESearchCase::IgnoreCase))
			{
				return ToPascalCaseNoSpaces(Rule.To);
			}
		}

		Working.ReplaceInline(TEXT("Uniform Sprite "), TEXT(""));
		Working.ReplaceInline(TEXT("Sprite "), TEXT(""));
		return ToPascalCaseNoSpaces(Working);
	}

	FString MakeAutoUserParameterName(const FString& EmitterName, const FString& RawInputName)
	{
		const FString EmitterToken = ToPascalCaseNoSpaces(EmitterName);
		const FString InputToken = MakeShortInputToken(RawInputName);
		return FString::Printf(TEXT("%s_%s"), *EmitterToken, *InputToken);
	}
}
