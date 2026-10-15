#include "HandyReferenceChecker.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Serialization/FindReferencersArchive.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "ScopedTransaction.h"
#include "FileHelpers.h"
#include "Misc/ScopedSlowTask.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "Materials/MaterialInstance.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialFunction.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

namespace HandyReferences {
bool Inside(const FString& Package, const FString& Root) { return Package == Root || Package.StartsWith(Root + TEXT("/")); }
// Deliberately limited to ordinary Texture2D source data. Other asset payloads need
// their own identity verifier; reflected properties alone cannot prove equality.
bool IdenticalTextures(UObject* A, UObject* B)
{
    UTexture2D* X=Cast<UTexture2D>(A); UTexture2D* Y=Cast<UTexture2D>(B);
    if (!X || !Y || X==Y || X->GetClass()!=UTexture2D::StaticClass() || Y->GetClass()!=UTexture2D::StaticClass()) return false;
    auto& S=X->Source; auto& T=Y->Source;
    if (!S.IsValid() || !T.IsValid() || S.GetNumBlocks()!=1 || T.GetNumBlocks()!=1 || S.GetNumLayers()!=1 || T.GetNumLayers()!=1) return false;
    if (S.GetSizeX()!=T.GetSizeX() || S.GetSizeY()!=T.GetSizeY() || S.GetNumSlices()!=T.GetNumSlices() || S.GetNumMips()!=T.GetNumMips() || S.GetFormat()!=T.GetFormat()) return false;
    for (int32 M=0; M<S.GetNumMips(); ++M) {
        TArray64<uint8> L,R;
        if (!S.GetMipData(L,M) || !T.GetMipData(R,M) || L.Num()==0 || L!=R) return false;
    }
    for (TFieldIterator<FProperty> It(X->GetClass()); It; ++It) {
        FProperty* P=*It;
        if (P->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient)) continue;
        const FName N=P->GetFName();
        // Source has already been compared byte for byte. Import paths and build
        // identifiers are provenance, not texture rendering settings.
        if (N==TEXT("ImportedSize") || N==TEXT("Source") || N==TEXT("AssetImportData") || N==TEXT("LightingGuid") || N==TEXT("AssetUserData")) continue;
        if (!P->Identical_InContainer(X,Y,PPF_None)) return false;
    }
    // User metadata may carry behavior: require it to match too.
    FProperty* UserData=FindFProperty<FProperty>(X->GetClass(),TEXT("AssetUserData"));
    return !UserData || UserData->Identical_InContainer(X,Y,PPF_None);
}
static FString OwnedKey(UObject* Object,UObject* Root)
{
    if(Object==Root)return TEXT("ROOT");
    for(UObject* Outer=Object;Outer && Outer!=Root;Outer=Outer->GetOuter()) {
        if(Outer->GetOuter()==Root && Outer->GetClass()->HasAnyClassFlags(CLASS_Optional))
            return TEXT("OPTIONAL:")+Outer->GetClass()->GetName()+(Object==Outer?FString():TEXT(".")+Object->GetPathName(Outer));
    }
    return Object->GetPathName(Root);
}
class FCanonicalMaterialArchive:public FObjectAndNameAsStringProxyArchive
{
    UObject* Root;
public:
    FCanonicalMaterialArchive(FArchive& Inner,UObject* InRoot):FObjectAndNameAsStringProxyArchive(Inner,false),Root(InRoot){}
    using FObjectAndNameAsStringProxyArchive::operator<<;
    FArchive& operator<<(UObject*& Object) override {
        FString Path=Object?(Object==Root||Object->IsIn(Root)?TEXT("<SELF>.")+OwnedKey(Object,Root):Object->GetPathName()):TEXT("None");
        *this << Path;return *this;
    }
    FArchive& operator<<(FObjectPtr& Object) override {UObject* Value=Object.Get();return *this << Value;}
};
// Material authoring data is reflected, including editor-only expression graphs.
// Compare every owned object's persistent properties; retain external paths exactly.
// Restrict to native Engine material classes (no arbitrary custom bulk serializers).
bool IdenticalAssets(UObject* A,UObject* B)
{
    if (!A || !B || A==B || A->GetClass()!=B->GetClass()) return false;
    if (A->GetClass()==UTexture2D::StaticClass()) return IdenticalTextures(A,B);
    if (A->GetClass()!=UMaterial::StaticClass() && A->GetClass()!=UMaterialInstanceConstant::StaticClass() && A->GetClass()!=UMaterialFunction::StaticClass()) return false;
    TArray<UObject*> Left,Right;GetObjectsWithOuter(A,Left,true);GetObjectsWithOuter(B,Right,true);Left.Add(A);Right.Add(B);
    TMap<FString,UObject*> Peers;
    for(UObject* Object:Right)if(!Object->HasAnyFlags(RF_Transient))Peers.Add(OwnedKey(Object,B),Object);
    TFunction<bool(FProperty*,const void*,const void*)> Equal;
    Equal=[&](FProperty* P,const void* X,const void* Y)->bool {
        if(P->Identical(X,Y,PPF_None))return true;
        if(auto* Ref=CastField<FObjectPropertyBase>(P)){
            UObject* U=Ref->GetObjectPropertyValue(X);UObject* V=Ref->GetObjectPropertyValue(Y);
            if(!U||!V)return false;
            if(!(U==A||U->IsIn(A)) || !(V==B||V->IsIn(B)))return false;
            return U->GetClass()==V->GetClass() && OwnedKey(U,A)==OwnedKey(V,B);
        }
        if(auto* Array=CastField<FArrayProperty>(P)){
            FScriptArrayHelper U(Array,X),V(Array,Y);if(U.Num()!=V.Num())return false;
            for(int32 I=0;I<U.Num();++I)if(!Equal(Array->Inner,U.GetRawPtr(I),V.GetRawPtr(I)))return false;return true;
        }
        if(auto* Struct=CastField<FStructProperty>(P)){
            // Include native payloads exactly, canonicalizing owned object references.
            if(Struct->Struct->StructFlags & (STRUCT_SerializeNative|STRUCT_IdenticalNative)) {
                TArray<uint8> L,R;FMemoryWriter WL(L,true),WR(R,true);
                FCanonicalMaterialArchive AL(WL,A),AR(WR,B);
                Struct->Struct->SerializeItem(AL,const_cast<void*>(X),nullptr);
                Struct->Struct->SerializeItem(AR,const_cast<void*>(Y),nullptr);
                return !AL.IsError() && !AR.IsError() && L.Num()>0 && L==R;
            }
            bool Any=false;
            for(TFieldIterator<FProperty> It(Struct->Struct);It;++It){Any=true;
                for(int32 I=0;I<It->ArrayDim;++I)if(!Equal(*It,It->ContainerPtrToValuePtr<void>(X,I),It->ContainerPtrToValuePtr<void>(Y,I)))return false;
            }return Any;
        }
        // Maps, sets and native structs must pass Unreal's exact equality. A
        // conservative missed match is preferable to replacing different data.
        return false;
    };
    int32 Compared=0;
    for(UObject* Object:Left){
        if(Object->HasAnyFlags(RF_Transient))continue;
        if(Object->GetClass()->GetOutermost()->GetName()!=TEXT("/Script/Engine"))return false;
        UObject** Other=Peers.Find(OwnedKey(Object,A));
        if(!Other || (*Other)->GetClass()!=Object->GetClass())return false;++Compared;
        for(TFieldIterator<FProperty> It(Object->GetClass());It;++It){
            FProperty* P=*It;
            if(P->HasAnyPropertyFlags(CPF_Transient|CPF_DuplicateTransient|CPF_NonPIEDuplicateTransient))continue;
            if(P->GetFName()==TEXT("StateId") || P->GetFName()==TEXT("LightingGuid") || P->GetFName()==TEXT("ParameterStateId"))continue;
            for(int32 I=0;I<P->ArrayDim;++I)if(!Equal(P,P->ContainerPtrToValuePtr<void>(Object,I),P->ContainerPtrToValuePtr<void>(*Other,I)))return false;
        }
    }
    if(Compared!=Peers.Num())return false;
    return true;
}
static FString Location(UObject* Object, UObject* Asset, UObject* Target, const TArray<FProperty*>& Properties)
{
    FString Result=Object->GetPathName(Asset->GetOutermost())+TEXT(" [")+Object->GetClass()->GetName()+TEXT("]");
    if (UNiagaraSystem* System=Cast<UNiagaraSystem>(Asset)) {
        for (const auto& Handle:System->GetEmitterHandles()) {
            UObject* Emitter=Handle.GetInstance().Emitter;
            if (Emitter && (Object==Emitter || Object->IsIn(Emitter))) { Result=TEXT("Emitter ")+Handle.GetName().ToString()+TEXT(" / ")+Result; break; }
        }
    }
    if (auto* Instance=Cast<UMaterialInstance>(Object)) {
        for (const auto& Value:Instance->TextureParameterValues) if (Value.ParameterValue==Target) Result+=TEXT(" / Texture parameter: ")+Value.ParameterInfo.Name.ToString();
    }
    TArray<FString> Names;
    for (FProperty* P:Properties) if (P) Names.AddUnique(P->GetName());
    return Result+TEXT(" : ")+(Names.IsEmpty()?TEXT("serialized reference"):FString::Join(Names,TEXT(", ")));
}
TArray<TSharedPtr<FRow>> Scan(const FString& Root, FString& Report, const TArray<FName>& SeedPackages)
{
    TArray<TSharedPtr<FRow>> Rows;
    if (!(Root==TEXT("/Game") || Root.StartsWith(TEXT("/Game/"))) || Root.Contains(TEXT(".."))) { Report=TEXT("Select one project Content folder."); return Rows; }
    TArray<UPackage*> Dirty; FEditorFileUtils::GetDirtyPackages(Dirty);
    for (UPackage* Package:Dirty) if (Package && !Package->HasAnyFlags(RF_Transient)) { Report=TEXT("Save or discard unsaved assets before scanning, so package references are current."); return Rows; }
    auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.SearchAllAssets(true);
    Registry.ScanPathsSynchronous({Root},true);
    TArray<FAssetData> Local; Registry.GetAssetsByPath(FName(*Root),Local,true);
    TArray<FName> Queue; for (const auto& A:Local) if(SeedPackages.IsEmpty() || SeedPackages.Contains(A.PackageName)) Queue.AddUnique(A.PackageName);
    TSet<FName> Visited;
    TMap<FString,FString> MatchCache;
    FScopedSlowTask Progress(0,FText::FromString(TEXT("Inspecting folder references and duplicate assets..."))); Progress.MakeDialog();
    for (int32 I=0;I<Queue.Num();++I) {
        const FName Package=Queue[I]; if (Visited.Contains(Package)) continue; Visited.Add(Package);
        TArray<FAssetData> Sources; Registry.GetAssetsByPackageName(Package,Sources);
        TArray<FName> Dependencies; Registry.GetDependencies(Package,Dependencies,UE::AssetRegistry::EDependencyCategory::All);
        for (FName Dependency:Dependencies) {
            if (Dependency==Package || Dependency.ToString().StartsWith(TEXT("/Script/"))) continue;
            const FString Dest=Dependency.ToString();
            const bool Allowed=Inside(Dest,TEXT("/Engine")) || Inside(Dest,TEXT("/Niagara"));
            if (!Allowed) Queue.AddUnique(Dependency);
            TArray<FAssetData> Targets; Registry.GetAssetsByPackageName(Dependency,Targets);
            bool Found=false;
            for (const auto& Source:Sources) {
                UObject* Asset=Source.GetAsset(); if (!Asset) continue;
                TArray<UObject*> Objects; GetObjectsWithOuter(Asset->GetOutermost(),Objects,true); 
                for (const auto& Target:Targets) {
                    UObject* TargetObject=Target.GetAsset(); if (!TargetObject) continue;
                    TArray<UObject*> Find{TargetObject};
                    for (UObject* Object:Objects) {
                        TArray<FProperty*> Props; FFindReferencersArchive Finder(Object,Find);
                        if (!Finder.GetReferenceCount(TargetObject,&Props)) continue;
                        auto Row=MakeShared<FRow>(); Row->Source=Source.GetObjectPathString(); Row->Target=Target.GetObjectPathString();
                        Row->Detail=Location(Object,Asset,TargetObject,Props); Row->Outside=!Inside(Dest,Root)&&!Allowed;
                        Row->Status=Allowed?TEXT("ENGINE / ALLOWED"):Row->Outside?TEXT("EXTERNAL"):TEXT("LOCAL");
                        if (Row->Outside && Inside(Package.ToString(),Root)) {
                            if (const FString* Cached=MatchCache.Find(Row->Target)) Row->Replacement=*Cached;
                            else {
                                TArray<FString> Matches;
                                for (const auto& Candidate:Local) {
                                    if (Candidate.AssetClassPath==Target.AssetClassPath && IdenticalAssets(TargetObject,Candidate.GetAsset())) Matches.Add(Candidate.GetObjectPathString());
                                }
                                Row->Replacement=Matches.Num()==1?Matches[0]:Matches.Num()>1?TEXT("Ambiguous: multiple identical assets"):TEXT("No verified local match; manual review");
                                MatchCache.Add(Row->Target,Row->Replacement);
                            }
                        }
                        Rows.Add(Row); Found=true;
                    }
                }
            }
            if (!Found) {
                auto Row=MakeShared<FRow>(); Row->Source=Package.ToString(); Row->Target=Dest; Row->Outside=!Inside(Dest,Root)&&!Allowed;
                Row->Status=Allowed?TEXT("ENGINE / ALLOWED"):Row->Outside?TEXT("EXTERNAL"):TEXT("LOCAL");
                Row->Detail=TEXT("Saved package dependency; precise property unresolved (may be a soft reference). Manual review."); Rows.Add(Row);
            }
        }
    }
    for(auto& Row:Rows) Row->Scope=Root;
    Rows.Sort([](const auto& A,const auto& B){ if(A->Outside!=B->Outside)return A->Outside; return A->Source<B->Source; });
    int32 External=0; for(const auto& R:Rows) External+=R->Outside?1:0;
    Report=FString::Printf(TEXT("%d reference locations; %d external locations. Dependency graph uses saved packages: save changes before scanning. Automatic matching supports Texture2D and native materials/functions/instances; other types require manual review."),Rows.Num(),External);
    return Rows;
}
bool Relink(const FString& Root,const TArray<TSharedPtr<FRow>>& Rows,FString& Report)
{

    TMap<UObject*,TMap<UObject*,UObject*>> Plans;
    for(const auto& R:Rows) if(R->Selected) {
        const FString Scope=R->Scope.IsEmpty()?Root:R->Scope;
        if(!(Scope==TEXT("/Game")||Scope.StartsWith(TEXT("/Game/")))||Scope.Contains(TEXT(".."))){Report=TEXT("Invalid project folder.");return false;}
        UObject* Source=LoadObject<UObject>(nullptr,*R->Source); UObject* Old=LoadObject<UObject>(nullptr,*R->Target); UObject* New=LoadObject<UObject>(nullptr,*R->Replacement);
        if(!Source || !Old || !New || !Inside(Source->GetOutermost()->GetName(),Scope) || Inside(Old->GetOutermost()->GetName(),Scope) || !Inside(New->GetOutermost()->GetName(),Scope) || !IdenticalAssets(Old,New)) {Report=TEXT("Reference or duplicate changed. Nothing modified; scan again.");return false;}
        TArray<UObject*> Owned;GetObjectsWithOuter(Source,Owned,true);Owned.Add(Source);
        bool StillReferenced=false;TArray<UObject*> Target{Old};
        for(UObject* O:Owned){FFindReferencersArchive Finder(O,Target);StillReferenced|=Finder.GetReferenceCount(Old)>0;}
        if(!StillReferenced){Report=TEXT("A selected reference no longer exists. Nothing modified; scan again.");return false;}
        auto& Plan=Plans.FindOrAdd(Source);
        if(UObject** Existing=Plan.Find(Old))if(*Existing!=New){Report=TEXT("Overlapping folder selections propose conflicting replacements. Select one scope for that asset.");return false;}
        Plan.Add(Old,New);
    }
    if(Plans.IsEmpty()){Report=TEXT("Check a verified replacement first.");return false;}
    FScopedTransaction Transaction(FText::FromString(TEXT("HandyToolbox: localize asset references")));
    int64 Count=0;
    for(auto& Plan:Plans) {
        UObject* Asset=Plan.Key; TArray<UObject*> Objects; GetObjectsWithOuter(Asset,Objects,true); Objects.Add(Asset);
        for(UObject* O:Objects){O->SetFlags(RF_Transactional);O->Modify();}
        Asset->PreEditChange(nullptr);
        FArchiveReplaceObjectRef<UObject> Replace(Asset,Plan.Value,EArchiveReplaceObjectFlags::IgnoreOuterRef|EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
        Count+=Replace.GetCount(); Asset->PostEditChange(); Asset->MarkPackageDirty();
    }
    Report=FString::Printf(TEXT("Replaced %lld references. Originals preserved. Undo is available; review and save the affected assets, then scan again."),Count);
    return true;
}
}
