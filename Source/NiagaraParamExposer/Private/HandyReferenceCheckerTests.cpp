#if WITH_DEV_AUTOMATION_TESTS
#include "HandyReferenceChecker.h"
#include "Misc/AutomationTest.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "Misc/Guid.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/ScopeExit.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandyReferenceTest,"NiagaraParamExposer.References.IdentityAndUndo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHandyReferenceTest::RunTest(const FString&)
{
    using namespace HandyReferences;
    TestTrue(TEXT("Root child included"),Inside(TEXT("/Game/Pack/T"),TEXT("/Game/Pack")));
    TestFalse(TEXT("Sibling prefix excluded"),Inside(TEXT("/Game/PackExtra/T"),TEXT("/Game/Pack")));
    const FString Base=TEXT("/Game/HandyReferenceTest_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto MakeTexture=[&](const FString& Path,uint8 Pixel){
        auto* T=NewObject<UTexture2D>(CreatePackage(*Path),TEXT("Texture"),RF_Public|RF_Standalone|RF_Transactional);
        uint8 Data[4]={Pixel,0,0,255};T->Source.Init(1,1,1,1,TSF_BGRA8,Data);return T;
    };
    auto* Outside=MakeTexture(Base+TEXT("/Outside/T"),42);
    auto* Local=DuplicateObject<UTexture2D>(Outside,CreatePackage(*(Base+TEXT("/Pack/T"))),TEXT("Texture"));
    TestTrue(TEXT("Duplicated texture verified"),IdenticalTextures(Outside,Local));
    Local->SRGB=!Outside->SRGB;
    TestFalse(TEXT("Different settings rejected"),IdenticalTextures(Outside,Local));Local->SRGB=Outside->SRGB;
    uint8 Different[4]={43,0,0,255};Local->Source.Init(1,1,1,1,TSF_BGRA8,Different);
    TestFalse(TEXT("Different pixels rejected"),IdenticalTextures(Outside,Local));
    uint8 Same[4]={42,0,0,255};Local->Source.Init(1,1,1,1,TSF_BGRA8,Same);
    TestTrue(TEXT("Same pixels despite different source ID"),IdenticalTextures(Outside,Local));
    auto* Material=NewObject<UMaterial>(CreatePackage(*(Base+TEXT("/Outside/M"))),TEXT("M"),RF_Public|RF_Standalone|RF_Transactional);
    auto* Sample=NewObject<UMaterialExpressionTextureSample>(Material);Sample->Texture=Outside;Sample->Material=Material;Material->GetExpressionCollection().AddExpression(Sample);Material->PostEditChange();
    auto* MaterialCopy=DuplicateObject<UMaterial>(Material,CreatePackage(*(Base+TEXT("/Pack/MCopy"))),TEXT("MCopy"));
    TestTrue(TEXT("Renamed duplicated material graph verified"),IdenticalAssets(Material,MaterialCopy));
    MaterialCopy->TwoSided=!Material->TwoSided;
    TestFalse(TEXT("Different material settings rejected"),IdenticalAssets(Material,MaterialCopy));MaterialCopy->TwoSided=Material->TwoSided;
    Material->GetOutermost()->SetDirtyFlag(false);MaterialCopy->GetOutermost()->SetDirtyFlag(false);
    auto* Instance=NewObject<UMaterialInstanceConstant>(CreatePackage(*(Base+TEXT("/Pack/MI"))),TEXT("MI"),RF_Public|RF_Standalone|RF_Transactional);
    Instance->SetParentEditorOnly(UMaterial::GetDefaultMaterial(MD_Surface),false);
    FTextureParameterValue V;V.ParameterInfo=FMaterialParameterInfo(TEXT("ColorMask"));V.ParameterValue=Outside;Instance->TextureParameterValues.Add(V);
    Instance->PostEditChange();
    auto* ExternalInstance=DuplicateObject<UMaterialInstanceConstant>(Instance,CreatePackage(*(Base+TEXT("/Outside/MI"))),TEXT("MI"));
    if (FParse::Param(FCommandLine::Get(),TEXT("HandyCleanupFixtures"))) {
        const FString FixtureMount=Base+TEXT("/");
        const FString FixtureDisk=FPaths::ProjectContentDir()+TEXT("ReferenceFixtures/")+FPackageName::GetShortName(Base)+TEXT("/");
        FPackageName::RegisterMountPoint(FixtureMount,FixtureDisk);
        ON_SCOPE_EXIT { FPackageName::UnRegisterMountPoint(FixtureMount,FixtureDisk); };
        for(UObject* O:TArray<UObject*>{Outside,Local,Instance,ExternalInstance}) {
            FString Filename=FPackageName::LongPackageNameToFilename(O->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
            if(!TestTrue(TEXT("Fixture confined to test project"),FPaths::ConvertRelativePathToFull(Filename).StartsWith(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()))))return false;
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
            FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
            TestTrue(TEXT("Saved reference fixture"),UPackage::SavePackage(O->GetOutermost(),O,*Filename,Args));FAssetRegistryModule::AssetCreated(O);
        }
        FString ScanReport;auto Results=Scan(Base+TEXT("/Pack"),ScanReport);
        auto OtherScope=Scan(Base+TEXT("/Outside"),ScanReport);
        TestTrue(TEXT("Independent second folder has local texture"),OtherScope.ContainsByPredicate([&](const auto& R){return R->Target==Outside->GetPathName()&&!R->Outside&&R->Scope==Base+TEXT("/Outside");}));
        TestTrue(TEXT("First folder still sees second-folder texture as external"),Results.ContainsByPredicate([&](const auto& R){return R->Target==Outside->GetPathName()&&R->Outside&&R->Status==TEXT("EXTERNAL")&&R->Scope==Base+TEXT("/Pack");}));
        bool Detailed=false,Matched=false,Allowed=false;
        for(const auto& R:Results){
            if(R->Target==Outside->GetPathName()){Detailed|=R->Detail.Contains(TEXT("ColorMask"));Matched|=R->Replacement==Local->GetPathName();TestTrue(TEXT("External texture highlighted"),R->Outside);}
            if(R->Target.StartsWith(TEXT("/Engine/"))){Allowed=true;TestFalse(TEXT("Engine is allowed"),R->Outside);}
        }
        TestTrue(*ScanReport,Results.Num()>0);TestTrue(TEXT("Material texture parameter identified"),Detailed);TestTrue(TEXT("Unique identical replacement discovered"),Matched);TestTrue(TEXT("Engine dependency listed"),Allowed);
    }
    TestTrue(TEXT("Identical material instance verified"),IdenticalAssets(Instance,ExternalInstance));
    ExternalInstance->TextureParameterValues[0].ParameterValue=Local;
    TestFalse(TEXT("Different material dependency rejected"),IdenticalAssets(Instance,ExternalInstance));
    ExternalInstance->TextureParameterValues[0].ParameterValue=Outside;
    auto Row=MakeShared<FRow>();Row->Source=Instance->GetPathName();Row->Target=Outside->GetPathName();Row->Replacement=Local->GetPathName();Row->Selected=true;
    FString Report;
    TestTrue(TEXT("Scoped replacement succeeds"),Relink(Base+TEXT("/Pack"),{Row},Report));
    TestTrue(TEXT("Local reference changed"),Instance->TextureParameterValues[0].ParameterValue==Local);
    TestTrue(TEXT("Outside referencer untouched"),ExternalInstance->TextureParameterValues[0].ParameterValue==Outside);
    GEditor->UndoTransaction();
    TestTrue(TEXT("Undo restores reference"),Instance->TextureParameterValues[0].ParameterValue==Outside);
    GEditor->RedoTransaction();
    TestTrue(TEXT("Redo restores replacement"),Instance->TextureParameterValues[0].ParameterValue==Local);
    Row->Source=ExternalInstance->GetPathName();
    TestFalse(TEXT("Outside source refused"),Relink(Base+TEXT("/Pack"),{Row},Report));
    for (UObject* O:TArray<UObject*>{Outside,Local,Instance,ExternalInstance})O->GetOutermost()->SetDirtyFlag(false);
    return true;
}
#endif
