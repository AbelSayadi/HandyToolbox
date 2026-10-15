#pragma once
#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
class SWidget;
namespace HandyNaming {
struct FRule { FString Type,Prefix,Suffix;bool Enabled=true; };
struct FRow { FAssetData Asset;FString Type,Proposed,Status;bool Selected=false,Ready=false; };
struct FHistory { FString OldPath,NewPath; };
TArray<TSharedPtr<FRule>> Defaults();
FString TypeOf(const FAssetData& Asset);
FString Propose(const FString& Name,const FRule& Rule,const TArray<FString>& Prefixes,const TArray<FString>& Suffixes);
TArray<TSharedPtr<FRow>> Preview(const TArray<FAssetData>& Assets,const TArray<TSharedPtr<FRule>>& Rules,const TArray<FString>& Prefixes,const TArray<FString>& Suffixes);
bool Apply(const TArray<TSharedPtr<FRow>>& Rows,TArray<FHistory>& History,FString& Report);
bool Revert(TArray<FHistory>& History,FString& Report);
TSharedRef<SWidget> MakePanel();
}
