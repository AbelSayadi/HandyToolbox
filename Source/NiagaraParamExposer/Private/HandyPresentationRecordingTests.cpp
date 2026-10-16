#include "HandyPresentationRender.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "HandyPresentation.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "MovieSceneObjectBindingID.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelinePrimaryConfig.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineAntiAliasingSetting.h"
#include "MovieScene/MovieSceneNiagaraSystemTrack.h"
#include "SceneView.h"
#include "LevelEditorViewport.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "Camera/CameraActor.h"
#include "Editor.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "Misc/ScopeExit.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandyRecordingPlanTest,"NiagaraParamExposer.Recording.Configuration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHandyRecordingPlanTest::RunTest(const FString&){
 auto* World=GEditor->GetEditorWorldContext().World();bool Dirty=World->GetOutermost()->IsDirty();FActorSpawnParameters P;P.ObjectFlags=RF_Transient;auto* Camera=World->SpawnActor<ACameraActor>(P);ON_SCOPE_EXIT{if(Camera)Camera->Destroy();World->GetOutermost()->SetDirtyFlag(Dirty);};
 TStrongObjectPtr<UNiagaraSystem> System(NewObject<UNiagaraSystem>());UNiagaraSystemFactoryNew::InitializeSystem(System.Get(),true);
 TStrongObjectPtr<ULevelSequence> Sequence(NewObject<ULevelSequence>());Sequence->Initialize();TStrongObjectPtr<UHandyPresentationData> List(NewObject<UHandyPresentationData>());List->Attach=false;List->Items.SetNum(2);for(auto& I:List->Items)I.System=System.Get();List->Items[1].Mode=2;List->Items[1].Duration=2;
 FString Error;if(!TestTrue(TEXT("Build fixture"),HandyPresentation::Build(Sequence.Get(),*List,nullptr,Error)))return false;
 TStrongObjectPtr<UHandyRenderSession> Session(NewObject<UHandyRenderSession>());FHandyRenderOptions O;O.ExposureMode=2;O.Output=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("RecordingConfig"));O.FPS=24;
 TestTrue(TEXT("Prepare render jobs"),Session->Prepare(Sequence.Get(),*List,O,Camera,Error));if(!TestEqual(TEXT("One job per VFX"),Session->Clips.Num(),2))return false;
 TestEqual(TEXT("Loop duration frames"),Session->Clips[0].End-Session->Clips[0].Start,120);TestEqual(TEXT("Override duration frames"),Session->Clips[1].End-Session->Clips[1].Start,48);TestEqual(TEXT("Gap excluded"),Session->Clips[1].Start,144);TestEqual(TEXT("Loop warm-up"),Session->Clips[0].WarmUp,32);TestEqual(TEXT("Once never warms away initial burst"),Session->Clips[1].WarmUp,0);
 for(int32 I=0;I<2;++I){auto* Job=Session->Queue->GetJobs()[I];auto* Out=CastChecked<UMoviePipelineOutputSetting>(Job->GetConfiguration()->FindOrAddSettingByClass(UMoviePipelineOutputSetting::StaticClass()));TestEqual(TEXT("Output FPS"),Out->OutputFrameRate,FFrameRate(24,1));TestEqual(TEXT("Resolution"),Out->OutputResolution,FIntPoint(1920,1080));TestNull(TEXT("Other effect removed from render copy"),Session->Copies[I]->GetMovieScene()->FindSpawnable(Sequence->FindMetaData<UHandyPresentationData>()->Actors[1-I]));}
 TestEqual(TEXT("Original actors untouched"),Sequence->GetMovieScene()->GetSpawnableCount(),2);TestNull(TEXT("Original camera cuts untouched"),Sequence->GetMovieScene()->GetCameraCutTrack());
 List->Items[0].Render=false;O.SelectedOnly=true;TestTrue(TEXT("Selected-only prepare"),Session->Prepare(Sequence.Get(),*List,O,Camera,Error));TestEqual(TEXT("One selected job"),Session->Clips.Num(),1);TestEqual(TEXT("Correct selected row"),Session->Clips[0].Index,1);
 O.Width=1919;TestFalse(TEXT("Odd H264 dimension rejected"),HandyRecording::Validate(O,Error));O.Width=1920;List->Duration=9;TestFalse(TEXT("Stale build rejected"),Session->Prepare(Sequence.Get(),*List,O,Camera,Error));List->Duration=5;
 TestFalse(TEXT("Missing camera cuts rejected"),Session->Prepare(Sequence.Get(),*List,O,nullptr,Error));
 auto* M=Sequence->GetMovieScene();auto* Template=DuplicateObject<ACameraActor>(Camera,M);Template->ClearFlags(RF_Transient);FGuid CameraID=M->AddSpawnable(TEXT("RecordedCamera"),*Template);auto* Spawn=M->AddTrack<UMovieSceneSpawnTrack>(CameraID);auto* SpawnSection=CastChecked<UMovieSceneSpawnSection>(Spawn->CreateNewSection());SpawnSection->SetRange(TRange<FFrameNumber>::All());SpawnSection->GetChannel().SetDefault(true);Spawn->AddSection(*SpawnSection);
 auto* Cuts=CastChecked<UMovieSceneCameraCutTrack>(M->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));auto* Cut=CastChecked<UMovieSceneCameraCutSection>(Cuts->CreateNewSection());Cut->SetRange(TRange<FFrameNumber>::All());Cut->SetCameraBindingID(FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(CameraID)));Cuts->AddSection(*Cut);
 TestTrue(TEXT("Existing camera cuts accepted"),Session->Prepare(Sequence.Get(),*List,O,nullptr,Error));Cut->SetCameraBindingID(FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(FGuid::NewGuid())));TestFalse(TEXT("Broken camera binding rejected before rendering"),Session->Prepare(Sequence.Get(),*List,O,nullptr,Error));

 List->SpawnKeys=false;TestTrue(TEXT("Rebuild natural fade"),HandyPresentation::Build(Sequence.Get(),*List,nullptr,Error));O.SelectedOnly=false;O.ExposureMode=1;O.EV100=7.25f;O.FadeOut=1;
 TestTrue(TEXT("Prepare fixed EV and tail"),Session->Prepare(Sequence.Get(),*List,O,Camera,Error));
 TestEqual(TEXT("Fade tail extends clip only"),Session->Clips[0].End-Session->Clips[0].Start,144);TestEqual(TEXT("Tail does not shift second start"),Session->Clips[1].Start,144);
 auto* Exposure=Session->Queue->GetJobs()[0]->GetConfiguration()->FindSetting<UHandyExposureSetting>();if(TestNotNull(TEXT("Fixed exposure configured"),Exposure)){TestEqual(TEXT("EV100 copied exactly"),Exposure->EV100,7.25f);FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(nullptr,nullptr,FEngineShowFlags(ESFIM_Game)).SetTime(FGameTime()));Exposure->SetupViewFamily(Family);TestTrue(TEXT("Renderer fixed exposure enabled"),Family.ExposureSettings.bFixed);TestEqual(TEXT("Renderer uses viewport-equivalent EV100"),Family.ExposureSettings.FixedEV100,7.25f);}
 auto* CopyScene=Session->Copies[0]->GetMovieScene();auto* Saved=Sequence->FindMetaData<UHandyPresentationData>();auto* NoKeys=CastChecked<UMovieSceneSpawnSection>(CopyScene->FindTrack<UMovieSceneSpawnTrack>(Saved->Actors[0])->GetAllSections()[0]);TestEqual(TEXT("Recording does not re-add spawn keys"),NoKeys->GetChannel().GetData().GetTimes().Num(),0);auto* Life=CopyScene->FindTrack<UMovieSceneNiagaraSystemTrack>(Saved->Components[0]);TestEqual(TEXT("Lifecycle ends before fade tail"),Life->GetAllSections()[0]->GetExclusiveEndFrame(),CopyScene->GetTickResolution().AsFrameNumber(5));
 O.ExposureMode=2;TestTrue(TEXT("Scene exposure prepare"),Session->Prepare(Sequence.Get(),*List,O,Camera,Error));TestNull(TEXT("Scene exposure not overridden"),Session->Queue->GetJobs()[0]->GetConfiguration()->FindSetting<UHandyExposureSetting>());
 if(auto* Viewport=GCurrentLevelEditingViewportClient){auto Previous=Viewport->ExposureSettings;ON_SCOPE_EXIT{Viewport->ExposureSettings=Previous;};Viewport->ExposureSettings.bFixed=true;Viewport->ExposureSettings.FixedEV100=4.5;O.ExposureMode=0;TestTrue(TEXT("Viewport exposure prepare"),Session->Prepare(Sequence.Get(),*List,O,Camera,Error));auto* E=Session->Queue->GetJobs()[0]->GetConfiguration()->FindSetting<UHandyExposureSetting>();if(TestNotNull(TEXT("Viewport exposure copied"),E))TestEqual(TEXT("Viewport EV100 exact"),E->EV100,4.5f);}
 return true;
}
class FHandyRecordingWait:public IAutomationLatentCommand {
 FAutomationTestBase* Test;double Deadline;TWeakObjectPtr<ACameraActor> Camera;TWeakObjectPtr<UPackage> WorldPackage;bool Dirty;
public:
 FHandyRecordingWait(FAutomationTestBase* InTest,ACameraActor* InCamera,bool InDirty):Test(InTest),Deadline(FPlatformTime::Seconds()+240),Camera(InCamera),WorldPackage(InCamera->GetWorld()->GetOutermost()),Dirty(InDirty){}
 bool Update() override {
  auto* S=HandyRecording::Session();if(S->Busy&&FPlatformTime::Seconds()<Deadline)return false;
  if(S->Busy){S->Cancel();Test->AddError(TEXT("Recording timed out."));return false;}
  for(auto* Job:S->Queue->GetJobs()){auto* E=Job->GetConfiguration()->FindSetting<UHandyExposureSetting>();if(Test->TestNotNull(TEXT("Render has fixed exposure"),E))Test->TestTrue(TEXT("Exposure reached actual render views"),E->AppliedViews>0);}
  Test->TestEqual(TEXT("Two MP4 files encoded"),S->Encoded,2);Test->AddInfo(S->Status);
  for(const auto& Clip:S->Clips){Test->TestTrue(TEXT("MP4 exists"),IFileManager::Get().FileSize(*Clip.OutputFile)>0);FString Probe=FPaths::GetPath(S->Options.Encoder)/TEXT("ffprobe.exe");if(FPaths::FileExists(Probe)){int32 Code=-1;FString Out,Err;FString Args=FString::Printf(TEXT("-v error -select_streams v:0 -count_frames -show_entries stream=nb_read_frames,width,height,r_frame_rate -of default=noprint_wrappers=1 \"%s\""),*Clip.OutputFile);FPlatformProcess::ExecProcess(*Probe,*Args,&Code,&Out,&Err);Test->TestEqual(TEXT("ffprobe succeeds"),Code,0);Test->TestTrue(TEXT("Correct clip frame count"),Out.Contains(FString::Printf(TEXT("nb_read_frames=%d"),Clip.End-Clip.Start)));Test->TestTrue(TEXT("Correct FPS"),Out.Contains(FString::Printf(TEXT("r_frame_rate=%d/1"),S->Options.FPS)));Test->TestTrue(TEXT("Correct width"),Out.Contains(TEXT("width=160")));Test->TestTrue(TEXT("Correct height"),Out.Contains(TEXT("height=90")));Test->AddInfo(Out);}}
  if(Camera.IsValid())Camera->Destroy();if(WorldPackage.IsValid())WorldPackage->SetDirtyFlag(Dirty);return true;
 }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandyRecordingSmokeTest,"NiagaraParamExposer.Recording.RenderAndEncode",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHandyRecordingSmokeTest::RunTest(const FString&){
 if(!FParse::Param(FCommandLine::Get(),TEXT("HandyRenderFixtures"))){AddInfo(TEXT("Use -HandyRenderFixtures only in an isolated test project for actual render/encode smoke test."));return true;}
 auto* World=GEditor->GetEditorWorldContext().World();bool Dirty=World->GetOutermost()->IsDirty();FActorSpawnParameters P;P.ObjectFlags=RF_Transient;auto* Camera=World->SpawnActor<ACameraActor>(P);if(!TestNotNull(TEXT("Fixture camera"),Camera))return false;
 Camera->SetActorLocation(FVector(-300,0,100));Camera->SetActorRotation(FRotator(-15,0,0));
 auto* System=NewObject<UNiagaraSystem>();UNiagaraSystemFactoryNew::InitializeSystem(System,true);auto* Sequence=NewObject<ULevelSequence>();Sequence->Initialize();auto* List=NewObject<UHandyPresentationData>();List->Attach=false;List->SpawnKeys=false;List->Duration=.4;List->Gap=.2;List->Items.SetNum(2);for(auto& I:List->Items)I.System=System;List->Items[1].Mode=2;
 FString Error;if(!HandyPresentation::Build(Sequence,*List,nullptr,Error)){AddError(Error);Camera->Destroy();return false;}
 FHandyRenderOptions O;O.ExposureMode=2;O.Width=160;O.Height=90;O.FPS=30;O.Quality=0;O.WarmUp=2;O.KeepFrames=true;O.ExposureMode=1;O.EV100=6;O.FadeOut=.2;O.Output=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("RecordingSmoke"));O.Encoder=HandyRecording::FindEncoder();
 if(!HandyRecording::Session()->Start(Sequence,*List,O,Camera,true)){AddError(HandyRecording::Session()->Status);Camera->Destroy();World->GetOutermost()->SetDirtyFlag(Dirty);return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FHandyRecordingWait(this,Camera,Dirty));return true;
}

class FHandyRecordingCancelWait:public IAutomationLatentCommand {
 FAutomationTestBase* Test;double Started;bool Requested=false;TWeakObjectPtr<ACameraActor> Camera;TWeakObjectPtr<UPackage> Package;bool Dirty;
public:
 FHandyRecordingCancelWait(FAutomationTestBase* T,ACameraActor* C,bool D):Test(T),Started(FPlatformTime::Seconds()),Camera(C),Package(C->GetWorld()->GetOutermost()),Dirty(D){}
 bool Update() override {
  auto* S=HandyRecording::Session();if(S->Busy&&!Requested&&S->Status.StartsWith(TEXT("Rendering "))){S->Cancel();Requested=true;}
  if(S->Busy&&FPlatformTime::Seconds()-Started<60)return false;
  if(S->Busy){S->Cancel();Test->AddError(TEXT("Render cancellation did not finish within 60 seconds."));}
  Test->TestTrue(TEXT("Cancel was requested during rendering"),Requested);Test->TestFalse(TEXT("Recording stopped"),S->Busy);Test->TestTrue(TEXT("Canceled state retained"),S->Canceled);Test->TestEqual(TEXT("Canceled job not encoded as complete video"),S->Encoded,0);
  if(Camera.IsValid())Camera->Destroy();if(Package.IsValid())Package->SetDirtyFlag(Dirty);return true;
 }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandyRecordingCancelTest,"NiagaraParamExposer.Recording.Cancel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHandyRecordingCancelTest::RunTest(const FString&){
 if(!FParse::Param(FCommandLine::Get(),TEXT("HandyRenderFixtures"))){AddInfo(TEXT("Actual cancellation test needs -HandyRenderFixtures in an isolated project."));return true;}
 auto* World=GEditor->GetEditorWorldContext().World();bool Dirty=World->GetOutermost()->IsDirty();FActorSpawnParameters P;P.ObjectFlags=RF_Transient;auto* Camera=World->SpawnActor<ACameraActor>(P);if(!Camera)return false;
 auto* System=NewObject<UNiagaraSystem>();UNiagaraSystemFactoryNew::InitializeSystem(System,true);auto* Sequence=NewObject<ULevelSequence>();Sequence->Initialize();auto* List=NewObject<UHandyPresentationData>();List->Attach=false;List->Duration=10;List->Items.SetNum(1);List->Items[0].System=System;FString Error;HandyPresentation::Build(Sequence,*List,nullptr,Error);
 FHandyRenderOptions O;O.ExposureMode=2;O.Width=160;O.Height=90;O.FPS=30;O.Quality=0;O.WarmUp=0;O.Output=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("RecordingCancel"));O.Encoder=HandyRecording::FindEncoder();
 if(!HandyRecording::Session()->Start(Sequence,*List,O,Camera,true)){AddError(HandyRecording::Session()->Status);Camera->Destroy();World->GetOutermost()->SetDirtyFlag(Dirty);return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FHandyRecordingCancelWait(this,Camera,Dirty));return true;
}
#endif
