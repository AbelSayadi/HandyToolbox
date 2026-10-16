#include "HandyPackSanity.h"
#include "HandyAssetSelection.h"
#include "HandyAssetCleanup.h"
#include "HandyReferenceChecker.h"
#include "HandyNaming.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "MaterialShared.h"
#include "NiagaraSystem.h"
#include "NiagaraScript.h"
#include "UObject/UObjectHash.h"
#include "Misc/PackageName.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopedSlowTask.h"
namespace HandySanity {
static void Add(TArray<TSharedPtr<FRow>>& Rows,const FString& Scope,const FString& Asset,const FString& Type,const TCHAR* Check,const FString& Detail,int32 Severity=1,const FString& Related=FString(),int32 Tool=-1){
 auto R=MakeShared<FRow>();R->Scope=Scope;R->Asset=Asset;R->Type=Type;R->Check=Check;R->Detail=Detail;R->Severity=Severity;R->Related=Related;R->Tool=Tool;Rows.Add(R);
}
void InspectTexture(int32 W,int32 H,const FOptions& O,const FString& Scope,const FAssetData& A,TArray<TSharedPtr<FRow>>& Rows){
 const FString Path=A.GetObjectPathString(),Type=A.AssetClassPath.GetAssetName().ToString();
 if(W<=0||H<=0){Add(Rows,Scope,Path,Type,TEXT("Texture dimensions unavailable"),TEXT("No readable source or platform dimensions. Open the texture to inspect it."),2);return;}
 if(W>=O.TextureWarning||H>=O.TextureWarning)Add(Rows,Scope,Path,Type,TEXT("Texture resolution"),FString::Printf(TEXT("%d x %d source pixels; review threshold %d. Large flipbook atlases may be intentional. This is a size warning, not a memory/performance measurement."),W,H,O.TextureWarning));
 if(!FMath::IsPowerOfTwo(W)||!FMath::IsPowerOfTwo(H))Add(Rows,Scope,Path,Type,TEXT("Texture power of two"),FString::Printf(TEXT("%d x %d: one or both dimensions are not powers of two. Rectangular power-of-two textures are allowed. Review intentional atlas/UI exceptions."),W,H));
}
void InspectMesh(int64 Triangles,const FOptions& O,const FString& Scope,const FAssetData& A,TArray<TSharedPtr<FRow>>& Rows){
 if(Triangles<0)Add(Rows,Scope,A.GetObjectPathString(),A.AssetClassPath.GetAssetName().ToString(),TEXT("Mesh data unavailable"),TEXT("LOD0 geometry is unavailable or still building. Open the mesh and scan again."),2);
 else if(Triangles>O.TriangleWarning)Add(Rows,Scope,A.GetObjectPathString(),A.AssetClassPath.GetAssetName().ToString(),TEXT("Mesh triangles"),FString::Printf(TEXT("LOD0: %lld triangles; review limit %d. Cost also depends on particle instances, materials, LOD and Nanite settings. This is not a measured performance budget."),Triangles,O.TriangleWarning));
}
static TArray<TSharedPtr<HandyNaming::FRule>> NamingRules(){
 auto Rules=HandyNaming::Defaults();bool Configured=false;GConfig->GetBool(TEXT("HandyToolbox.Naming"),TEXT("RulesConfigured"),Configured,GEditorPerProjectIni);TArray<FString> Saved;
 bool Found=GConfig->GetArray(TEXT("HandyToolbox.Naming"),TEXT("Rules"),Saved,GEditorPerProjectIni)>0;
 if(Configured||Found){Rules.Reset();for(const auto& Line:Saved){TArray<FString> Parts;Line.ParseIntoArray(Parts,TEXT("|"),false);if(Parts.Num()==4){auto R=MakeShared<HandyNaming::FRule>();R->Type=Parts[0];R->Prefix=Parts[1];R->Suffix=Parts[2];R->Enabled=Parts[3]==TEXT("1");Rules.Add(R);}}}return Rules;
}
static void CompileCheck(UObject* Object,const FAssetData& A,const FString& Scope,TArray<TSharedPtr<FRow>>& Rows){
 const FString Path=A.GetObjectPathString(),Type=A.AssetClassPath.GetAssetName().ToString();
 if(auto* System=Cast<UNiagaraSystem>(Object)){
  if(System->HasOutstandingCompilationRequests(true)){Add(Rows,Scope,Path,Type,TEXT("Niagara compile pending"),TEXT("Compilation is running; wait for completion and scan again."));return;}
  TArray<UObject*> Owned;GetObjectsWithOuter(System,Owned,true);int32 Scripts=0;
  for(auto* O:Owned)if(auto* Script=Cast<UNiagaraScript>(O)){
   ++Scripts;auto Status=Script->GetLastCompileStatus();FString Where=Script->GetPathName(System);
   if(Status==ENiagaraScriptCompileStatus::NCS_Error)Add(Rows,Scope,Path,Type,TEXT("Niagara compile error"),Where+TEXT(": open the system for the compiler diagnostic."),0);
   else if(Status==ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings||Status==ENiagaraScriptCompileStatus::NCS_ComputeUpToDateWithWarnings)Add(Rows,Scope,Path,Type,TEXT("Niagara compile warning"),Where+TEXT(": compiler warnings require review."));
   else if(Status==ENiagaraScriptCompileStatus::NCS_Dirty||Status==ENiagaraScriptCompileStatus::NCS_BeingCreated)Add(Rows,Scope,Path,Type,TEXT("Niagara compile pending"),Where+TEXT(": compile in the Niagara editor and scan again."));
  }
  if(!Scripts||!System->IsValid())Add(Rows,Scope,Path,Type,TEXT("Niagara validation"),TEXT("The system has no inspectable scripts or is not currently valid. Open and compile it; cached status alone cannot establish readiness."));
 }
 if(auto* Material=Cast<UMaterialInterface>(Object)){
  if(auto* MI=Cast<UMaterialInstance>(Object))if(!MI->Parent)Add(Rows,Scope,Path,Type,TEXT("Missing material parent"),TEXT("Material instance has no parent material."),0);
  auto* Resource=Material->GetMaterialResource(GMaxRHIFeatureLevel);
  if(!Resource||!Resource->IsCompilationFinished())Add(Rows,Scope,Path,Type,TEXT("Material compile pending"),TEXT("No completed shader result for the current editor feature level. Open the material and scan again."),2);
  else for(const auto& Error:Resource->GetCompileErrors())Add(Rows,Scope,Path,Type,TEXT("Material compile error"),Error,0);
 }
}
FResult Scan(const TArray<FString>& Folders,const TArray<FAssetData>& Explicit,const FOptions& Options){
 FResult Result;
 if(Options.TextureWarning<1||Options.TriangleWarning<1){Result.Error=TEXT("Texture and triangle warning limits must be positive.");return Result;}
 if(Folders.IsEmpty()&&Explicit.IsEmpty()){Result.Error=TEXT("Select Content folders or assets first.");return Result;}
 for(const auto& F:Folders)if(!HandySelection::IsProjectPath(F)){Result.Error=TEXT("Choose project Content folders (/Game).");return Result;}
 if(HandySelection::HasUnsavedContent(Result.Error))return Result;
 auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();Registry.SearchAllAssets(true);
 TMap<FString,TArray<FAssetData>> Scopes;
 for(const auto& F:Folders)Scopes.Add(F,HandySelection::Gather({F},{}));
 for(const auto& A:Explicit){bool Covered=false;for(const auto& F:Folders)Covered|=HandyReferences::Inside(A.PackageName.ToString(),F);if(!Covered&&HandySelection::IsProjectPath(A.PackageName.ToString()))Scopes.FindOrAdd(A.PackagePath.ToString()).AddUnique(A);}
 auto Rules=NamingRules();TArray<FString> Prefixes,Suffixes;GConfig->GetArray(TEXT("HandyToolbox.Naming"),TEXT("KnownPrefixes"),Prefixes,GEditorPerProjectIni);GConfig->GetArray(TEXT("HandyToolbox.Naming"),TEXT("KnownSuffixes"),Suffixes,GEditorPerProjectIni);
 FScopedSlowTask Progress(Scopes.Num(),FText::FromString(TEXT("Checking pack contents...")));Progress.MakeDialog(true);
 for(const auto& Scope:Scopes){
  if(Progress.ShouldCancel()){Result.Canceled=true;break;}Progress.EnterProgressFrame(1,FText::FromString(Scope.Key));++Result.Scopes;Result.Assets+=Scope.Value.Num();
  if(Scope.Value.IsEmpty()){Add(Result.Rows,Scope.Key,FString(),FString(),TEXT("Empty scope"),TEXT("No registered assets found in this folder."),2);continue;}
  if(Options.Naming)for(const auto& R:HandyNaming::Preview(Scope.Value,Rules,Prefixes,Suffixes)){
   if(R->Status==TEXT("Already correct"))continue;
   Add(Result.Rows,Scope.Key,R->Asset.GetObjectPathString(),R->Type,TEXT("Naming"),R->Proposed!=R->Asset.AssetName.ToString()?TEXT("Suggested: ")+R->Proposed+TEXT(". ")+R->Status:R->Status,R->Status==TEXT("No enabled rule")?2:1,FString(),3);
  }
  if(Options.References){
   TArray<FName> Seeds;for(const auto& A:Scope.Value)Seeds.AddUnique(A.PackageName);FString Report;auto Refs=HandyReferences::Scan(Scope.Key,Report,Seeds);
   if(Refs.IsEmpty()&&!Report.StartsWith(TEXT("0 reference locations")))Add(Result.Rows,Scope.Key,FString(),FString(),TEXT("Reference check incomplete"),Report,1,FString(),2);
   for(const auto& R:Refs){
    FString Package=FPackageName::ObjectPathToPackageName(R->Target);bool Missing=!Package.StartsWith(TEXT("/Script/"))&&!FPackageName::DoesPackageExist(Package);
    if(Missing)Add(Result.Rows,Scope.Key,R->Source,FString(),TEXT("Missing dependency"),R->Detail,0,R->Target,2);
    else if(R->Outside)Add(Result.Rows,Scope.Key,R->Source,FString(),TEXT("External reference"),R->Detail+(R->Replacement.IsEmpty()?FString():TEXT(" | Local match: ")+R->Replacement),1,R->Target,2);
   }
  }
  if(Options.Unused){
   auto Cleanup=HandyAssetCleanup::Scan({},Scope.Value);
   if(!Cleanup.Error.IsEmpty())Add(Result.Rows,Scope.Key,FString(),FString(),TEXT("Unused check incomplete"),Cleanup.Error,1,FString(),1);
   for(const auto& R:Cleanup.Rows)if(R->bEligible)Add(Result.Rows,Scope.Key,R->Asset.GetObjectPathString(),R->Asset.AssetClassPath.GetAssetName().ToString(),TEXT("Unused content"),TEXT("Candidate only: no saved Niagara or retained project references found. Review runtime/code-based loads in Asset Cleanup before deletion."),1,FString(),1);
  }
  for(const auto& A:Scope.Value){
   if(Progress.ShouldCancel()){Result.Canceled=true;break;}
   UObject* Object=A.GetAsset();if(!Object){Add(Result.Rows,Scope.Key,A.GetObjectPathString(),A.AssetClassPath.GetAssetName().ToString(),TEXT("Asset load failed"),TEXT("Registered asset could not be loaded."),0);continue;}
   if(A.IsRedirector())Add(Result.Rows,Scope.Key,A.GetObjectPathString(),TEXT("ObjectRedirector"),TEXT("Redirector"),TEXT("Review redirectors before distributing this pack."),1,FString(),3);
   if(Options.Textures)if(auto* T=Cast<UTexture>(Object))InspectTexture(T->Source.IsValid()?T->Source.GetSizeX():static_cast<int32>(T->GetSurfaceWidth()),T->Source.IsValid()?T->Source.GetSizeY():static_cast<int32>(T->GetSurfaceHeight()),Options,Scope.Key,A,Result.Rows);
   if(Options.Meshes){
    if(auto* M=Cast<UStaticMesh>(Object))InspectMesh(M->GetNumLODs()>0?M->GetNumTriangles(0):-1,Options,Scope.Key,A,Result.Rows);
    if(auto* M=Cast<USkeletalMesh>(Object)){auto* Data=M->GetResourceForRendering();int64 Tri=-1;if(Data&&!Data->LODRenderData.IsEmpty()){Tri=0;for(const auto& S:Data->LODRenderData[0].RenderSections)Tri+=S.NumTriangles;}InspectMesh(Tri,Options,Scope.Key,A,Result.Rows);}
   }
   if(Options.Compile)CompileCheck(Object,A,Scope.Key,Result.Rows);
  }
  if(Result.Canceled)break;
  if(Options.Duplicates){
   int32 Comparisons=0;bool Limited=false;
   for(int32 I=0;I<Scope.Value.Num()&&!Limited;++I)for(int32 J=I+1;J<Scope.Value.Num();++J){
    if(Progress.ShouldCancel()){Result.Canceled=true;Limited=true;break;}
    const auto& A=Scope.Value[I];const auto& B=Scope.Value[J];if(A.AssetClassPath!=B.AssetClassPath)continue;
    const FName C=A.AssetClassPath.GetAssetName();if(C!=TEXT("Texture2D")&&C!=TEXT("Material")&&C!=TEXT("MaterialInstanceConstant")&&C!=TEXT("MaterialFunction"))continue;
    if(++Comparisons>50000){Add(Result.Rows,Scope.Key,FString(),FString(),TEXT("Duplicate check incomplete"),TEXT("Comparison limit reached. Scan smaller folders to finish duplicate checking."));Limited=true;break;}
    if(HandyReferences::IdenticalAssets(A.GetAsset(),B.GetAsset()))Add(Result.Rows,Scope.Key,A.GetObjectPathString(),C.ToString(),TEXT("Verified duplicate"),TEXT("Content and supported settings match. Review uses before consolidating. No assets were replaced."),1,B.GetObjectPathString(),2);
   }
  }
 }
 TSet<FString> Keys;Result.Rows.RemoveAll([&](const auto& R){if(Keys.Contains(R->Key()))return true;Keys.Add(R->Key());return false;});
 Result.Rows.Sort([](const auto& A,const auto& B){if(A->Severity!=B->Severity)return A->Severity<B->Severity;if(A->Scope!=B->Scope)return A->Scope<B->Scope;return A->Asset<B->Asset;});
 return Result;
}
}
