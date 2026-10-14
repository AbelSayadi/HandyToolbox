// Copyright NiagaraParamExposer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UNiagaraNodeFunctionCall;

namespace NiagaraParamExposerInputFilter
{
	void ReloadFromConfig();
	bool IsParticleStateLifetime(const UNiagaraNodeFunctionCall& FunctionCall, const FString& InputName);
	bool MatchesEmitterSearch(const FString& EmitterName, const FString& SearchText);

	bool IsImportantInputName(const FString& InputName);
	bool ShouldHideFromDefaultView(const FString& TypeName, const FString& InputName);
	bool MatchesSearchFilter(const FString& SearchText, const FString& ModuleName, const FString& InputName, const FString& UserParamName);

	/** Lower = earlier in category/list. */
	int32 GetUserParameterSortPriority(const FString& ShortUserName, const FString& SourceInputName = FString());

	/** Best-effort: hide static-switch duplicates for Initialize Particle style modules. */
	bool ShouldHideModuleInputForDisplay(
		const UNiagaraNodeFunctionCall& FunctionCall,
		const FString& InputName,
		const FString& TypeName,
		const TArray<FString>& AllInputNamesOnModule);
}
