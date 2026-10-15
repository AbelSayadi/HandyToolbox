#if WITH_DEV_AUTOMATION_TESTS
#include "HandyNaming.h"
#include "HandyAssetSelection.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandyNamingRulesTest,"NiagaraParamExposer.Naming.Rules",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHandyNamingRulesTest::RunTest(const FString&){
    HandyNaming::FRule Rule;Rule.Prefix=TEXT("MI_");Rule.Suffix=TEXT("_VFX");
    TArray<FString> P{TEXT("M_"),TEXT("MI_"),TEXT("T_")},S{TEXT("_Old"),TEXT("_VFX")};
    TestEqual(TEXT("Replace recognized prefix and suffix"),HandyNaming::Propose(TEXT("M_Fire_Old"),Rule,P,S),FString(TEXT("MI_Fire_VFX")));
    TestEqual(TEXT("Idempotent names"),HandyNaming::Propose(TEXT("MI_Fire_VFX"),Rule,P,S),FString(TEXT("MI_Fire_VFX")));
    TestEqual(TEXT("Preserve base underscores"),HandyNaming::Propose(TEXT("Fire_Aura"),Rule,P,S),FString(TEXT("MI_Fire_Aura_VFX")));
    TestTrue(TEXT("Empty base rejected"),HandyNaming::Propose(TEXT("M_"),Rule,P,S).IsEmpty());
    auto Rules=HandyNaming::Defaults();
    TestTrue(TEXT("Color curves default"),Rules.ContainsByPredicate([](const auto& R){return R->Type==TEXT("CurveLinearColor")&&R->Prefix==TEXT("CC_");}));
    TestTrue(TEXT("Color atlas default"),Rules.ContainsByPredicate([](const auto& R){return R->Type==TEXT("CurveLinearColorAtlas")&&R->Prefix==TEXT("CA_");}));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandyNamingIntegrationTest,"NiagaraParamExposer.Naming.RenameAndRedirectors",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHandyNamingIntegrationTest::RunTest(const FString&){
    if(!FParse::Param(FCommandLine::Get(),TEXT("HandyNamingFixtures"))){AddInfo(TEXT("Pass -HandyNamingFixtures only in an isolated test project."));return true;}
    const FString Root=TEXT("/Game/HandyNaming_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Disk=FPaths::ProjectSavedDir()+TEXT("NamingFixtures/")+FPackageName::GetShortName(Root)+TEXT("/");
    FPackageName::RegisterMountPoint(Root+TEXT("/"),Disk);ON_SCOPE_EXIT{FPackageName::UnRegisterMountPoint(Root+TEXT("/"),Disk);};
    auto* Texture=NewObject<UTexture2D>(CreatePackage(*(Root+TEXT("/Scope/Fire"))),TEXT("Fire"),RF_Public|RF_Standalone);
    uint8 Data[4]={42,0,0,255};Texture->Source.Init(1,1,1,1,TSF_BGRA8,Data);
    auto* MI=NewObject<UMaterialInstanceConstant>(CreatePackage(*(Root+TEXT("/Outside/MI_Consumer"))),TEXT("MI_Consumer"),RF_Public|RF_Standalone);
    MI->SetParentEditorOnly(UMaterial::GetDefaultMaterial(MD_Surface),false);FTextureParameterValue V;V.ParameterInfo=FMaterialParameterInfo(TEXT("FireColor"));V.ParameterValue=Texture;MI->TextureParameterValues.Add(V);MI->PostEditChange();
    for(UObject* O:TArray<UObject*>{Texture,MI}){
        FString Filename=FPackageName::LongPackageNameToFilename(O->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
        if(!TestTrue(TEXT("Test writes only isolated fixture directory"),FPaths::ConvertRelativePathToFull(Filename).StartsWith(FPaths::ConvertRelativePathToFull(Disk))))return false;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
        TestTrue(TEXT("Fixture saved"),UPackage::SavePackage(O->GetOutermost(),O,*Filename,Args));FAssetRegistryModule::AssetCreated(O);
    }
    auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();Registry.SearchAllAssets(true);Registry.ScanPathsSynchronous({Root},true);
    auto Assets=HandySelection::Gather({Root+TEXT("/Scope")},{});auto Rows=HandyNaming::Preview(Assets,HandyNaming::Defaults(),{},{});
    if(!TestEqual(TEXT("Exactly scoped asset in preview"),Rows.Num(),1))return false;
    TestEqual(TEXT("Texture prefix"),Rows[0]->Proposed,FString(TEXT("T_Fire")));TestTrue(TEXT("Ready to rename"),Rows[0]->Ready);
    // Exercise the real native fix-up report only in this explicit isolated test.
    auto& Slate=FSlateApplication::Get();int32 Reports=0;
    auto Handle=Slate.GetOnModalLoopTickEvent().AddLambda([&](float){
        auto Window=Slate.GetActiveModalWindow();if(!Window||Window->GetTitle().ToString()!=TEXT("Redirector Update Report"))return;
        TSharedPtr<SButton> Delete,Keep;
        TFunction<FString(TSharedRef<SWidget>)> Labels=[&](TSharedRef<SWidget> W){FString T;if(W->GetType()==TEXT("STextBlock"))T=StaticCastSharedRef<STextBlock>(W)->GetText().ToString();auto* C=W->GetChildren();for(int32 I=0;I<C->Num();++I)T+=Labels(C->GetChildAt(I));return T;};
        TFunction<void(TSharedRef<SWidget>)> Visit=[&](TSharedRef<SWidget> W){if(W->GetType()==TEXT("SButton")){FString T=Labels(W);if(T.Contains(TEXT("Delete Unreferenced Redirectors")))Delete=StaticCastSharedRef<SButton>(W);else if(T.Contains(TEXT("Keep"))||T==TEXT("OK"))Keep=StaticCastSharedRef<SButton>(W);}auto* C=W->GetChildren();for(int32 I=0;I<C->Num();++I)Visit(C->GetChildAt(I));};
        Visit(Window.ToSharedRef());if(Delete){++Reports;Delete->SimulateClick();}else if(Keep)Keep->SimulateClick();
    });ON_SCOPE_EXIT{Slate.GetOnModalLoopTickEvent().Remove(Handle);};
    TArray<HandyNaming::FHistory> History;FString Report;
    TestTrue(*Report,HandyNaming::Apply(Rows,History,Report));AddInfo(Report);
    TestEqual(TEXT("Asset renamed"),Texture->GetName(),FString(TEXT("T_Fire")));
    TestTrue(TEXT("External material still references same texture"),MI->TextureParameterValues[0].ParameterValue==Texture);
    Registry.ScanPathsSynchronous({Root},true);TArray<FName> Dependencies;Registry.GetDependencies(MI->GetOutermost()->GetFName(),Dependencies);
    TestTrue(TEXT("Saved external reference points to new package"),Dependencies.Contains(Texture->GetOutermost()->GetFName()));
    TestFalse(TEXT("Old redirector file removed after fix-up"),FPackageName::DoesPackageExist(Root+TEXT("/Scope/Fire")));
    AddInfo(FString::Printf(TEXT("Native redirector reports: %d (none needed when rename repairs references directly)."),Reports));
    TestTrue(TEXT("Revert succeeds"),HandyNaming::Revert(History,Report));AddInfo(Report);
    TestEqual(TEXT("Revert restores original name"),Texture->GetName(),FString(TEXT("Fire")));
    TestTrue(TEXT("Revert preserves outside reference"),MI->TextureParameterValues[0].ParameterValue==Texture);
    return true;
}
#endif
