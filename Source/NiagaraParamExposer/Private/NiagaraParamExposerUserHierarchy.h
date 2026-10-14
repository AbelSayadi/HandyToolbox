// Copyright NiagaraParamExposer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UNiagaraSystem;
class UNiagaraScriptVariable;

namespace NiagaraParamExposerUserHierarchy
{
	bool AssignUserParametersToCategory(
		UNiagaraSystem& System,
		const FString& CategoryName,
		const TArray<FString>& UserParameterShortNames,
		FString& OutError);

	TArray<FString> ListUserParameterCategoryNames(UNiagaraSystem& System);

	/** Remove hierarchy user-parameter items that no longer exist on the system's exposed store. */
	void PruneStaleUserParameterHierarchy(UNiagaraSystem& System);
	FString CategoryForParameter(const FString& ParameterName, const FString& Fallback);
	bool RepairEmitterCategories(UNiagaraSystem& System, FString& OutError);
	TArray<FString> GetCategoryAssignments(UNiagaraSystem& System);
}
