#pragma once
#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
class SWidget;
namespace HandyAssetCleanup
{
struct FRow
{
    FAssetData Asset;
    FString Reason;
    bool bEligible = false;
    bool bSelected = false;
};
struct FScan
{
    TArray<TSharedPtr<FRow>> Rows;
    FString Error;
    int32 NiagaraSystems = 0;
};
using FEdges = TMap<FName, TArray<FName>>;
TSet<FName> Reachable(const TSet<FName>& Roots, const FEdges& Dependencies);
TSet<FName> SafeSubset(TSet<FName> Candidates, const FEdges& Referencers);
FScan Scan(const TArray<FString>& Folders, const TArray<FAssetData>& ExplicitAssets = {});
TSharedRef<SWidget> MakePanel();
}
