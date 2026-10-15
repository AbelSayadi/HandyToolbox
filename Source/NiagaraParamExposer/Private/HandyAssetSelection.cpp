#include "HandyAssetSelection.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "IContentBrowserDataModule.h"
#include "ContentBrowserDataSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "UObject/Package.h"
namespace HandySelection {
bool IsProjectPath(const FString& P){return (P==TEXT("/Game")||P.StartsWith(TEXT("/Game/")))&&!P.Contains(TEXT(".."));}
TArray<FString> SelectedFolders(){TArray<FString> V;FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().GetSelectedPathViewFolders(V);return IContentBrowserDataModule::Get().GetSubsystem()->TryConvertVirtualPathsToInternal(V);}
TArray<FAssetData> SelectedAssets(){TArray<FAssetData> A;FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().GetSelectedAssets(A);A.RemoveAll([](const FAssetData& D){return !IsProjectPath(D.PackageName.ToString());});return A;}
TArray<FAssetData> Gather(const TArray<FString>& Folders,const TArray<FAssetData>& Assets){
    auto& R=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();TMap<FSoftObjectPath,FAssetData> Unique;
    for(const FString& F:Folders)if(IsProjectPath(F)){TArray<FAssetData> Found;R.GetAssetsByPath(FName(*F),Found,true);for(const auto& A:Found)Unique.Add(A.GetSoftObjectPath(),A);}
    for(const auto& A:Assets)if(IsProjectPath(A.PackageName.ToString())){auto Fresh=R.GetAssetByObjectPath(A.GetSoftObjectPath());if(Fresh.IsValid())Unique.Add(Fresh.GetSoftObjectPath(),Fresh);}
    TArray<FAssetData> Result;Unique.GenerateValueArray(Result);Result.Sort([](const FAssetData& A,const FAssetData& B){return A.PackageName.LexicalLess(B.PackageName);});return Result;
}
bool HasUnsavedContent(FString& Error){TArray<UPackage*> Dirty;FEditorFileUtils::GetDirtyPackages(Dirty);for(UPackage* P:Dirty)if(P&&!P->HasAnyFlags(RF_Transient)){Error=TEXT("Save or discard unsaved assets/maps before continuing: ")+P->GetName();return true;}return false;}
}
