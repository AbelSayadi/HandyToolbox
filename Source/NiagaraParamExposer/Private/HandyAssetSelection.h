#pragma once
#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
namespace HandySelection {
bool IsProjectPath(const FString& Path);
TArray<FString> SelectedFolders();
TArray<FAssetData> SelectedAssets();
TArray<FAssetData> Gather(const TArray<FString>& Folders,const TArray<FAssetData>& Assets);
bool HasUnsavedContent(FString& Error);
}
