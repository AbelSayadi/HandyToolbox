#pragma once
#include "CoreMinimal.h"
class SWidget;
namespace HandyReferences {
struct FRow { FString Scope, Source, Target, Detail, Status, Replacement; bool Outside=false, Selected=false; };
bool Inside(const FString& Package, const FString& Root);
bool IdenticalAssets(UObject* A, UObject* B);
bool IdenticalTextures(UObject* A, UObject* B);
TArray<TSharedPtr<FRow>> Scan(const FString& Root, FString& Report, const TArray<FName>& SeedPackages = {});
bool Relink(const FString& Root, const TArray<TSharedPtr<FRow>>& Rows, FString& Report);
TSharedRef<SWidget> MakePanel();
}
