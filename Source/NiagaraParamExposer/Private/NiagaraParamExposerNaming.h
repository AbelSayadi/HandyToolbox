// Copyright NiagaraParamExposer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Extensible short-name rules for user parameters (see DefaultEditor.ini). */
namespace NiagaraParamExposerNaming
{
	/** Strip spaces / prefixes and apply phrase replacements (e.g. Uniform Sprite Size Min -> SizeMin). */
	FString MakeShortInputToken(const FString& RawInputName);

	/** Default auto name: EmitterName_ShortToken (PascalCase-ish, no spaces). */
	FString MakeAutoUserParameterName(const FString& EmitterName, const FString& RawInputName);

	void ReloadRulesFromConfig();
}
