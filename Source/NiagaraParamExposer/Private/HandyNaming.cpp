#include "HandyNaming.h"
#include "HandyAssetSelection.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/ObjectRedirector.h"
#include "Misc/PackageName.h"
#include "NiagaraScript.h"
#include "Engine/Texture.h"
namespace HandyNaming {
TArray<TSharedPtr<FRule>> Defaults(){
    TArray<TSharedPtr<FRule>> R;
    auto Add=[&](const TCHAR* Type,const TCHAR* Prefix){auto V=MakeShared<FRule>();V->Type=Type;V->Prefix=Prefix;R.Add(V);};
    Add(TEXT("MaterialInstanceConstant"),TEXT("MI_"));Add(TEXT("Material"),TEXT("M_"));Add(TEXT("MaterialFunction"),TEXT("MF_"));
    Add(TEXT("Texture"),TEXT("T_"));Add(TEXT("NiagaraSystem"),TEXT("NS_"));Add(TEXT("StaticMesh"),TEXT("SM_"));
    Add(TEXT("CurveLinearColor"),TEXT("CC_"));Add(TEXT("CurveLinearColorAtlas"),TEXT("CA_"));Add(TEXT("NiagaraModule"),TEXT("NM_"));
    Add(TEXT("NiagaraFunction"),TEXT("NF_"));Add(TEXT("NiagaraDynamicInput"),TEXT("NDI_"));Add(TEXT("NiagaraEmitter"),TEXT("NE_"));
    Add(TEXT("SkeletalMesh"),TEXT("SK_"));Add(TEXT("Skeleton"),TEXT("SKEL_"));Add(TEXT("PhysicsAsset"),TEXT("PHYS_"));
    Add(TEXT("CurveFloat"),TEXT("CF_"));Add(TEXT("CurveVector"),TEXT("CV_"));Add(TEXT("MaterialParameterCollection"),TEXT("MPC_"));
    return R;
}
FString TypeOf(const FAssetData& A){
    FString C=A.AssetClassPath.GetAssetName().ToString();
    if(C==TEXT("NiagaraScript")){auto* S=Cast<UNiagaraScript>(A.GetAsset());if(!S)return C;switch(S->GetUsage()){
        case ENiagaraScriptUsage::Module:return TEXT("NiagaraModule");case ENiagaraScriptUsage::Function:return TEXT("NiagaraFunction");case ENiagaraScriptUsage::DynamicInput:return TEXT("NiagaraDynamicInput");default:return C;}}
    if(C!=TEXT("CurveLinearColorAtlas")){UClass* Class=A.GetClass();if(Class && Class->IsChildOf(UTexture::StaticClass()))return TEXT("Texture");}
    return C;
}
FString Propose(const FString& Name,const FRule& Rule,const TArray<FString>& Prefixes,const TArray<FString>& Suffixes){
    FString Base=Name;
    auto Strip=[&](const TArray<FString>& Values,bool Prefix){FString Longest;for(const auto& V:Values)if(!V.IsEmpty()&&V.Len()>Longest.Len()&&(Prefix?Base.StartsWith(V):Base.EndsWith(V)))Longest=V;if(!Longest.IsEmpty())Base=Prefix?Base.Mid(Longest.Len()):Base.LeftChop(Longest.Len());};
    Strip(Prefixes,true);Strip(Suffixes,false);return Base.IsEmpty()?FString():Rule.Prefix+Base+Rule.Suffix;
}
TArray<TSharedPtr<FRow>> Preview(const TArray<FAssetData>& Assets,const TArray<TSharedPtr<FRule>>& Rules,const TArray<FString>& Prefixes,const TArray<FString>& Suffixes){
    auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();TArray<TSharedPtr<FRow>> Rows;TMap<FName,int32> Destinations;
    TArray<FString> P=Prefixes,S=Suffixes;for(const auto& Rule:Rules){P.AddUnique(Rule->Prefix);S.AddUnique(Rule->Suffix);}
    for(const auto& A:Assets){
        auto R=MakeShared<FRow>();R->Asset=A;R->Type=TypeOf(A);R->Proposed=A.AssetName.ToString();Rows.Add(R);
        TArray<TSharedPtr<FRule>> Matches;for(const auto& Rule:Rules)if(Rule->Enabled&&Rule->Type.Equals(R->Type,ESearchCase::IgnoreCase))Matches.Add(Rule);
        if(Matches.Num()!=1){R->Status=Matches.IsEmpty()?TEXT("No enabled rule"):TEXT("Conflicting rules for type");continue;}
        if(!HandySelection::IsProjectPath(A.PackageName.ToString()) || A.IsRedirector() || A.AssetClassPath.GetAssetName()==TEXT("World")){R->Status=TEXT("Protected asset type or path");continue;}
        TArray<FAssetData> PackageAssets;Registry.GetAssetsByPackageName(A.PackageName,PackageAssets);
        if(PackageAssets.Num()!=1){R->Status=TEXT("Shared package; manual review");continue;}
        R->Proposed=Propose(A.AssetName.ToString(),*Matches[0],P,S);
        const FString Package=A.PackagePath.ToString()+TEXT("/")+R->Proposed;
        FText Reason;if(R->Proposed.IsEmpty() || !FPackageName::IsValidObjectPath(Package+TEXT(".")+R->Proposed,&Reason)){R->Status=TEXT("Invalid name: ")+Reason.ToString();continue;}
        if(R->Proposed==A.AssetName.ToString()){R->Status=TEXT("Already correct");continue;}
        if(R->Proposed.Equals(A.AssetName.ToString(),ESearchCase::IgnoreCase)){R->Status=TEXT("Case-only change; rename manually");continue;}
        if(FPackageName::DoesPackageExist(Package) || Registry.GetAssetByObjectPath(FSoftObjectPath(Package+TEXT(".")+R->Proposed)).IsValid()){R->Status=TEXT("Name already exists (including redirectors)");continue;}
        R->Ready=true;R->Selected=true;R->Status=TEXT("Ready");++Destinations.FindOrAdd(FName(*Package));
    }
    for(auto& R:Rows)if(R->Ready && Destinations.FindRef(FName(*(R->Asset.PackagePath.ToString()+TEXT("/")+R->Proposed)))>1){R->Ready=R->Selected=false;R->Status=TEXT("Multiple assets would have the same name");}
    return Rows;
}
static void FixRedirectors(const TArray<FHistory>& Done){
    TArray<UObjectRedirector*> Redirectors;
    for(const auto& H:Done){
        auto* Redirector=FindObject<UObjectRedirector>(nullptr,*H.OldPath);
        if(!Redirector && FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(H.OldPath)))Redirector=LoadObject<UObjectRedirector>(nullptr,*H.OldPath,nullptr,LOAD_NoRedirects|LOAD_NoWarn);
        if(Redirector)Redirectors.AddUnique(Redirector);
    }
    if(!Redirectors.IsEmpty())FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().FixupReferencers(Redirectors,true,ERedirectFixupMode::DeleteFixedUpRedirectors);
}
bool Apply(const TArray<TSharedPtr<FRow>>& Rows,TArray<FHistory>& History,FString& Report){
    if(HandySelection::HasUnsavedContent(Report))return false;
    auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    auto& Tools=FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    if(Tools.IsFixupReferencersInProgress()){Report=TEXT("Wait for the current redirector fix-up to finish.");return false;}
    TArray<FAssetRenameData> Plans;TArray<FHistory> Intended;TSet<FName> Targets;
    for(const auto& R:Rows)if(R->Selected && R->Ready){
        UObject* O=R->Asset.GetAsset();const FString NewPackage=R->Asset.PackagePath.ToString()+TEXT("/")+R->Proposed;
        const bool ValidName=!R->Proposed.IsEmpty()&&FPackageName::IsValidObjectPath(NewPackage+TEXT(".")+R->Proposed);
        if(!ValidName||Registry.GetAssetByObjectPath(FSoftObjectPath(NewPackage+TEXT(".")+R->Proposed)).IsValid()||!O||O->GetPathName()!=R->Asset.GetObjectPathString()||!HandySelection::IsProjectPath(O->GetOutermost()->GetName())||FPackageName::DoesPackageExist(NewPackage)||Targets.Contains(FName(*NewPackage))){Report=TEXT("Assets or destination names changed. Preview again; nothing renamed.");return false;}
        Targets.Add(FName(*NewPackage));Plans.Emplace(O,R->Asset.PackagePath.ToString(),R->Proposed);Intended.Add({O->GetPathName(),NewPackage+TEXT(".")+R->Proposed});
    }
    if(Plans.IsEmpty()){Report=TEXT("Select ready assets in the preview first.");return false;}
    Tools.RenameAssets(Plans); // Native rename manager updates loaded and saved asset referencers.
    TArray<FHistory> Done;for(int32 I=0;I<Plans.Num();++I)if(Plans[I].Asset.IsValid()&&Plans[I].Asset->GetPathName()==Intended[I].NewPath)Done.Add(Intended[I]);
    if(Done.IsEmpty()){Report=TEXT("Unreal did not rename any assets. Check its messages/source-control prompts.");return false;}
    History=Done;FixRedirectors(Done);
    Report=FString::Printf(TEXT("Renamed %d of %d assets. Unreal's redirector fix-up was requested for the old paths; any redirectors it cannot safely fix are retained. See Unreal's report. Revert Last Rename is available for this session."),Done.Num(),Plans.Num());
    return true;
}
bool Revert(TArray<FHistory>& History,FString& Report){
    if(History.IsEmpty()){Report=TEXT("No completed rename batch in this session.");return false;}
    if(HandySelection::HasUnsavedContent(Report))return false;
    auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<TSharedPtr<FRow>> Rows;
    for(const auto& H:History){auto R=MakeShared<FRow>();R->Asset=Registry.GetAssetByObjectPath(FSoftObjectPath(H.NewPath));R->Proposed=FPackageName::ObjectPathToObjectName(H.OldPath);R->Ready=R->Selected=true;if(!R->Asset.IsValid()){Report=TEXT("An asset from the last batch moved again. Revert stopped; nothing renamed.");return false;}Rows.Add(R);}
    TArray<FHistory> Reverse;const bool Result=Apply(Rows,Reverse,Report);if(Result){History.RemoveAll([&](const FHistory& H){return Reverse.ContainsByPredicate([&](const FHistory& R){return R.NewPath==H.OldPath;});});Report=TEXT("Revert result: ")+Report;}return Result;
}
}
