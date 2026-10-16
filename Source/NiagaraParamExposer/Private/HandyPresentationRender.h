#pragma once
#include "CoreMinimal.h"
#include "Runtime/Launch/Resources/Version.h"
#include "MoviePipelinePIEExecutor.h"
#include "MoviePipelineViewFamilySetting.h"
#include "HAL/PlatformProcess.h"
#include "Containers/Ticker.h"
#include "HandyPresentationRender.generated.h"
class ULevelSequence;
class UHandyPresentationData;
class UMoviePipelineQueue;
class AActor;
class SWidget;
struct FHandyRenderOptions {
 FString Output,Encoder;
 int32 Width=1920,Height=1080,FPS=30,Quality=2,WarmUp=32;
 bool SelectedOnly=false,KeepFrames=false;
 int32 ExposureMode=0; // Viewport, fixed EV100, camera/scene.
 float EV100=1.f,FadeOut=0.f;
};
struct FHandyRenderClip {
 FString Name,Directory,OutputFile;
 int32 Index=0,Start=0,End=0,WarmUp=0;
 TArray<FString> Files;
};
UCLASS()
class UHandyExposureSetting:public UMoviePipelineViewFamilySetting {
 GENERATED_BODY()
public:
 UPROPERTY() float EV100=1.f;
 int32 AppliedViews=0;
 virtual void SetupViewFamily(FSceneViewFamily& ViewFamily) override;
};
UCLASS()
class UHandyRenderExecutor:public UMoviePipelinePIEExecutor {
 GENERATED_BODY()
public: float Fraction(){return GetCompletionPercentageFromActivePipeline();}
 bool TryCancelSafely();
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
protected: virtual bool GetSupportsPostRenderActions_Implementation() override { return false; }
#endif
};
UCLASS()
class UHandyRenderSession:public UObject {
 GENERATED_BODY()
public:
 UPROPERTY(Transient) TObjectPtr<UMoviePipelineQueue> Queue;
 UPROPERTY(Transient) TObjectPtr<UHandyRenderExecutor> Executor;
 UPROPERTY(Transient) TArray<TObjectPtr<ULevelSequence>> Copies;
 FHandyRenderOptions Options;
 TArray<FHandyRenderClip> Clips;
 FString Status=TEXT("Ready. Each VFX is exported to its own MP4; gaps are excluded."),BatchFolder,Errors;
 bool Busy=false,Canceled=false,Encoding=false;
 int32 Active=0,Rendered=0,Encoded=0;
 FProcHandle Process;
 FTSTicker::FDelegateHandle Ticker;
 void* ReadPipe=nullptr;void* WritePipe=nullptr;
 float EncodeFraction=0;
 bool Prepare(ULevelSequence* Sequence,const UHandyPresentationData& List,const FHandyRenderOptions& Settings,AActor* Camera,FString& Error);
 bool Start(ULevelSequence* Sequence,const UHandyPresentationData& List,const FHandyRenderOptions& Settings,AActor* Camera,bool AllowUnsavedTestMap=false);
 void Cancel();
 float Progress() const;
 void JobFinished(FMoviePipelineOutputData Output);
 void RenderFinished(UMoviePipelineExecutorBase*,bool Success);
 bool Tick(float);
 void Finish(const FString& Message);
};
namespace HandyRecording {
UHandyRenderSession* Session();
FString FindEncoder();
bool CheckEncoder(const FString& Path,FString& Message);
bool Validate(const FHandyRenderOptions& Options,FString& Error);
bool ViewportExposure(float& EV100,bool& Fixed);
TSharedRef<SWidget> MakePanel(TFunction<ULevelSequence*()> GetSequence,TFunction<const UHandyPresentationData*()> GetList);
}
