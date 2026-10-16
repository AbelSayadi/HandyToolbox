#include "HandyPresentationRender.h"
#include "HandyPresentation.h"
#include "LevelSequence.h"
#include "UniversalObjectLocatorResolveParams.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneSpawnable.h"
#include "MovieSceneObjectBindingID.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "Tracks/MovieSceneParticleTrack.h"
#include "Sections/MovieSceneParticleSection.h"
#include "Tracks/MovieScene3DAttachTrack.h"
#include "MovieScene/MovieSceneNiagaraSystemTrack.h"
#include "MovieScene/MovieSceneNiagaraSystemSpawnSection.h"
#include "MoviePipelineQueueSubsystem.h"
#include "MoviePipelineQueue.h"
#include "MoviePipeline.h"
#include "MoviePipelinePrimaryConfig.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineAntiAliasingSetting.h"
#include "MoviePipelineCameraSetting.h"
#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineImageSequenceOutput.h"
#include "Camera/CameraComponent.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "SceneView.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Containers/Ticker.h"
void UHandyExposureSetting::SetupViewFamily(FSceneViewFamily& ViewFamily){
 ViewFamily.ExposureSettings.bFixed=true;ViewFamily.ExposureSettings.FixedEV100=EV100;++AppliedViews;
}
namespace HandyRecording {
bool ViewportExposure(float& EV100,bool& Fixed){
 auto* Viewport=GCurrentLevelEditingViewportClient;
 if(!Viewport||!Viewport->IsPerspective())return false;
 EV100=Viewport->ExposureSettings.FixedEV100;Fixed=Viewport->ExposureSettings.bFixed;return true;
}
UHandyRenderSession* Session(){static TStrongObjectPtr<UHandyRenderSession> State(NewObject<UHandyRenderSession>());return State.Get();}
FString FindEncoder(){TArray<FString> Paths;FPlatformMisc::GetEnvironmentVariable(TEXT("PATH")).ParseIntoArray(Paths,TEXT(";"),true);for(auto P:Paths){P=P.TrimQuotes()/TEXT("ffmpeg.exe");if(FPaths::FileExists(P))return FPaths::ConvertRelativePathToFull(P);}return FString();}
bool CheckEncoder(const FString& Path,FString& Message){
 if(!FPaths::FileExists(Path)||Path.Contains(TEXT("\""))){Message=TEXT("Choose an existing ffmpeg.exe file.");return false;}
 int32 Code=-1;FString Out,Err;bool Ran=FPlatformProcess::ExecProcess(*Path,TEXT("-hide_banner -encoders"),&Code,&Out,&Err);
 if(!Ran||Code!=0||!Out.Contains(TEXT("libx264"))){Message=TEXT("FFmpeg could not run or has no libx264 H.264 encoder. Choose a full FFmpeg build.");return false;}
 Message=TEXT("FFmpeg verified: H.264 / MP4 available.");return true;
}
bool Validate(const FHandyRenderOptions& O,FString& Error){
 if(O.ExposureMode<0||O.ExposureMode>2||!FMath::IsFinite(O.EV100)||O.EV100< -20||O.EV100>30||!FMath::IsFinite(O.FadeOut)||O.FadeOut<0||O.FadeOut>600){Error=TEXT("Use EV100 -20 to 30 and fade-out time 0 to 600 seconds.");return false;}
 if(O.Width<16||O.Height<16||O.Width>8192||O.Height>8192||O.Width%2||O.Height%2){Error=TEXT("Use even width/height values from 16 to 8192 for H.264.");return false;}
 if(O.FPS<1||O.FPS>120||O.Quality<0||O.Quality>3||O.WarmUp<0||O.WarmUp>10000){Error=TEXT("Use FPS 1-120 and warm-up 0-10000 frames.");return false;}
 if(O.Output.IsEmpty()||FPaths::IsRelative(O.Output)||O.Output.Contains(TEXT("\""))||O.Output.Contains(TEXT("\n"))){Error=TEXT("Choose an absolute output folder.");return false;}return true;
}
}
static bool SameList(const UHandyPresentationData& A,const UHandyPresentationData& B){
 if(A.Items.Num()!=B.Items.Num()||A.Duration!=B.Duration||A.Gap!=B.Gap||A.Start!=B.Start||A.Loop!=B.Loop||A.SpawnKeys!=B.SpawnKeys||A.Attach!=B.Attach||A.Target!=B.Target||A.Socket!=B.Socket)return false;
 for(int32 I=0;I<A.Items.Num();++I)if(A.Items[I].System!=B.Items[I].System||A.Items[I].Duration!=B.Items[I].Duration||A.Items[I].Mode!=B.Items[I].Mode||A.Items[I].Attach!=B.Items[I].Attach)return false;return true;
}
// UE 5.6 can restore an uncached shot range if Shutdown runs before its first tick.
// Stop queue advancement immediately, then let the first shot initialize before
// requesting asynchronous shutdown (also keeps the editor responsive).
bool UHandyRenderExecutor::TryCancelSafely(){
 bIsCanceling=true;
 auto* Pipeline=Cast<UMoviePipeline>(ActiveMoviePipeline);
 if(!Pipeline)return false;
 const auto State=Pipeline->GetPipelineState();
 if(State==EMovieRenderPipelineState::Uninitialized)return false;
 if(State==EMovieRenderPipelineState::ProducingFrames){
  const auto& Shots=Pipeline->GetActiveShotList();const int32 Index=Pipeline->GetCurrentShotIndex();
  if(!Shots.IsValidIndex(Index)||!Shots[Index]->ShotInfo.IsInitialized())return false;
  Pipeline->RequestShutdown(true);
 }
 return true;
}
bool UHandyRenderSession::Prepare(ULevelSequence* Sequence,const UHandyPresentationData& List,const FHandyRenderOptions& Settings,AActor* Camera,FString& Error){
 if(Busy){Error=TEXT("A Presentation render is already running.");return false;}
 if(!HandyRecording::Validate(Settings,Error))return false;
 auto* Built=Sequence?Sequence->FindMetaData<UHandyPresentationData>():nullptr;
 if(!Built||!SameList(*Built,List)||Built->Actors.Num()!=List.Items.Num()||Built->Components.Num()!=List.Items.Num()){Error=TEXT("Build / Update Presentation before recording so the sequence matches this list.");return false;}
 auto* Source=Sequence->GetMovieScene();TArray<HandyPresentation::FSlot> Slots;if(!HandyPresentation::Plan(List,Source->GetTickResolution(),Slots,Error))return false;
 if(Camera&&!Camera->FindComponentByClass<UCameraComponent>()){Error=TEXT("The chosen actor has no camera component.");return false;}
 Options=Settings;
 bool FixedExposure=Options.ExposureMode==1;float ExposureEV=Options.EV100;
 if(Options.ExposureMode==0&&!HandyRecording::ViewportExposure(ExposureEV,FixedExposure)){Error=TEXT("Focus a perspective level viewport first, or choose Fixed EV100 / Camera and scene exposure.");return false;}
 Queue=NewObject<UMoviePipelineQueue>(this);Copies.Reset();Clips.Reset();
 BatchFolder=FPaths::ConvertRelativePathToFull(Options.Output)/FString::Printf(TEXT("Presentation_%s_%s"),*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")),*FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
 const FString Token=FGuid::NewGuid().ToString(EGuidFormats::Digits);const FFrameRate Rate(Options.FPS,1);
 for(int32 I=0;I<List.Items.Num();++I){
  if(Options.SelectedOnly&&!List.Items[I].Render)continue;
  if(!Source->FindSpawnable(Built->Actors[I])||!Source->FindBinding(Built->Components[I])){Error=TEXT("Generated tracks are missing. Build / Update Presentation again.");return false;}
  FHandyRenderClip Clip;Clip.Index=I;Clip.Name=FString::Printf(TEXT("%02d_%s"),I+1,*FPaths::MakeValidFileName(List.Items[I].System.GetAssetName()));
  const double Seconds=Source->GetTickResolution().AsSeconds(Slots[I].Start);const double Duration=Source->GetTickResolution().AsSeconds(Slots[I].End-Slots[I].Start);
  Clip.Start=static_cast<int32>(FMath::RoundToInt(Seconds*Options.FPS));const int64 Count=FMath::RoundToInt(Duration*Options.FPS);
  if(Count<1||static_cast<int64>(Clip.Start)+Count>=MAX_int32){Error=TEXT("A duration is shorter than one output frame or exceeds the render range.");return false;}
  const int64 Tail=FMath::RoundToInt(Options.FadeOut*Options.FPS);if(static_cast<int64>(Clip.Start)+Count+Tail>=MAX_int32){Error=TEXT("Fade-out exceeds the render range.");return false;}
  Clip.End=Clip.Start+static_cast<int32>(Count+Tail);Clip.WarmUp=Slots[I].Loop?Options.WarmUp:0;Clip.Directory=BatchFolder/TEXT("Frames")/Clip.Name;Clip.OutputFile=BatchFolder/(Clip.Name+TEXT(".mp4"));
  auto* Copy=DuplicateObject<ULevelSequence>(Sequence,GetTransientPackage(),FName(*(TEXT("HandyRender_")+Token+TEXT("_")+FString::FromInt(I))));Copy->ClearFlags(RF_Standalone|RF_Public);Copy->SetFlags(RF_Transient);Copies.Add(Copy);auto* M=Copy->GetMovieScene();M->SetReadOnly(false);M->SetDisplayRate(Rate);
  const FFrameNumber Start=FFrameRate::TransformTime(FFrameTime(Clip.Start),Rate,M->GetTickResolution()).RoundToFrame(),End=FFrameRate::TransformTime(FFrameTime(Clip.End),Rate,M->GetTickResolution()).RoundToFrame();
  const FFrameNumber EffectEnd=FFrameRate::TransformTime(FFrameTime(Clip.Start+static_cast<int32>(Count)),Rate,M->GetTickResolution()).RoundToFrame();
  FMovieSceneObjectBindingID CameraID;
  if(Camera){FGuid ID=Copy->FindBindingFromObject(Camera,Camera->GetWorld());if(!ID.IsValid()){auto* CameraTemplate=DuplicateObject<AActor>(Camera,M);CameraTemplate->ClearFlags(RF_Transient);ID=M->AddSpawnable(Camera->GetActorLabel(),*CameraTemplate);auto* CameraSpawn=M->AddTrack<UMovieSceneSpawnTrack>(ID);auto* CameraSection=CastChecked<UMovieSceneSpawnSection>(CameraSpawn->CreateNewSection());CameraSection->SetRange(TRange<FFrameNumber>::All());CameraSection->GetChannel().SetDefault(true);CameraSpawn->AddSection(*CameraSection);}CameraID=FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(ID));}
  else if(auto* CutTrack=Source->GetCameraCutTrack())for(auto* S:CutTrack->GetAllSections())if(auto* Cut=Cast<UMovieSceneCameraCutSection>(S))if(Cut->IsActive()&&Cut->GetRange().Contains(TRange<FFrameNumber>(Slots[I].Start,Options.FadeOut>0?End:Slots[I].End)))CameraID=Cut->GetCameraBindingID();
  if(!CameraID.IsValid()){Error=TEXT("Each clip needs a Camera Cut covering its full range, including fade-out seconds. Choose a camera actor here, or add suitable Camera Cuts to the presentation sequence.");return false;}
  if(!Camera){
   bool Resolved=false;const FGuid ID=CameraID.GetGuid();
   if(auto* Spawnable=Source->FindSpawnable(ID))if(auto* Actor=Cast<AActor>(Spawnable->GetObjectTemplate()))Resolved=Actor->FindComponentByClass<UCameraComponent>()!=nullptr;
   if(!Resolved){TArray<UObject*,TInlineAllocator<1>> Objects;Sequence->LocateBoundObjects(ID,UE::UniversalObjectLocator::FResolveParams(GEditor->GetEditorWorldContext().World()),Objects);for(auto* O:Objects)if(auto* Actor=Cast<AActor>(O))Resolved|=Actor->FindComponentByClass<UCameraComponent>()!=nullptr;}
   if(!Resolved){Error=TEXT("A Camera Cut has an unresolved camera binding. Select a camera actor in Presentation, or repair the sequence's camera binding.");return false;}
  }
  M->RemoveCameraCutTrack();auto* Cuts=CastChecked<UMovieSceneCameraCutTrack>(M->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));auto* Cut=CastChecked<UMovieSceneCameraCutSection>(Cuts->CreateNewSection());Cut->SetCameraBindingID(CameraID);Cut->SetRange(TRange<FFrameNumber>(Start,End));Cuts->AddSection(*Cut);
  for(int32 Other=0;Other<Built->Actors.Num();++Other)if(Other!=I){Copy->UnbindPossessableObjects(Built->Components[Other]);M->RemovePossessable(Built->Components[Other]);M->RemoveSpawnable(Built->Actors[Other]);}
  auto* Spawn=M->FindTrack<UMovieSceneSpawnTrack>(Built->Actors[I]);if(!Spawn||Spawn->GetAllSections().IsEmpty()){Error=TEXT("Spawn track missing. Build the presentation again.");return false;}
  auto* SpawnSection=CastChecked<UMovieSceneSpawnSection>(Spawn->GetAllSections()[0]);SpawnSection->GetChannel().Reset();SpawnSection->GetChannel().SetDefault(!List.SpawnKeys);if(List.SpawnKeys){SpawnSection->GetChannel().GetData().AddKey(Start,true);SpawnSection->GetChannel().GetData().AddKey(EffectEnd,false);}
  if(auto* Life=M->FindTrack<UMovieSceneNiagaraSystemTrack>(Built->Components[I]))for(auto* S:Life->GetAllSections())S->SetRange(TRange<FFrameNumber>(Start,EffectEnd));
  if(auto* Once=M->FindTrack<UMovieSceneParticleTrack>(Built->Components[I]))for(auto* S:Once->GetAllSections()){auto* P=CastChecked<UMovieSceneParticleSection>(S);P->SetRange(TRange<FFrameNumber>(Start,EffectEnd));P->ParticleKeys.Reset();P->ParticleKeys.GetData().AddKey(Start,static_cast<uint8>(EParticleKey::Trigger));}
  if(auto* Attach=M->FindTrack<UMovieScene3DAttachTrack>(Built->Actors[I]))for(auto* S:Attach->GetAllSections())S->SetRange(List.SpawnKeys?TRange<FFrameNumber>(Start,EffectEnd):TRange<FFrameNumber>::All());
  M->SetPlaybackRange(Start,End.Value-Start.Value);
  auto* Job=Queue->AllocateNewJob(UMoviePipelineExecutorJob::StaticClass());Job->JobName=Clip.Name;Job->Map=FSoftObjectPath(GEditor->GetEditorWorldContext().World());Job->SetSequence(FSoftObjectPath(Copy));
  auto* Config=Job->GetConfiguration();if(FixedExposure){auto* Exposure=CastChecked<UHandyExposureSetting>(Config->FindOrAddSettingByClass(UHandyExposureSetting::StaticClass()));Exposure->EV100=ExposureEV;}auto* Output=CastChecked<UMoviePipelineOutputSetting>(Config->FindOrAddSettingByClass(UMoviePipelineOutputSetting::StaticClass()));
  Output->OutputDirectory.Path=Clip.Directory;Output->FileNameFormat=TEXT("{frame_number_rel}");Output->OutputResolution=FIntPoint(Options.Width,Options.Height);Output->bUseCustomFrameRate=true;Output->OutputFrameRate=Rate;Output->bUseCustomPlaybackRange=true;Output->CustomStartFrame=Clip.Start;Output->CustomEndFrame=Clip.End;Output->ZeroPadFrameNumbers=8;Output->HandleFrameCount=0;Output->OutputFrameStep=1;Output->bOverrideExistingOutput=false;
  Config->FindOrAddSettingByClass(UMoviePipelineDeferredPassBase::StaticClass());Config->FindOrAddSettingByClass(UMoviePipelineImageSequenceOutput_PNG::StaticClass());
  auto* AA=CastChecked<UMoviePipelineAntiAliasingSetting>(Config->FindOrAddSettingByClass(UMoviePipelineAntiAliasingSetting::StaticClass()));AA->TemporalSampleCount=1;AA->SpatialSampleCount=1<<Options.Quality;AA->bOverrideAntiAliasing=true;AA->AntiAliasingMethod=AAM_None;AA->EngineWarmUpCount=Clip.WarmUp;AA->RenderWarmUpCount=0;AA->bUseCameraCutForWarmUp=false;AA->bRenderWarmUpFrames=Clip.WarmUp>0;
  auto* CameraSettings=CastChecked<UMoviePipelineCameraSetting>(Config->FindOrAddSettingByClass(UMoviePipelineCameraSetting::StaticClass()));CameraSettings->ShutterTiming=EMoviePipelineShutterTiming::FrameOpen;CameraSettings->bRenderAllCameras=false;CameraSettings->bOverrideCameraOverscan=true;CameraSettings->OverscanPercentage=0;
  Clips.Add(Clip);
 }
 if(Clips.IsEmpty()){Error=TEXT("No VFX selected for recording. Check rows or choose All VFX.");return false;}return true;
}
bool UHandyRenderSession::Start(ULevelSequence* Sequence,const UHandyPresentationData& List,const FHandyRenderOptions& Settings,AActor* Camera,bool AllowUnsavedTestMap){
 if(Busy)return false;auto* Sub=GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
 if(Sub->IsRendering()||GEditor->PlayWorld){Status=TEXT("Stop the current render or Play session first.");return false;}
 auto* World=GEditor->GetEditorWorldContext().World();if(!World||(!AllowUnsavedTestMap&&!FPackageName::DoesPackageExist(World->GetOutermost()->GetName()))){Status=TEXT("Save the current level once before recording.");return false;}
 if(!HandyRecording::CheckEncoder(Settings.Encoder,Status)||!Prepare(Sequence,List,Settings,Camera,Status))return false;
 if(!IFileManager::Get().MakeDirectory(*BatchFolder,true)){Status=TEXT("Cannot create the output folder.");return false;}
 FString Probe=BatchFolder/TEXT("write_check.txt");if(!FFileHelper::SaveStringToFile(TEXT("HandyToolbox"),*Probe)){Status=TEXT("Output folder is not writable.");return false;}IFileManager::Get().Delete(*Probe);
 Busy=true;Canceled=false;Encoding=false;Active=Rendered=Encoded=0;Errors.Reset();Executor=NewObject<UHandyRenderExecutor>(this);Executor->SetIsRenderingOffscreen(true);Executor->SetAllowUsingUnsavedLevels(AllowUnsavedTestMap);
 Executor->OnIndividualJobStarted().AddLambda([this](UMoviePipelineExecutorJob* Job){Active=Queue->GetJobs().IndexOfByKey(Job);Status=FString::Printf(TEXT("Rendering %d/%d: %s"),Active+1,Clips.Num(),*Job->JobName);});
 Executor->OnIndividualJobWorkFinished().AddUObject(this,&UHandyRenderSession::JobFinished);
 Executor->OnExecutorErrored().AddLambda([this](UMoviePipelineExecutorBase*,UMoviePipeline*,bool,const FText& Message){Errors+=Message.ToString()+TEXT("\n");});
 Executor->OnExecutorFinished().AddUObject(this,&UHandyRenderSession::RenderFinished);
 Status=TEXT("Starting rendering inside Unreal...");Sub->RenderQueueInstanceWithExecutorInstance(Queue,Executor);return true;
}
void UHandyRenderSession::JobFinished(FMoviePipelineOutputData Data){
 const int32 Index=Queue->GetJobs().IndexOfByKey(Data.Job);if(!Clips.IsValidIndex(Index))return;auto& Clip=Clips[Index];
 if(!Data.bSuccess){Errors+=TEXT("Failed render: ")+Clip.Name+TEXT("\n");return;}
 for(const auto& Shot:Data.ShotData)for(const auto& Pass:Shot.RenderPassData)for(const auto& File:Pass.Value.FilePaths)if(FPaths::GetExtension(File).Equals(TEXT("png"),ESearchCase::IgnoreCase))Clip.Files.Add(File);
 Clip.Files.Sort();if(Clip.Files.Num()!=Clip.End-Clip.Start)Errors+=FString::Printf(TEXT("%s produced %d frames; expected %d. MP4 encoding stopped to avoid a wrong duration.\n"),*Clip.Name,Clip.Files.Num(),Clip.End-Clip.Start);++Rendered;
}
void UHandyRenderSession::RenderFinished(UMoviePipelineExecutorBase*,bool Success){
 if(Canceled){Finish(TEXT("Canceled. Completed files and intermediate frames remain in ")+BatchFolder);return;}
 if(!Success||!Errors.IsEmpty()||Rendered!=Clips.Num()){Finish(TEXT("Render failed. Frames retained for inspection. ")+Errors);return;}
 Encoding=true;Active=0;Status=TEXT("Rendering finished. Encoding MP4 clips...");Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UHandyRenderSession::Tick),.2f);
}
bool UHandyRenderSession::Tick(float){
 if(!Busy||Canceled)return false;
 if(Process.IsValid()){
  FString Log=FPlatformProcess::ReadPipe(ReadPipe);if(!Log.IsEmpty())FFileHelper::SaveStringToFile(Log,*(Clips[Active].Directory/TEXT("encoder.log")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
  FString ProgressText;FFileHelper::LoadFileToString(ProgressText,*(Clips[Active].Directory/TEXT("encode_progress.txt")));TArray<FString> Lines;ProgressText.ParseIntoArrayLines(Lines);for(int32 I=Lines.Num()-1;I>=0;--I)if(Lines[I].StartsWith(TEXT("frame="))){EncodeFraction=FMath::Clamp(FCString::Atof(*Lines[I].Mid(6))/FMath::Max(1,Clips[Active].Files.Num()),0.f,1.f);break;}
  if(FPlatformProcess::IsProcRunning(Process))return true;
  int32 Code=-1;FPlatformProcess::GetProcReturnCode(Process,&Code);FPlatformProcess::CloseProc(Process);Process.Reset();FPlatformProcess::ClosePipe(ReadPipe,WritePipe);ReadPipe=WritePipe=nullptr;
  auto& Clip=Clips[Active];if(Code!=0||IFileManager::Get().FileSize(*(Clip.OutputFile+TEXT(".partial.mp4")))<=0){Finish(FString::Printf(TEXT("MP4 encoding failed for %s (exit %d). Frames and encoder log retained in %s."),*Clip.Name,Code,*Clip.Directory));return false;}
  if(!IFileManager::Get().Move(*Clip.OutputFile,*(Clip.OutputFile+TEXT(".partial.mp4")),false)){Finish(TEXT("Could not finalize the MP4 filename. Encoded partial file retained."));return false;}
  ++Encoded;EncodeFraction=0;if(!Options.KeepFrames)for(const auto& File:Clip.Files)if(FPaths::IsUnderDirectory(File,BatchFolder/TEXT("Frames")))IFileManager::Get().Delete(*File);
  ++Active;
 }
 if(Active>=Clips.Num()){Finish(FString::Printf(TEXT("Finished: %d separate MP4 clips in %s"),Encoded,*BatchFolder));return false;}
 auto& Clip=Clips[Active];
 if(Clip.Files.IsEmpty()){Finish(TEXT("No frames to encode."));return false;}
 // Image-sequence input preserves exact frame pacing, including 24/30/60 FPS.
 const int32 First=FCString::Atoi(*FPaths::GetBaseFilename(Clip.Files[0]));
 for(int32 I=0;I<Clip.Files.Num();++I)if(FPaths::GetCleanFilename(Clip.Files[I])!=FString::Printf(TEXT("%08d.png"),First+I)){Finish(TEXT("Rendered frames are not contiguous. Frames retained; encoding stopped."));return false;}
 const FString Pattern=FPaths::GetPath(Clip.Files[0])/TEXT("%08d.png");
 const int32 CRF[]={28,23,20,16};FString Args=FString::Printf(TEXT("-hide_banner -nostdin -n -loglevel error -framerate %d -start_number %d -i \"%s\" -frames:v %d -c:v libx264 -crf %d -preset medium -pix_fmt yuv420p -movflags +faststart -progress \"%s\" \"%s\""),Options.FPS,First,*Pattern,Clip.Files.Num(),CRF[Options.Quality],*(Clip.Directory/TEXT("encode_progress.txt")),*(Clip.OutputFile+TEXT(".partial.mp4")));
 FPlatformProcess::CreatePipe(ReadPipe,WritePipe);
 Process=FPlatformProcess::CreateProc(*Options.Encoder,*Args,false,true,true,nullptr,0,*Clip.Directory,WritePipe,nullptr,WritePipe);
 if(!Process.IsValid()){FPlatformProcess::ClosePipe(ReadPipe,WritePipe);ReadPipe=WritePipe=nullptr;Finish(TEXT("Could not start FFmpeg. Frames retained.") );return false;}
 Status=FString::Printf(TEXT("Encoding MP4 %d/%d: %s"),Active+1,Clips.Num(),*Clip.Name);return true;
}
float UHandyRenderSession::Progress() const{
 if(!Busy)return Encoded==Clips.Num()&&!Clips.IsEmpty()?1.f:0.f;
 if(Encoding)return .8f+.2f*(Encoded+EncodeFraction)/FMath::Max(1,Clips.Num());
 return .8f*(Rendered+(Executor?FMath::Clamp(Executor->Fraction(),0.f,1.f):0.f))/FMath::Max(1,Clips.Num());
}
void UHandyRenderSession::Cancel(){
 if(!Busy||Canceled)return;Canceled=true;Status=TEXT("Canceling recording...");
 if(Encoding){if(Process.IsValid()){FPlatformProcess::TerminateProc(Process,true);FPlatformProcess::CloseProc(Process);Process.Reset();FPlatformProcess::ClosePipe(ReadPipe,WritePipe);ReadPipe=WritePipe=nullptr;}if(Ticker.IsValid())FTSTicker::GetCoreTicker().RemoveTicker(Ticker);Ticker.Reset();Finish(TEXT("Canceled. Completed clips and frames retained in ")+BatchFolder);}
 else if(Executor&&!Executor->TryCancelSafely()){
  Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this](float){return Busy&&Executor&&!Executor->TryCancelSafely();}));
 }
}
void UHandyRenderSession::Finish(const FString& Message){Busy=false;Encoding=false;Status=Message;Copies.Reset();}
