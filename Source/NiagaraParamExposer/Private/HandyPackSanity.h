#pragma once
#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
class SWidget;
namespace HandySanity {
struct FOptions {
 int32 TextureWarning=2048,TriangleWarning=20000;
 bool References=true,Naming=true,Compile=true,Unused=true,Duplicates=true,Textures=true,Meshes=true;
};
struct FRow {
 FString Scope,Asset,Type,Check,Detail,Related;
 int32 Severity=1; // Error, warning, information.
 int32 Tool=-1;
 FString Key()const{return Scope+TEXT("|")+Asset+TEXT("|")+Check+TEXT("|")+Related+TEXT("|")+Detail;}
};
struct FResult {TArray<TSharedPtr<FRow>> Rows;FString Error;int32 Assets=0,Scopes=0;bool Canceled=false;};
void InspectTexture(int32 Width,int32 Height,const FOptions& Options,const FString& Scope,const FAssetData& Asset,TArray<TSharedPtr<FRow>>& Rows);
void InspectMesh(int64 Triangles,const FOptions& Options,const FString& Scope,const FAssetData& Asset,TArray<TSharedPtr<FRow>>& Rows);
FResult Scan(const TArray<FString>& Folders,const TArray<FAssetData>& Assets,const FOptions& Options);
TSharedRef<SWidget> MakePanel(TFunction<void(int32)> OpenTool);
}
