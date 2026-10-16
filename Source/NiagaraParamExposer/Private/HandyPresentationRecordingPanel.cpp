#include "HandyPresentationRender.h"
#include "HandyPresentation.h"
#include "LevelSequence.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Actor.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
namespace HandyRecording {
class SRecording:public SCompoundWidget {
public:
 SLATE_BEGIN_ARGS(SRecording){} SLATE_END_ARGS()
 FHandyRenderOptions Options;TFunction<ULevelSequence*()> GetSequence;TFunction<const UHandyPresentationData*()> GetList;
 TWeakObjectPtr<AActor> Camera;bool OverrideCamera=false;TArray<TSharedPtr<FString>> Qualities,Exposures;
 TSharedRef<SWidget> Label(const TCHAR* S){return SNew(STextBlock).Text(FText::FromString(S));}
 TSharedRef<SWidget> Button(const TCHAR* S,TFunction<void()> F){return SNew(SButton).Text(FText::FromString(S)).ButtonColorAndOpacity(FLinearColor(.14f,.23f,.30f)).OnClicked_Lambda([F](){F();return FReply::Handled();});}
 TSharedRef<SWidget> Number(int32* N){return SNew(SNumericEntryBox<int32>).Value_Lambda([N](){return TOptional<int32>(*N);}).OnValueCommitted_Lambda([this,N](int32 V,ETextCommit::Type){*N=V;Save();});}
 void Save(){const TCHAR* Section=TEXT("HandyToolbox.Recording");GConfig->SetString(Section,TEXT("Output"),*Options.Output,GEditorPerProjectIni);GConfig->SetString(Section,TEXT("FFmpeg"),*Options.Encoder,GEditorPerProjectIni);GConfig->SetInt(Section,TEXT("Width"),Options.Width,GEditorPerProjectIni);GConfig->SetInt(Section,TEXT("Height"),Options.Height,GEditorPerProjectIni);GConfig->SetInt(Section,TEXT("FPS"),Options.FPS,GEditorPerProjectIni);GConfig->SetInt(Section,TEXT("Quality"),Options.Quality,GEditorPerProjectIni);GConfig->SetInt(Section,TEXT("WarmUp"),Options.WarmUp,GEditorPerProjectIni);GConfig->SetBool(Section,TEXT("KeepFrames"),Options.KeepFrames,GEditorPerProjectIni);GConfig->SetInt(Section,TEXT("ExposureMode"),Options.ExposureMode,GEditorPerProjectIni);GConfig->SetFloat(Section,TEXT("EV100"),Options.EV100,GEditorPerProjectIni);GConfig->SetFloat(Section,TEXT("FadeOut"),Options.FadeOut,GEditorPerProjectIni);GConfig->Flush(false,GEditorPerProjectIni);}
 void Construct(const FArguments&,TFunction<ULevelSequence*()> SequenceGetter,TFunction<const UHandyPresentationData*()> ListGetter){
  GetSequence=MoveTemp(SequenceGetter);GetList=MoveTemp(ListGetter);Options.Output=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("HandyPresentation"));const TCHAR* Section=TEXT("HandyToolbox.Recording");
  GConfig->GetString(Section,TEXT("Output"),Options.Output,GEditorPerProjectIni);GConfig->GetString(Section,TEXT("FFmpeg"),Options.Encoder,GEditorPerProjectIni);if(Options.Encoder.IsEmpty())Options.Encoder=FindEncoder();
  GConfig->GetInt(Section,TEXT("Width"),Options.Width,GEditorPerProjectIni);GConfig->GetInt(Section,TEXT("Height"),Options.Height,GEditorPerProjectIni);GConfig->GetInt(Section,TEXT("FPS"),Options.FPS,GEditorPerProjectIni);GConfig->GetInt(Section,TEXT("Quality"),Options.Quality,GEditorPerProjectIni);Options.Quality=FMath::Clamp(Options.Quality,0,3);GConfig->GetInt(Section,TEXT("WarmUp"),Options.WarmUp,GEditorPerProjectIni);GConfig->GetBool(Section,TEXT("KeepFrames"),Options.KeepFrames,GEditorPerProjectIni);
  GConfig->GetInt(Section,TEXT("ExposureMode"),Options.ExposureMode,GEditorPerProjectIni);Options.ExposureMode=FMath::Clamp(Options.ExposureMode,0,2);GConfig->GetFloat(Section,TEXT("EV100"),Options.EV100,GEditorPerProjectIni);GConfig->GetFloat(Section,TEXT("FadeOut"),Options.FadeOut,GEditorPerProjectIni);
  for(const TCHAR* E:{TEXT("Match viewport exposure"),TEXT("Fixed EV100"),TEXT("Camera / scene exposure")})Exposures.Add(MakeShared<FString>(E));
  for(const TCHAR* Q:{TEXT("Draft"),TEXT("Standard"),TEXT("High"),TEXT("Epic")})Qualities.Add(MakeShared<FString>(Q));
  ChildSlot[SNew(SExpandableArea).InitiallyCollapsed(false).HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Record MP4 Clips"))).ColorAndOpacity(FLinearColor(.68f,.79f,.88f))]
   .BodyContent()[SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[SNew(SVerticalBox).IsEnabled_Lambda([](){return !Session()->Busy;})
     +SVerticalBox::Slot().AutoHeight().Padding(3)[SNew(SHorizontalBox)
      +SHorizontalBox::Slot().AutoWidth().Padding(3)[Label(TEXT("Resolution"))]
      +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Number(&Options.Width)]
      +SHorizontalBox::Slot().AutoWidth().Padding(2)[Label(TEXT("x"))]
      +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Number(&Options.Height)]
      +SHorizontalBox::Slot().AutoWidth().Padding(3)[Label(TEXT("FPS"))]
      +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Number(&Options.FPS)]
      +SHorizontalBox::Slot().FillWidth(1).Padding(2)[SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&Qualities).OnGenerateWidget_Lambda([this](TSharedPtr<FString> Q){return Label(**Q);}).OnSelectionChanged_Lambda([this](TSharedPtr<FString> Q,ESelectInfo::Type){int32 I=Qualities.Find(Q);if(I!=INDEX_NONE){Options.Quality=I;Save();}})[SNew(STextBlock).Text_Lambda([this](){return FText::FromString(*Qualities[Options.Quality]);})]]]
     +SVerticalBox::Slot().AutoHeight().Padding(3)[SNew(SHorizontalBox)
      +SHorizontalBox::Slot().AutoWidth().Padding(2)[Label(TEXT("Loop warm-up frames"))]
      +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Number(&Options.WarmUp)]
      +SHorizontalBox::Slot().AutoWidth().Padding(3)[SNew(SCheckBox).IsChecked_Lambda([this](){return Options.SelectedOnly?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([this](ECheckBoxState S){Options.SelectedOnly=S==ECheckBoxState::Checked;})[Label(TEXT("Selected VFX only (off = all)"))]]
      +SHorizontalBox::Slot().AutoWidth().Padding(3)[SNew(SCheckBox).IsChecked_Lambda([this](){return Options.KeepFrames?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([this](ECheckBoxState S){Options.KeepFrames=S==ECheckBoxState::Checked;Save();})[Label(TEXT("Keep PNG frames"))]]]
     +SVerticalBox::Slot().AutoHeight().Padding(3)[SNew(SHorizontalBox)
      +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Use Selected Camera"),[this](){for(FSelectionIterator I(*GEditor->GetSelectedActors());I;++I)if(auto* A=Cast<AActor>(*I))if(A->FindComponentByClass<UCameraComponent>()){Camera=A;OverrideCamera=true;return;}Session()->Status=TEXT("Select a camera actor in the level first.");})]
      +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Use Sequence Camera Cuts"),[this](){OverrideCamera=false;Camera.Reset();})]
      +SHorizontalBox::Slot().FillWidth(1).Padding(3)[SNew(STextBlock).Text_Lambda([this](){return FText::FromString(OverrideCamera?(Camera.IsValid()?Camera->GetActorLabel():TEXT("Camera missing")):TEXT("Sequence Camera Cuts"));})]]
     +SVerticalBox::Slot().AutoHeight().Padding(3)[SNew(SHorizontalBox)
      +SHorizontalBox::Slot().AutoWidth().Padding(2)[Label(TEXT("Output"))]
      +SHorizontalBox::Slot().FillWidth(1).Padding(2)[SNew(SEditableTextBox).Text_Lambda([this](){return FText::FromString(Options.Output);}).OnTextCommitted_Lambda([this](const FText& T,ETextCommit::Type){Options.Output=T.ToString();Save();})]
      +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Browse Folder"),[this](){FString Folder;if(auto* Desktop=FDesktopPlatformModule::Get())if(Desktop->OpenDirectoryDialog(nullptr,TEXT("MP4 output folder"),Options.Output,Folder)){Options.Output=FPaths::ConvertRelativePathToFull(Folder);Save();}})]]
     +SVerticalBox::Slot().AutoHeight().Padding(3)[SNew(SHorizontalBox)
      +SHorizontalBox::Slot().AutoWidth().Padding(2)[Label(TEXT("FFmpeg"))]
      +SHorizontalBox::Slot().FillWidth(1).Padding(2)[SNew(SEditableTextBox).Text_Lambda([this](){return FText::FromString(Options.Encoder);}).OnTextCommitted_Lambda([this](const FText& T,ETextCommit::Type){Options.Encoder=T.ToString().TrimQuotes();Save();})]
      +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Browse"),[this](){TArray<FString> Files;if(auto* Desktop=FDesktopPlatformModule::Get())if(Desktop->OpenFileDialog(nullptr,TEXT("Choose ffmpeg.exe"),FPaths::GetPath(Options.Encoder),TEXT("ffmpeg.exe"),TEXT("FFmpeg executable|*.exe"),0,Files)&&!Files.IsEmpty()){Options.Encoder=FPaths::ConvertRelativePathToFull(Files[0]);Save();}})]
      +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Detect / Check"),[this](){if(!FPaths::FileExists(Options.Encoder))Options.Encoder=FindEncoder();CheckEncoder(Options.Encoder,Session()->Status);Save();})]]
     +SVerticalBox::Slot().AutoHeight().Padding(3)[SNew(SHorizontalBox)
      +SHorizontalBox::Slot().AutoWidth().Padding(2)[Label(TEXT("Exposure"))]
      +SHorizontalBox::Slot().FillWidth(1).Padding(2)[SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&Exposures).OnGenerateWidget_Lambda([this](TSharedPtr<FString> E){return Label(**E);}).OnSelectionChanged_Lambda([this](TSharedPtr<FString> E,ESelectInfo::Type){int32 I=Exposures.Find(E);if(I!=INDEX_NONE){Options.ExposureMode=I;Save();}})[SNew(STextBlock).Text_Lambda([this](){return FText::FromString(*Exposures[Options.ExposureMode]);})]]
      +SHorizontalBox::Slot().AutoWidth().Padding(2)[Label(TEXT("EV100"))]
      +SHorizontalBox::Slot().FillWidth(.5f).Padding(2)[SNew(SNumericEntryBox<float>).IsEnabled_Lambda([this](){return Options.ExposureMode==1;}).MinValue(-20).MaxValue(30).Value_Lambda([this](){return TOptional<float>(Options.EV100);}).OnValueCommitted_Lambda([this](float V,ETextCommit::Type){Options.EV100=V;Save();})]
      +SHorizontalBox::Slot().AutoWidth().Padding(2)[Label(TEXT("Fade-out seconds"))]
      +SHorizontalBox::Slot().FillWidth(.5f).Padding(2)[SNew(SNumericEntryBox<float>).MinValue(0).MaxValue(600).Value_Lambda([this](){return TOptional<float>(Options.FadeOut);}).OnValueCommitted_Lambda([this](float V,ETextCommit::Type){Options.FadeOut=V;Save();})]]
     +SVerticalBox::Slot().AutoHeight().Padding(3)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this](){
      if(Options.ExposureMode==1)return FText::FromString(FString::Printf(TEXT("Recording at fixed EV100 %.2f. Camera and scene exposure assets remain unchanged."),Options.EV100));
      if(Options.ExposureMode==2)return FText::FromString(TEXT("Uses the camera and scene exposure, including Game Settings auto exposure."));
      float EV=0;bool Fixed=false;if(!ViewportExposure(EV,Fixed))return FText::FromString(TEXT("Focus your perspective level viewport to read its exposure, or choose Fixed EV100."));
      return FText::FromString(Fixed?FString::Printf(TEXT("Viewport EV100 %.2f will be used when recording starts."),EV):TEXT("Viewport uses Game Settings: recording will use camera / scene exposure."));
     })]
     +SVerticalBox::Slot().AutoHeight().Padding(3)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("Loop warm-up is discarded; Once has no warm-up to preserve its first burst. Clips use each row's duration, rounded to output frames, without the gaps. Fade-out seconds adds recording time after the VFX duration; disable Spawned keys to keep fading particles alive. A new output batch folder prevents overwriting older videos.")))]]
    +SVerticalBox::Slot().AutoHeight().Padding(3)[SNew(SHorizontalBox)
     +SHorizontalBox::Slot().FillWidth(1).Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("Record Individual MP4 Clips"))).ButtonColorAndOpacity(FLinearColor(.12f,.30f,.25f)).IsEnabled_Lambda([](){return !Session()->Busy;}).OnClicked_Lambda([this](){if(OverrideCamera&&!Camera.IsValid()){Session()->Status=TEXT("The selected camera is missing. Select it again.");return FReply::Handled();}if(auto* List=GetList()){Save();Session()->Start(GetSequence(),*List,Options,OverrideCamera?Camera.Get():nullptr);}return FReply::Handled();})]
     +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("Cancel Recording"))).IsEnabled_Lambda([](){return Session()->Busy;}).OnClicked_Lambda([](){Session()->Cancel();return FReply::Handled();})]
     +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Open Output"),[this](){FPlatformProcess::ExploreFolder(*(Session()->BatchFolder.IsEmpty()?Options.Output:Session()->BatchFolder));})]]
    +SVerticalBox::Slot().AutoHeight().Padding(3)[SNew(SProgressBar).Percent_Lambda([](){return TOptional<float>(Session()->Progress());})]
    +SVerticalBox::Slot().AutoHeight().Padding(3)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([](){return FText::FromString(Session()->Status);})]
   ]];
 }
};
TSharedRef<SWidget> MakePanel(TFunction<ULevelSequence*()> GetSequence,TFunction<const UHandyPresentationData*()> GetList){return SNew(SRecording,MoveTemp(GetSequence),MoveTemp(GetList));}
}
