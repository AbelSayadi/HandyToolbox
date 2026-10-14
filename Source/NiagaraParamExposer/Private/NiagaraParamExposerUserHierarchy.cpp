// Copyright NiagaraParamExposer. All Rights Reserved.

#include "NiagaraParamExposerUserHierarchy.h"
#include "Algo/StableSort.h"
#include "NiagaraParamExposerInputFilter.h"
#include "NiagaraParamExposerLog.h"

#include "NiagaraSystem.h"
#include "NiagaraSystemEditorData.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraUserRedirectionParameterStore.h"

#include "DataHierarchyViewModelBase.h"
#include "ViewModels/HierarchyEditor/NiagaraUserParametersHierarchyViewModel.h"
#include "UObject/UnrealType.h"

namespace NiagaraParamExposerUserHierarchy
{
	namespace
	{
		FString ShortNameOf(FString Name)
		{
			Name.RemoveFromStart(TEXT("User."), ESearchCase::IgnoreCase);
			return Name;
		}

		// Child arrays are authoritative: older versions moved entries without changing Outer.
		void DetachItem(UHierarchyElement& Parent, UHierarchyElement& Item)
		{
			if (Parent.GetChildrenMutable().Contains(&Item))
			{
				Parent.SetFlags(RF_Transactional);
				Parent.Modify();
				Parent.GetChildrenMutable().Remove(&Item);
			}
			const auto Children = Parent.GetChildrenMutable();
			for (UHierarchyElement* Child : Children)
			{
				if (Child) DetachItem(*Child, Item);
			}
		}

		UNiagaraSystemEditorData* GetSystemEditorData(UNiagaraSystem& System)
		{
			return Cast<UNiagaraSystemEditorData>(System.GetEditorData());
		}

		UHierarchyRoot* EnsureUserParameterHierarchyRoot(UNiagaraSystemEditorData& EditorData)
		{
			if (!EditorData.UserParameterHierarchy)
			{
				EditorData.Modify();
				EditorData.UserParameterHierarchy =
					NewObject<UHierarchyRoot>(&EditorData, NAME_None, RF_Transactional);
			}
			return EditorData.UserParameterHierarchy;
		}

		UHierarchyCategory* FindCategoryRecursive(UHierarchyElement& Element, const FName CategoryName)
		{
			TArray<UHierarchyCategory*> Categories;
			Element.GetChildrenOfType<UHierarchyCategory>(Categories, true);
			for (UHierarchyCategory* Category : Categories)
			{
				if (Category && Category->GetCategoryName() == CategoryName)
				{
					return Category;
				}
			}
			return nullptr;
		}

		UHierarchyCategory* FindOrCreateCategory(UHierarchyRoot& Root, const FString& CategoryName)
		{
			const FName CategoryFName(*CategoryName);
			if (UHierarchyCategory* Existing = FindCategoryRecursive(Root, CategoryFName))
			{
				Existing->SetFlags(RF_Transactional);
				return Existing;
			}

			Root.SetFlags(RF_Transactional);
			Root.Modify();
			UHierarchyCategory* NewCategory = Root.AddChild<UHierarchyCategory>();
			if (NewCategory)
			{
				NewCategory->SetFlags(RF_Transactional);
				NewCategory->SetCategoryName(CategoryFName);
			}
			return NewCategory;
		}

		void WireHierarchyUserParameterItem(
			UNiagaraHierarchyUserParameter& Item,
			UNiagaraScriptVariable& ScriptVariable)
		{
			if (FObjectPropertyBase* ScriptVariableProperty = FindFProperty<FObjectPropertyBase>(
					UNiagaraHierarchyUserParameter::StaticClass(),
					TEXT("UserParameterScriptVariable")))
			{
				ScriptVariableProperty->SetObjectPropertyValue_InContainer(&Item, &ScriptVariable);
			}

			const FGuid ScriptVariableGuid = ScriptVariable.Metadata.GetVariableGuid();
			Item.SetIdentity(FHierarchyElementIdentity({ScriptVariableGuid}, {}));
		}

		UNiagaraScriptVariable* FindUserScriptVariableByShortName(
			UNiagaraSystemEditorData& EditorData,
			UNiagaraSystem& System,
			const FString& ShortName)
		{
			TArray<FNiagaraVariable> UserParameters;
			System.GetExposedParameters().GetUserParameters(UserParameters);
			for (const FNiagaraVariable& UserParameter : UserParameters)
			{
				if (!ShortNameOf(UserParameter.GetName().ToString()).Equals(ShortNameOf(ShortName), ESearchCase::IgnoreCase))
				{
					continue;
				}

				FNiagaraVariable UserLookup(UserParameter);
				FNiagaraUserRedirectionParameterStore::MakeUserVariable(UserLookup);
				return EditorData.FindOrAddUserScriptVariable(UserLookup, System);
			}
			return nullptr;
		}

		void CollectExistingCategoryMemberNames(UHierarchyCategory& Category, TSet<FString>& OutNames)
		{
			TArray<UNiagaraHierarchyUserParameter*> Items;
			Category.GetChildrenOfType<UNiagaraHierarchyUserParameter>(Items, false);
			for (UNiagaraHierarchyUserParameter* Item : Items)
			{
				if (Item && Item->GetScriptVariable())
				{
					OutNames.Add(ShortNameOf(Item->GetUserParameter().GetName().ToString()));
				}
			}
		}

		void SortCategoryChildren(UHierarchyCategory& Category)
		{
			TArray<TObjectPtr<UHierarchyElement>>& Children = Category.GetChildrenMutable();
			Algo::StableSort(Children,
				[](const TObjectPtr<UHierarchyElement>& A, const TObjectPtr<UHierarchyElement>& B)
				{
					const UNiagaraHierarchyUserParameter* ParamA = Cast<UNiagaraHierarchyUserParameter>(A.Get());
					const UNiagaraHierarchyUserParameter* ParamB = Cast<UNiagaraHierarchyUserParameter>(B.Get());
					if (!ParamA || !ParamB || !ParamA->GetScriptVariable() || !ParamB->GetScriptVariable())
					{
						return ParamA == nullptr && ParamB != nullptr;
					}

					const FString NameA = ShortNameOf(ParamA->GetUserParameter().GetName().ToString());
					const FString NameB = ShortNameOf(ParamB->GetUserParameter().GetName().ToString());
					const int32 PriorityA = NiagaraParamExposerInputFilter::GetUserParameterSortPriority(NameA.Mid(NameA.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromEnd) + 1));
					const int32 PriorityB = NiagaraParamExposerInputFilter::GetUserParameterSortPriority(NameB.Mid(NameB.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromEnd) + 1));
					if (PriorityA != PriorityB)
					{
						return PriorityA < PriorityB;
					}
					FString PairA = NameA, PairB = NameB;
					const int32 RankA = PairA.RemoveFromEnd(TEXT("Min")) ? 1 : (PairA.RemoveFromEnd(TEXT("Max")) ? 2 : 0);
					const int32 RankB = PairB.RemoveFromEnd(TEXT("Min")) ? 1 : (PairB.RemoveFromEnd(TEXT("Max")) ? 2 : 0);
					const int32 PairComparison = PairA.Compare(PairB, ESearchCase::IgnoreCase);
					if (PairComparison != 0) return PairComparison < 0;
					if (RankA != RankB) return RankA < RankB;
					return NameA.Compare(NameB, ESearchCase::CaseSensitive) < 0;
				});
		}

		bool EnsureHierarchyItemInCategory(
			UHierarchyRoot& Root,
			UHierarchyCategory& Category,
			UNiagaraSystemEditorData& EditorData,
			UNiagaraSystem& System,
			const FString& ShortName)
		{
			UNiagaraScriptVariable* ScriptVariable = FindUserScriptVariableByShortName(EditorData, System, ShortName);
			if (!ScriptVariable)
			{
				return false;
			}

			// Recreate only this parameter's hierarchy entry under the proper owner.
			// This also removes duplicate entries left by the old reparent implementation.
			TArray<UNiagaraHierarchyUserParameter*> Items;
			Root.GetChildrenOfType<UNiagaraHierarchyUserParameter>(Items, true);
			UNiagaraHierarchyUserParameter* ItemToKeep = nullptr;
			for (UNiagaraHierarchyUserParameter* Item : Items)
			{
				if (!Item || !Item->GetScriptVariable() ||
					!ShortNameOf(Item->GetUserParameter().GetName().ToString()).Equals(ShortName, ESearchCase::IgnoreCase)) continue;
				if (!ItemToKeep && Item->GetOuter() == &Category)
				{
					ItemToKeep = Item;
				}
				DetachItem(Root, *Item);
			}
			Category.Modify();
			if (!ItemToKeep)
			{
				ItemToKeep = NewObject<UNiagaraHierarchyUserParameter>(&Category, NAME_None, RF_Transactional);
			}
			ItemToKeep->SetFlags(RF_Transactional);
			ItemToKeep->Modify();
			WireHierarchyUserParameterItem(*ItemToKeep, *ScriptVariable);
			Category.GetChildrenMutable().AddUnique(ItemToKeep);
			return true;
		}
	}

	bool AssignUserParametersToCategory(
		UNiagaraSystem& System,
		const FString& CategoryName,
		const TArray<FString>& UserParameterShortNames,
		FString& OutError)
	{
		if (CategoryName.IsEmpty())
		{
			OutError = TEXT("CategoryName is empty.");
			return false;
		}

		UNiagaraSystemEditorData* EditorData = GetSystemEditorData(System);
		if (!EditorData)
		{
			OutError = TEXT("UNiagaraSystemEditorData is missing.");
			return false;
		}

		EditorData->SetFlags(RF_Transactional);
		EditorData->Modify();
		System.Modify();

		UHierarchyRoot* Root = EnsureUserParameterHierarchyRoot(*EditorData);
		if (!Root)
		{
			OutError = TEXT("Failed to create UserParameterHierarchy root.");
			return false;
		}

		UHierarchyCategory* Category = FindOrCreateCategory(*Root, CategoryName);
		if (!Category)
		{
			OutError = TEXT("Failed to find or create hierarchy category.");
			return false;
		}

		TSet<FString> AllShortNames;
		CollectExistingCategoryMemberNames(*Category, AllShortNames);
		const int32 ExistingInCategory = AllShortNames.Num();
		for (const FString& ShortName : UserParameterShortNames)
		{
			if (!ShortName.IsEmpty())
			{
				AllShortNames.Add(ShortNameOf(ShortName));
			}
		}

		UE_LOG(
			LogNiagaraParamExposer,
			Display,
			TEXT("AssignUserParametersToCategory: category='%s' existing=%d requested=%d union=%d"),
			*CategoryName,
			ExistingInCategory,
			UserParameterShortNames.Num(),
			AllShortNames.Num());

		int32 AssignedCount = 0;
		for (const FString& ShortName : AllShortNames)
		{
			if (EnsureHierarchyItemInCategory(*Root, *Category, *EditorData, System, ShortName))
			{
				++AssignedCount;
			}
			else
			{
				UE_LOG(
					LogNiagaraParamExposer,
					Warning,
					TEXT("AssignUserParametersToCategory: could not place User.%s in category '%s'"),
					*ShortName,
					*CategoryName);
			}
		}

		Category->Modify();
		SortCategoryChildren(*Category);

		TArray<UNiagaraHierarchyUserParameter*> SortedItems;
		Category->GetChildrenOfType<UNiagaraHierarchyUserParameter>(SortedItems, false);
		for (int32 Index = 0; Index < SortedItems.Num(); ++Index)
		{
			if (const UNiagaraHierarchyUserParameter* Item = SortedItems[Index])
			{
				UE_LOG(
					LogNiagaraParamExposer,
					Display,
					TEXT("AssignUserParametersToCategory: category='%s' order[%d]=%s"),
					*CategoryName,
					Index,
					*ShortNameOf(Item->GetUserParameter().GetName().ToString()));
			}
		}

		if (AssignedCount != AllShortNames.Num() || AssignedCount == 0)
		{
			OutError = TEXT("One or more requested user parameters could not be assigned to the category.");
			return false;
		}

		return true;
	}

	void PruneStaleUserParameterHierarchy(UNiagaraSystem& System)
	{
		UNiagaraSystemEditorData* EditorData = GetSystemEditorData(System);
		if (!EditorData || !EditorData->UserParameterHierarchy)
		{
			return;
		}

		TSet<FString> LiveShortNames;
		TArray<FNiagaraVariable> UserParameters;
		System.GetExposedParameters().GetUserParameters(UserParameters);
		for (const FNiagaraVariable& UserParameter : UserParameters)
		{
			LiveShortNames.Add(ShortNameOf(UserParameter.GetName().ToString()).ToLower());
		}

		TArray<UNiagaraHierarchyUserParameter*> Items;
		EditorData->UserParameterHierarchy->GetChildrenOfType<UNiagaraHierarchyUserParameter>(Items, true);
		int32 RemovedCount = 0;
		for (UNiagaraHierarchyUserParameter* Item : Items)
		{
			if (!Item) continue;
			if (!Item->GetScriptVariable())
			{
				DetachItem(*EditorData->UserParameterHierarchy, *Item);
				++RemovedCount;
				continue;
			}

			const FString ShortName = ShortNameOf(Item->GetUserParameter().GetName().ToString());
			if (LiveShortNames.Contains(ShortName.ToLower()))
			{
				if (UNiagaraScriptVariable* Variable = FindUserScriptVariableByShortName(*EditorData, System, ShortName))
				{
					Item->SetFlags(RF_Transactional);
					Item->Modify();
					WireHierarchyUserParameterItem(*Item, *Variable);
				}
				continue;
			}

			EditorData->SetFlags(RF_Transactional);
		EditorData->Modify();
			DetachItem(*EditorData->UserParameterHierarchy, *Item);
			++RemovedCount;
		}

		if (RemovedCount > 0)
		{
			UE_LOG(
				LogNiagaraParamExposer,
				Display,
				TEXT("PruneStaleUserParameterHierarchy: removed %d stale item(s)"),
				RemovedCount);
		}
	}

	TArray<FString> ListUserParameterCategoryNames(UNiagaraSystem& System)
	{
		TArray<FString> Out;
		UNiagaraSystemEditorData* EditorData = GetSystemEditorData(System);
		if (!EditorData || !EditorData->UserParameterHierarchy)
		{
			return Out;
		}

		TArray<UHierarchyCategory*> Categories;
		EditorData->UserParameterHierarchy->GetChildrenOfType<UHierarchyCategory>(Categories, true);
		for (UHierarchyCategory* Category : Categories)
		{
			if (Category)
			{
				Out.AddUnique(Category->GetCategoryName().ToString());
			}
		}
		return Out;
	}
	FString CategoryForParameter(const FString& ParameterName, const FString& Fallback)
	{
		const FString Name = ShortNameOf(ParameterName);
		int32 Separator;
		if (Name.FindLastChar(TEXT('_'), Separator) && Separator > 0 && Separator < Name.Len() - 1)
		{
			return Name.Left(Separator);
		}
		return Fallback;
	}

	bool RepairEmitterCategories(UNiagaraSystem& System, FString& OutError)
	{
		PruneStaleUserParameterHierarchy(System);
		TArray<FNiagaraVariable> Parameters;
		System.GetExposedParameters().GetUserParameters(Parameters);
		TMap<FString, TArray<FString>> Groups;
		for (const FNiagaraVariable& Parameter : Parameters)
		{
			const FString Name = ShortNameOf(Parameter.GetName().ToString());
			const FString Category = CategoryForParameter(Name, FString());
			if (!Category.IsEmpty()) Groups.FindOrAdd(Category).Add(Name);
		}
		bool bSuccess = true;
		for (const auto& Group : Groups)
		{
			FString Error;
			if (!AssignUserParametersToCategory(System, Group.Key, Group.Value, Error))
			{
				bSuccess = false;
				OutError += Group.Key + TEXT(": ") + Error + TEXT("\n");
			}
		}
		if (UNiagaraSystemEditorData* EditorData = GetSystemEditorData(System))
		{
			System.MarkPackageDirty();
			EditorData->OnUserParameterScriptVariablesSynced().Broadcast();
		}
		else { OutError = TEXT("System editor data is missing."); return false; }
		return bSuccess;
	}

	TArray<FString> GetCategoryAssignments(UNiagaraSystem& System)
	{
		TArray<FString> Result;
		UNiagaraSystemEditorData* EditorData = GetSystemEditorData(System);
		if (!EditorData || !EditorData->UserParameterHierarchy) return Result;
		TFunction<void(UHierarchyElement&)> Visit = [&](UHierarchyElement& Parent)
		{
			for (UHierarchyElement* Child : Parent.GetChildrenMutable())
			{
				if (const UNiagaraHierarchyUserParameter* Item = Cast<UNiagaraHierarchyUserParameter>(Child))
				{
					const UNiagaraScriptVariable* Variable = Item->GetScriptVariable();
					const UHierarchyCategory* Category = Cast<UHierarchyCategory>(&Parent);
					const bool bValid = Variable && Item->GetPersistentIdentity() ==
						FHierarchyElementIdentity({Variable->Metadata.GetVariableGuid()}, {}) &&
						EditorData->FindUserScriptVariable(Variable->Metadata.GetVariableGuid()) == Variable &&
						Item->GetOuter() == &Parent;
					Result.Add(FString::Printf(TEXT("%s|%s|%s"),
						Variable ? *ShortNameOf(Variable->Variable.GetName().ToString()) : TEXT("<invalid>"),
						Category ? *Category->GetCategoryName().ToString() : TEXT(""), bValid ? TEXT("valid") : TEXT("invalid")));
				}
				else if (Child) Visit(*Child);
			}
		};
		Visit(*EditorData->UserParameterHierarchy);
		return Result;
	}

}
