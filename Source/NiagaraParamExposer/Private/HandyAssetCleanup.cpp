#include "HandyAssetCleanup.h"
#include "HandyAssetSelection.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "NiagaraSystem.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Engine/Texture.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "FileHelpers.h"
#include "UObject/Package.h"
#include "Misc/ScopedSlowTask.h"

namespace HandyAssetCleanup
{
TSet<FName> Reachable(const TSet<FName>& Roots, const FEdges& Dependencies)
{
    TSet<FName> Result = Roots;
    TArray<FName> Pending = Roots.Array();
    while (!Pending.IsEmpty())
    {
        const FName Current = Pending.Pop(EAllowShrinking::No);
        if (const auto* Next = Dependencies.Find(Current)) for (FName Name : *Next)
            if (!Result.Contains(Name)) { Result.Add(Name); Pending.Add(Name); }
    }
    return Result;
}
TSet<FName> SafeSubset(TSet<FName> Candidates, const FEdges& Referencers)
{
    // A retained asset must never lose one of its dependencies. Propagate
    // protection until stable; isolated mutually-referencing groups can go together.
    bool bChanged;
    do
    {
        bChanged = false;
        for (FName Candidate : Candidates.Array())
            if (const auto* Refs = Referencers.Find(Candidate))
                for (FName Ref : *Refs) if (Ref != Candidate && !Candidates.Contains(Ref))
                { Candidates.Remove(Candidate); bChanged = true; break; }
    } while (bChanged);
    return Candidates;
}
FScan Scan(const TArray<FString>& Folders, const TArray<FAssetData>& ExplicitAssets)
{
    FScan Result;
    if (Folders.IsEmpty() && ExplicitAssets.IsEmpty()) { Result.Error = TEXT("Select Content Browser folders, then click Use Selected Folders."); return Result; }
    for (const FString& Folder : Folders)
        if (!(Folder == TEXT("/Game") || Folder.StartsWith(TEXT("/Game/"))) || Folder.Contains(TEXT("..")))
        { Result.Error = TEXT("Only project Content folders (/Game) can be cleaned. Engine and plugin content are protected."); return Result; }
    TArray<UPackage*> Dirty;
    FEditorFileUtils::GetDirtyPackages(Dirty);
    for (UPackage* Package : Dirty)
        if (Package && !Package->HasAnyFlags(RF_Transient))
        { Result.Error = FString::Printf(TEXT("Save or discard unsaved assets/maps before scanning. Unsaved references cannot be checked reliably: %s"), *Package->GetName()); return Result; }
    FScopedSlowTask Progress(3.f, NSLOCTEXT("HandyToolbox", "ScanAssets", "Checking asset references..."));
    Progress.MakeDialog();
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Progress.EnterProgressFrame(1.f);
    Registry.SearchAllAssets(true);
    Registry.ScanPathsSynchronous({TEXT("/Game")}, true);
    if (Registry.IsLoadingAssets()) { Result.Error = TEXT("Asset discovery is still running. Try Scan again when it finishes."); return Result; }
    FARFilter NiagaraFilter;
    NiagaraFilter.ClassPaths.Add(UNiagaraSystem::StaticClass()->GetClassPathName());
    NiagaraFilter.bRecursiveClasses = true;
    TArray<FAssetData> Systems;
    Registry.GetAssets(NiagaraFilter, Systems);
    TSet<FName> Roots;
    for (const auto& System : Systems) Roots.Add(System.PackageName);
    Result.NiagaraSystems = Roots.Num();
    if (Roots.IsEmpty()) { Result.Error = TEXT("No Niagara Systems were found. Cleanup is disabled until asset discovery finds at least one system."); return Result; }
    FEdges Dependencies;
    TSet<FName> Seen;
    TArray<FName> Queue = Roots.Array();
    while (!Queue.IsEmpty())
    {
        FName Name = Queue.Pop(EAllowShrinking::No);
        if (Seen.Contains(Name)) continue;
        Seen.Add(Name);
        auto& Deps = Dependencies.FindOrAdd(Name);
        Registry.GetDependencies(Name, Deps, UE::AssetRegistry::EDependencyCategory::All);
        Queue.Append(Deps);
    }
    const TSet<FName> NiagaraUsed = Reachable(Roots, Dependencies);
    Progress.EnterProgressFrame(1.f);
    TArray<FAssetData> Assets=HandySelection::Gather(Folders,ExplicitAssets);
    if(Assets.IsEmpty()){Result.Error=TEXT("No assets found in the selected scope.");return Result;}
    FARFilter SupportedFilter;
    for(const auto& A:Assets)SupportedFilter.PackageNames.AddUnique(A.PackageName);
    // Include every top-level object in selected packages when evaluating safety.
    for(FName Package:SupportedFilter.PackageNames){TArray<FAssetData> PackageAssets;Registry.GetAssetsByPackageName(Package,PackageAssets);for(const auto& A:PackageAssets)Assets.AddUnique(A);}
    SupportedFilter.bRecursiveClasses = true;
    SupportedFilter.ClassPaths = {UMaterialInterface::StaticClass()->GetClassPathName(), UMaterialFunctionInterface::StaticClass()->GetClassPathName(), UTexture::StaticClass()->GetClassPathName(), UStaticMesh::StaticClass()->GetClassPathName(), USkeletalMesh::StaticClass()->GetClassPathName()};
    TArray<FAssetData> Supported;
    Registry.GetAssets(SupportedFilter, Supported);
    TSet<FName> SupportedPackages;
    TSet<FSoftObjectPath> SupportedObjects;
    for (const auto& Asset : Supported) { SupportedPackages.Add(Asset.PackageName); SupportedObjects.Add(Asset.GetSoftObjectPath()); }
    // Never delete a package which also contains an unsupported asset.
    for (const auto& Asset : Assets)
        if (!SupportedObjects.Contains(Asset.GetSoftObjectPath())) SupportedPackages.Remove(Asset.PackageName);
    // A single checkbox must not implicitly delete another top-level asset
    // stored in the same package. Exclude unusual multi-asset packages.
    TMap<FName, int32> PackageCounts;
    for (const auto& Asset : Assets) ++PackageCounts.FindOrAdd(Asset.PackageName);
    for (const auto& Pair : PackageCounts) if (Pair.Value > 1) SupportedPackages.Remove(Pair.Key);
    TSet<FName> Candidates;
    FEdges References;
    for (const auto& Asset : Assets)
    {
        if (SupportedPackages.Contains(Asset.PackageName) && !NiagaraUsed.Contains(Asset.PackageName)) Candidates.Add(Asset.PackageName);
        Registry.GetReferencers(Asset.PackageName, References.FindOrAdd(Asset.PackageName), UE::AssetRegistry::EDependencyCategory::All);
    }
    const auto Safe = SafeSubset(Candidates, References);
    Progress.EnterProgressFrame(1.f);
    for (const auto& Asset : Assets)
    {
        auto Row = MakeShared<FRow>(); Row->Asset = Asset;
        Row->bEligible = Safe.Contains(Asset.PackageName);
        if (NiagaraUsed.Contains(Asset.PackageName)) Row->Reason = TEXT("Keep - used by Niagara (directly or indirectly)");
        else if (!SupportedPackages.Contains(Asset.PackageName)) Row->Reason = TEXT("Keep - asset type or shared package excluded from cleanup");
        else if (!Row->bEligible)
        {
            Row->Reason = TEXT("Keep - referenced by retained project content");
            for (FName Ref : References.FindRef(Asset.PackageName)) if (!Safe.Contains(Ref) && Ref != Asset.PackageName)
            { Row->Reason += TEXT(": ") + Ref.ToString(); break; }
        }
        else Row->Reason = TEXT("Unused - eligible for review");
        Result.Rows.Add(Row);
    }
    Result.Rows.Sort([](const TSharedPtr<FRow>& A, const TSharedPtr<FRow>& B)
    { if (A->bEligible != B->bEligible) return A->bEligible; return A->Asset.PackageName.LexicalLess(B->Asset.PackageName); });
    return Result;
}
}
