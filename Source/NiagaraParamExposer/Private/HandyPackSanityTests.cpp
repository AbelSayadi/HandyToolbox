#include "HandyPackSanity.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandySanityRulesTest,"NiagaraParamExposer.Sanity.Thresholds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHandySanityRulesTest::RunTest(const FString&){
 HandySanity::FOptions O;TStrongObjectPtr<UTexture2D> T(NewObject<UTexture2D>());FAssetData A(T.Get());TArray<TSharedPtr<HandySanity::FRow>> Rows;
 HandySanity::InspectTexture(2048,1024,O,TEXT("/Game/Test"),A,Rows);TestEqual(TEXT("2K warns by default"),Rows.Num(),1);TestEqual(TEXT("2K is review warning, not error"),Rows[0]->Severity,1);TestTrue(TEXT("Flipbook exception explained"),Rows[0]->Detail.Contains(TEXT("flipbook")));
 Rows.Reset();HandySanity::InspectTexture(1024,512,O,TEXT("/Game/Test"),A,Rows);TestEqual(TEXT("Rectangular POT passes"),Rows.Num(),0);
 HandySanity::InspectTexture(300,128,O,TEXT("/Game/Test"),A,Rows);TestEqual(TEXT("NPOT warning"),Rows.Num(),1);TestEqual(TEXT("NPOT rule"),Rows[0]->Check,FString(TEXT("Texture power of two")));
 Rows.Reset();O.TextureWarning=4096;HandySanity::InspectTexture(2048,2048,O,TEXT("/Game/Test"),A,Rows);TestEqual(TEXT("Threshold override respected"),Rows.Num(),0);
 HandySanity::InspectMesh(20000,O,TEXT("/Game/Test"),A,Rows);TestEqual(TEXT("At mesh budget passes"),Rows.Num(),0);HandySanity::InspectMesh(20001,O,TEXT("/Game/Test"),A,Rows);TestEqual(TEXT("Above mesh budget warns"),Rows.Num(),1);
 TestFalse(TEXT("Reject engine folder"),HandySanity::Scan({TEXT("/Engine")},{},O).Error.IsEmpty());O.TextureWarning=0;TestFalse(TEXT("Reject invalid limits"),HandySanity::Scan({TEXT("/Game")},{},O).Error.IsEmpty());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandySanityPackTest,"NiagaraParamExposer.Sanity.SavedPack",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHandySanityPackTest::RunTest(const FString&){
 if(!FParse::Param(FCommandLine::Get(),TEXT("HandySanityFixtures"))){AddInfo(TEXT("Use -HandySanityFixtures in an isolated project for the saved-pack scan."));return true;}
 const FString Root=TEXT("/Game/HandySanity_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);const FString A=Root+TEXT("/A"),B=Root+TEXT("/B");
 // Earlier integration tests mount source content read-only at /Game. Override
 // that mount for this saved fixture so dependency refresh covers the isolated Content.
 const FString Disk=FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
 FPackageName::RegisterMountPoint(TEXT("/Game/"),Disk);
 ON_SCOPE_EXIT{FPackageName::UnRegisterMountPoint(TEXT("/Game/"),Disk);};
 TArray<UObject*> Objects;
 auto Make=[&](UClass* C,const FString& Path){auto* O=NewObject<UObject>(CreatePackage(*Path),C,FName(*FPackageName::GetShortName(Path)),RF_Public|RF_Standalone);Objects.Add(O);return O;};
 auto Texture=[&](const FString& Path,int32 W,int32 H){auto* T=CastChecked<UTexture2D>(Make(UTexture2D::StaticClass(),Path));TArray<uint8> Pixels;Pixels.Init(64,W*H*4);T->Source.Init(W,H,1,1,TSF_BGRA8,Pixels.GetData());return T;};
 auto* External=Texture(B+TEXT("/T_Atlas"),2048,4);auto* Local=Texture(A+TEXT("/T_LocalAtlas"),2048,4);auto* Bad=Texture(A+TEXT("/BadTexture"),300,128);Texture(A+TEXT("/T_Orphan"),16,16);Texture(A+TEXT("/T_OrphanCopy"),16,16);
 auto* Material=CastChecked<UMaterial>(Make(UMaterial::StaticClass(),A+TEXT("/M_Base")));
 auto* MI=CastChecked<UMaterialInstanceConstant>(Make(UMaterialInstanceConstant::StaticClass(),A+TEXT("/MI_Effect")));MI->SetParentEditorOnly(Material,false);FTextureParameterValue V;V.ParameterInfo=FMaterialParameterInfo(TEXT("Atlas"));V.ParameterValue=External;MI->TextureParameterValues.Add(V);
 Make(UMaterialInstanceConstant::StaticClass(),A+TEXT("/MI_NoParent"));
 auto* System=CastChecked<UNiagaraSystem>(Make(UNiagaraSystem::StaticClass(),A+TEXT("/NS_Effect")));UNiagaraSystemFactoryNew::InitializeSystem(System,true);FNiagaraVariable Binding(FNiagaraTypeDefinition(UMaterialInterface::StaticClass()),TEXT("User.Material"));System->GetExposedParameters().AddParameter(Binding);System->GetExposedParameters().SetUObject(MI,Binding);
 auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));if(!TestNotNull(TEXT("Engine cube fixture"),Cube))return false;auto* Mesh=DuplicateObject<UStaticMesh>(Cube,CreatePackage(*(A+TEXT("/SM_Dense"))),TEXT("SM_Dense"));Mesh->SetFlags(RF_Public|RF_Standalone);Objects.Add(Mesh);
 for(auto* Object:Objects){auto* Package=Object->GetOutermost();const FString File=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());if(!TestTrue(TEXT("Fixture saved only inside test project"),FPaths::IsUnderDirectory(File,FPaths::ProjectContentDir())))return false;IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;if(!TestTrue(TEXT("Save fixture"),UPackage::SavePackage(Package,Object,*File,Args)))return false;FAssetRegistryModule::AssetCreated(Object);}
 FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().ScanPathsSynchronous({Root},true);
 HandySanity::FOptions O;O.TriangleWarning=1;auto Result=HandySanity::Scan({A,B},{},O);if(!TestTrue(*Result.Error,Result.Error.IsEmpty()))return false;TestEqual(TEXT("Independent scopes"),Result.Scopes,2);
 auto Has=[&](const FString& Scope,const FString& Asset,const FString& Check){return Result.Rows.ContainsByPredicate([&](const auto& R){return R->Scope==Scope&&R->Asset==Asset&&R->Check==Check;});};
 TestTrue(TEXT("Cross-selected-folder reference remains external"),Has(A,MI->GetPathName(),TEXT("External reference")));TestFalse(TEXT("Same reference is not merged into other scope"),Has(B,MI->GetPathName(),TEXT("External reference")));
 TestTrue(TEXT("2K atlas flagged"),Has(A,Local->GetPathName(),TEXT("Texture resolution")));TestTrue(TEXT("NPOT source detected"),Has(A,Bad->GetPathName(),TEXT("Texture power of two")));TestTrue(TEXT("Incorrect prefix flagged"),Has(A,Bad->GetPathName(),TEXT("Naming")));TestFalse(TEXT("Correct texture prefix retained"),Has(A,Local->GetPathName(),TEXT("Naming")));
 TestTrue(TEXT("Mesh triangle warning uses geometry"),Has(A,Mesh->GetPathName(),TEXT("Mesh triangles")));TestTrue(TEXT("Missing material parent error"),Has(A,A+TEXT("/MI_NoParent.MI_NoParent"),TEXT("Missing material parent")));
 TestTrue(TEXT("Orphan reported for review"),Has(A,A+TEXT("/T_Orphan.T_Orphan"),TEXT("Unused content")));TestFalse(TEXT("Niagara-used external texture protected"),Has(B,External->GetPathName(),TEXT("Unused content")));
 TestTrue(TEXT("Local duplicate reported"),Has(A,A+TEXT("/T_Orphan.T_Orphan"),TEXT("Verified duplicate")));
 TestTrue(TEXT("External duplicate replacement identified"),Result.Rows.ContainsByPredicate([&](const auto& R){return R->Check==TEXT("External reference")&&R->Detail.Contains(Local->GetPathName());}));
 TestEqual(TEXT("Scan preserves material reference"),MI->TextureParameterValues[0].ParameterValue.Get(),static_cast<UTexture*>(External));for(auto* Object:Objects)TestFalse(TEXT("Scan does not dirty fixture"),Object->GetOutermost()->IsDirty());
 return true;
}
#endif
