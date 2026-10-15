#include "HandyAssetCleanup.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/Texture2D.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "UObject/SavePackage.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "NiagaraParamExposerEditorTab.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Framework/Application/SlateApplication.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandyGraphTest, "NiagaraParamExposer.AssetCleanup.Dependencies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHandyGraphTest::RunTest(const FString&)
{
    using namespace HandyAssetCleanup;
    FEdges Dependencies;
    Dependencies.Add(TEXT("System"), {TEXT("Mesh"), TEXT("Instance")});
    Dependencies.Add(TEXT("Mesh"), {TEXT("Material")});
    Dependencies.Add(TEXT("Instance"), {TEXT("Material"), TEXT("SharedTexture")});
    Dependencies.Add(TEXT("Material"), {TEXT("Texture"), TEXT("Function")});
    Dependencies.Add(TEXT("Function"), {TEXT("Material")});
    const auto Used = Reachable({TEXT("System")}, Dependencies);
    TestEqual(TEXT("Complete transitive closure with cycles"), Used.Num(), 7);
    TestTrue(TEXT("Indirect texture retained"), Used.Contains(TEXT("Texture")));
    TestTrue(TEXT("Shared texture retained"), Used.Contains(TEXT("SharedTexture")));
    TestFalse(TEXT("Unconnected material excluded"), Used.Contains(TEXT("UnusedMaterial")));
    FEdges Refs;
    Refs.Add(TEXT("UnusedTexture"), {TEXT("UnusedMaterial")});
    Refs.Add(TEXT("OutsideUsedTexture"), {TEXT("OutsideMaterial")});
    Refs.Add(TEXT("Leaf"), {TEXT("OutsideUsedTexture")});
    Refs.Add(TEXT("CycleA"), {TEXT("CycleB")}); Refs.Add(TEXT("CycleB"), {TEXT("CycleA")});
    const auto Safe = SafeSubset({TEXT("UnusedMaterial"), TEXT("UnusedTexture"), TEXT("OutsideUsedTexture"), TEXT("Leaf"), TEXT("CycleA"), TEXT("CycleB")}, Refs);
    TestEqual(TEXT("Only orphan group and isolated cycle eligible"), Safe.Num(), 4);
    TestFalse(TEXT("External reference protects asset"), Safe.Contains(TEXT("OutsideUsedTexture")));
    TestFalse(TEXT("External protection propagates"), Safe.Contains(TEXT("Leaf")));
    TestTrue(TEXT("Orphan texture can be deleted with orphan material"), Safe.Contains(TEXT("UnusedTexture")));
    TestTrue(TEXT("Unselected material blocks deletion of its texture"), SafeSubset({TEXT("UnusedTexture")}, Refs).IsEmpty());
    TestTrue(TEXT("Engine scope rejected"), !Scan({TEXT("/Engine")}).Error.IsEmpty());
    TestTrue(TEXT("Empty scope rejected"), !Scan({}).Error.IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandyPanelTest, "NiagaraParamExposer.AssetCleanup.Navigation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHandyPanelTest::RunTest(const FString&)
{
    NiagaraParamExposer_OpenEditorTab(nullptr);
    auto Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(TEXT("NiagaraParamExposerEditorTab")));
    if (!TestTrue(TEXT("HandyToolbox opened"), Tab.IsValid())) return false;
    FSlateApplication::Get().Tick();
    TSet<FString> Labels;
    TSharedPtr<SWidgetSwitcher> Pages;
    TFunction<void(TSharedRef<SWidget>)> Visit = [&](TSharedRef<SWidget> Widget)
    {
        if (Widget->GetType() == TEXT("STextBlock")) Labels.Add(StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString());
        if (Widget->GetType() == TEXT("SWidgetSwitcher")) Pages = StaticCastSharedRef<SWidgetSwitcher>(Widget);
        FChildren* Children = Widget->GetChildren();
        for (int32 I=0; I<Children->Num(); ++I) Visit(Children->GetChildAt(I));
    };
    Visit(Tab->GetContent());
    TestTrue(TEXT("Brand visible"), Labels.Contains(TEXT("HandyToolbox")));
    TestTrue(TEXT("Parameters navigation present"), Labels.Contains(TEXT("Niagara Parameters")));
    TestTrue(TEXT("Cleanup navigation present"), Labels.Contains(TEXT("Asset Cleanup")));
    if (!TestTrue(TEXT("Independent tool pages exist"), Pages.IsValid())) return false;
    Pages->SetActiveWidgetIndex(1); Labels.Reset(); Visit(Tab->GetContent());
    TestTrue(TEXT("Cleanup scope action present"), Labels.Contains(TEXT("Use Selected Folders")));
    TestTrue(TEXT("Scan action present"), Labels.Contains(TEXT("Scan")));
    Pages->SetActiveWidgetIndex(2); Labels.Reset(); Visit(Tab->GetContent());
    TestTrue(TEXT("Reference checker scan present"), Labels.Contains(TEXT("Scan References")));
    TestTrue(TEXT("Reference relink present"), Labels.Contains(TEXT("Relink Selected")));
    Pages->SetActiveWidgetIndex(3); Labels.Reset(); Visit(Tab->GetContent());
    TestTrue(TEXT("Naming preview present"), Labels.Contains(TEXT("Preview Names")));
    TestTrue(TEXT("Naming apply present"), Labels.Contains(TEXT("Rename and Fix Redirectors")));
    Pages->SetActiveWidgetIndex(0); Labels.Reset(); Visit(Tab->GetContent());
    TestTrue(TEXT("Parameter cleanup retained"), Labels.Contains(TEXT("Clean Up Unused User Parameters")));
    TestTrue(TEXT("Undo retained"), Labels.Contains(TEXT("Undo")));
    Tab->RequestCloseTab();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandyRegistryTest, "NiagaraParamExposer.AssetCleanup.Registry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHandyRegistryTest::RunTest(const FString&)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("HandyCleanupFixtures"))) { AddInfo(TEXT("Use -HandyCleanupFixtures only in an isolated test project.")); return true; }
    // Fixtures live exclusively in the isolated automation project's Content.
    const FString Root = TEXT("/Game/HandyCleanupTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Scope = Root + TEXT("/Scope");
    TArray<UObject*> Created;
    auto Make = [&](UClass* Class, const FString& Path) -> UObject*
    {
        UPackage* Package = CreatePackage(*Path);
        UObject* Asset = NewObject<UObject>(Package, Class, FName(*FPackageName::GetShortName(Path)), RF_Public | RF_Standalone);
        Created.Add(Asset); return Asset;
    };
    auto* Shared = Cast<UTexture2D>(Make(UTexture2D::StaticClass(), Scope + TEXT("/Shared")));
    auto* OrphanTexture = Cast<UTexture2D>(Make(UTexture2D::StaticClass(), Scope + TEXT("/OrphanTexture")));
    auto* OutsideTexture = Cast<UTexture2D>(Make(UTexture2D::StaticClass(), Scope + TEXT("/OutsideUsedTexture")));
    auto* Base = Cast<UMaterial>(Make(UMaterial::StaticClass(), Scope + TEXT("/Base")));
    auto MakeInstance = [&](const FString& Path, UTexture* Texture)
    {
        auto* Instance = Cast<UMaterialInstanceConstant>(Make(UMaterialInstanceConstant::StaticClass(), Path));
        Instance->SetParentEditorOnly(Base, false);
        FTextureParameterValue Value; Value.ParameterInfo = FMaterialParameterInfo(TEXT("FixtureTexture")); Value.ParameterValue = Texture;
        Instance->TextureParameterValues.Add(Value);
        return Instance;
    };
    auto* Used = MakeInstance(Scope + TEXT("/UsedInstance"), Shared);
    auto* Orphan = MakeInstance(Scope + TEXT("/OrphanInstance"), OrphanTexture);
    FTextureParameterValue SharedValue; SharedValue.ParameterInfo = FMaterialParameterInfo(TEXT("Shared")); SharedValue.ParameterValue = Shared;
    Orphan->TextureParameterValues.Add(SharedValue);
    MakeInstance(Root + TEXT("/Outside/RetainedInstance"), OutsideTexture);
    auto* System = Cast<UNiagaraSystem>(Make(UNiagaraSystem::StaticClass(), Root + TEXT("/Systems/KeepSystem")));
    UNiagaraSystemFactoryNew::InitializeSystem(System, true);
    FNiagaraVariable Binding(FNiagaraTypeDefinition(UMaterialInterface::StaticClass()), TEXT("User.Material"));
    System->GetExposedParameters().AddParameter(Binding);
    System->GetExposedParameters().SetUObject(Used, Binding);
    for (UObject* Asset : Created)
    {
        const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        if (!TestTrue(TEXT("Fixture path stays in isolated project"), FPaths::ConvertRelativePathToFull(Filename).StartsWith(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir())))) return false;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
        if (!TestTrue(TEXT("Fixture saved"), UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args))) return false;
        FAssetRegistryModule::AssetCreated(Asset);
    }
    auto Result = HandyAssetCleanup::Scan({Scope});
    if (!TestTrue(*Result.Error, Result.Error.IsEmpty())) return false;
    auto Eligible = [&](const TCHAR* Name)
    {
        const auto* Row = Result.Rows.FindByPredicate([&](const TSharedPtr<HandyAssetCleanup::FRow>& Item) { return Item->Asset.AssetName == FName(Name); });
        TestNotNull(*FString::Printf(TEXT("Found %s"), Name), Row);
        return Row && (*Row)->bEligible;
    };
    TestFalse(TEXT("Niagara material retained"), Eligible(TEXT("UsedInstance")));
    TestFalse(TEXT("Indirect base material retained"), Eligible(TEXT("Base")));
    TestFalse(TEXT("Shared indirect texture retained"), Eligible(TEXT("Shared")));
    TestFalse(TEXT("Reference outside selected folders retained"), Eligible(TEXT("OutsideUsedTexture")));
    TestTrue(TEXT("Unreferenced material instance eligible"), Eligible(TEXT("OrphanInstance")));
    TestTrue(TEXT("Texture used only by orphan instance eligible"), Eligible(TEXT("OrphanTexture")));
    auto Explicit=HandyAssetCleanup::Scan({}, {FAssetData(OrphanTexture)});
    TestEqual(TEXT("Individual asset scan does not include siblings"),Explicit.Rows.Num(),1);
    if(Explicit.Rows.Num()==1)TestFalse(TEXT("Unselected owning material protects individual texture"),Explicit.Rows[0]->bEligible);
    auto Batch=HandyAssetCleanup::Scan({}, {FAssetData(OrphanTexture),FAssetData(Orphan)});
    TestEqual(TEXT("Individual batch contains precisely two assets"),Batch.Rows.Num(),2);
    for(const auto& R:Batch.Rows)TestTrue(TEXT("Complete unused material-texture batch eligible"),R->bEligible);
    Shared->GetOutermost()->MarkPackageDirty();
    TestTrue(TEXT("Unsaved content blocks stale dependency scan"), !HandyAssetCleanup::Scan({Scope}).Error.IsEmpty());
    Shared->GetOutermost()->SetDirtyFlag(false);
    // No deletion API is invoked by this test. Fixtures remain in the temporary project.
    return true;
}
#endif
