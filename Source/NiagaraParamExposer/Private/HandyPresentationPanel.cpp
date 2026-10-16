#include "HandyPresentation.h"
#include "HandyPresentationRender.h"
#include "HandyAssetSelection.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneSpawnable.h"
#include "MovieSceneObjectBindingID.h"
#include "NiagaraSystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/Factory.h"
#include "PropertyCustomizationHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/MessageDialog.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
namespace HandyPresentation {
class SPresentation:public SCompoundWidget {
public:
 SLATE_BEGIN_ARGS(SPresentation){} SLATE_END_ARGS()
 TStrongObjectPtr<UHandyPresentationData> Data;
 TWeakObjectPtr<ULevelSequence> Sequence;TWeakObjectPtr<AActor> Target;
 TSharedPtr<SVerticalBox> Rows;
 struct FTarget {FGuid ID;FString Label;};TArray<TSharedPtr<FTarget>> Targets;TSharedPtr<SComboBox<TSharedPtr<FTarget>>> TargetBox;
 TArray<TSharedPtr<FString>> Modes,Attachments;
 FString Report=TEXT("Choose an open/existing sequence or create one. Add selected Niagara systems or folders. Set durations, spacing and attachment, then Build / Update.");
 TSharedRef<SWidget> Text(const FString& S){return SNew(STextBlock).Text(FText::FromString(S));}
 TSharedRef<SWidget> Button(const TCHAR* S,TFunction<void()> F){return SNew(SButton).Text(FText::FromString(S)).ButtonColorAndOpacity(FLinearColor(.14f,.23f,.30f)).OnClicked_Lambda([F](){F();return FReply::Handled();});}
 TSharedRef<SWidget> Number(TFunction<double()> Get,TFunction<void(double)> Set){return SNew(SNumericEntryBox<double>).MinValue(0).Value_Lambda([Get](){return TOptional<double>(Get());}).OnValueCommitted_Lambda([Set](double V,ETextCommit::Type){Set(V);});}
 TSharedRef<SWidget> Choice(TArray<TSharedPtr<FString>>& Options,TFunction<int32()> Get,TFunction<void(int32)> Set){auto* List=&Options;return SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(List).OnGenerateWidget_Lambda([this](TSharedPtr<FString> S){return Text(*S);}).OnSelectionChanged_Lambda([List,Set](TSharedPtr<FString> S,ESelectInfo::Type){int32 I=List->Find(S);if(I!=INDEX_NONE)Set(I);})[SNew(STextBlock).Text_Lambda([List,Get](){return FText::FromString(*(*List)[FMath::Clamp(Get(),0,List->Num()-1)]);})];}
 void SetSequence(ULevelSequence* Q){
  Sequence=Q;Target.Reset();Data->Target.Invalidate();if(Q)if(auto* Saved=Q->FindMetaData<UHandyPresentationData>())Data.Reset(DuplicateObject<UHandyPresentationData>(Saved,GetTransientPackage()));
  RefreshTargets();Rebuild();
 }
 void RefreshTargets(){
  Targets.Reset();if(auto* Q=Sequence.Get())for(const auto& B:Q->GetMovieScene()->GetBindings()){
   bool Skeletal=false;if(auto* S=Q->GetMovieScene()->FindSpawnable(B.GetObjectGuid()))if(auto* A=Cast<AActor>(S->GetObjectTemplate()))Skeletal=A->FindComponentByClass<USkeletalMeshComponent>()!=nullptr;
   if(Q==ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence())for(auto* O:ULevelSequenceEditorBlueprintLibrary::GetBoundObjects(FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(B.GetObjectGuid())))){auto* A=Cast<AActor>(O);Skeletal|=A&&A->FindComponentByClass<USkeletalMeshComponent>();}
   if(Skeletal){auto T=MakeShared<FTarget>();T->ID=B.GetObjectGuid();T->Label=B.GetName();Targets.Add(T);}
  }if(TargetBox)TargetBox->RefreshOptions();
 }
 void Add(bool Folders){
  auto Assets=Folders?HandySelection::Gather(HandySelection::SelectedFolders(),{}):HandySelection::SelectedAssets();int32 Count=0;
  for(const auto& A:Assets)if(A.AssetClassPath==UNiagaraSystem::StaticClass()->GetClassPathName()&&!Data->Items.ContainsByPredicate([&](const auto& I){return I.System.ToSoftObjectPath()==A.GetSoftObjectPath();})){FHandyPresentationItem I;I.System=TSoftObjectPtr<UNiagaraSystem>(A.GetSoftObjectPath());Data->Items.Add(I);++Count;}
  Report=FString::Printf(TEXT("Added %d systems. Use Up / Down to set playback order. Duration 0 uses the shared duration."),Count);Rebuild();
 }
 void Rebuild(){if(!Rows)return;Rows->ClearChildren();
  for(int32 I=0;I<Data->Items.Num();++I){auto* System=Data->Items[I].System.LoadSynchronous();const bool Skeletal=HasSkeletalSampling(System);
   Rows->AddSlot().AutoHeight().Padding(3)[SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox).IsEnabled_Lambda([](){return !HandyRecording::Session()->Busy;})
     +SHorizontalBox::Slot().AutoWidth().Padding(3)[SNew(SCheckBox).ToolTipText(FText::FromString(TEXT("Include when recording selected VFX"))).IsChecked_Lambda([this,I](){return Data->Items[I].Render?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([this,I](ECheckBoxState S){Data->Items[I].Render=S==ECheckBoxState::Checked;})[Text(TEXT("Record"))]]
     +SHorizontalBox::Slot().FillWidth(1)[Text(FString::Printf(TEXT("%02d  %s"),I+1,*Data->Items[I].System.GetAssetName()))]
     +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Up"),[this,I](){if(I>0){Data->Items.Swap(I,I-1);Rebuild();}})]
     +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Down"),[this,I](){if(I+1<Data->Items.Num()){Data->Items.Swap(I,I+1);Rebuild();}})]
     +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Remove"),[this,I](){Data->Items.RemoveAt(I);Rebuild();})]]
    +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox).IsEnabled_Lambda([](){return !HandyRecording::Session()->Busy;})
     +SHorizontalBox::Slot().AutoWidth().Padding(2)[Text(TEXT("Seconds (0 = default)"))]
     +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Number([this,I](){return Data->Items[I].Duration;},[this,I](double V){Data->Items[I].Duration=V;})]
     +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Choice(Modes,[this,I](){return Data->Items[I].Mode;},[this,I](int32 V){Data->Items[I].Mode=V;})]
     +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Choice(Attachments,[this,I](){return Data->Items[I].Attach;},[this,I](int32 V){Data->Items[I].Attach=V;})]]
    +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).ColorAndOpacity(Skeletal?FLinearColor(.9f,.72f,.38f):FLinearColor(.6f,.65f,.7f)).Text_Lambda([this,I,Skeletal](){TArray<FSlot> Slots;FString Error;auto* Q=Sequence.Get();if(!Plan(*Data,Q?Q->GetMovieScene()->GetTickResolution():FFrameRate(24000,1),Slots,Error))return FText::FromString(Error);const double Rate=Q?Q->GetMovieScene()->GetTickResolution().AsDecimal():24000;return FText::FromString(FString::Printf(TEXT("%.2f - %.2f s | %s"),Slots[I].Start.Value/Rate,Slots[I].End.Value/Rate,Skeletal?TEXT("Skeletal sampling detected: review attachment/source requirements"):TEXT("No skeletal sampling detected; attachment remains your choice")));})]];
  }
 }
 void Apply(){auto* Q=Sequence.Get();if(Q&&Q->FindMetaData<UHandyPresentationData>()&&FMessageDialog::Open(EAppMsgType::YesNo,FText::FromString(TEXT("Rebuild the generated VFX tracks from this list? Manual edits to those generated tracks will be replaced. Cameras, mesh and animation tracks are preserved. Undo is available.")))!=EAppReturnType::Yes)return;
  if(Build(Q,*Data,Target.Get(),Report)){Data->Target=Q->FindMetaData<UHandyPresentationData>()->Target;ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Q);ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();RefreshTargets();}
 }
 void Construct(const FArguments&){
  Data.Reset(NewObject<UHandyPresentationData>());
  for(const TCHAR* V:{TEXT("Default mode"),TEXT("Loop"),TEXT("Once / Trigger")})Modes.Add(MakeShared<FString>(V));
  for(const TCHAR* V:{TEXT("Default attachment"),TEXT("Attach to SKM"),TEXT("World / no attachment")})Attachments.Add(MakeShared<FString>(V));
  ChildSlot[SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(TEXT("Presentation"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",16)).ColorAndOpacity(FLinearColor(.68f,.79f,.88f))]
   +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SHorizontalBox).IsEnabled_Lambda([](){return !HandyRecording::Session()->Busy;})
    +SHorizontalBox::Slot().FillWidth(1)[SNew(SObjectPropertyEntryBox).AllowedClass(ULevelSequence::StaticClass()).ObjectPath_Lambda([this](){return Sequence.IsValid()?Sequence->GetPathName():FString();}).OnObjectChanged_Lambda([this](const FAssetData& A){SetSequence(Cast<ULevelSequence>(A.GetAsset()));})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Use Open Sequencer"),[this](){auto* Q=ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();if(Q)SetSequence(Q);else Report=TEXT("Open a Level Sequence first, or choose Create Sequence.");})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Create Sequence"),[this](){UClass* C=LoadClass<UFactory>(nullptr,TEXT("/Script/LevelSequenceEditor.LevelSequenceFactoryNew"));if(!C){Report=TEXT("Level Sequence factory is unavailable.");return;}auto* Q=Cast<ULevelSequence>(FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateAssetWithDialog(TEXT("LS_Presentation"),TEXT("/Game"),ULevelSequence::StaticClass(),NewObject<UFactory>(GetTransientPackage(),C)));if(Q){SetSequence(Q);ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Q);}})]]
   +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SHorizontalBox).IsEnabled_Lambda([](){return !HandyRecording::Session()->Busy;})
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Add Selected Niagara Systems"),[this](){Add(false);})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Add Selected Folders"),[this](){Add(true);})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Clear List"),[this](){Data->Items.Reset();Rebuild();})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Undo"),[this](){GEditor->UndoTransaction();ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Redo"),[this](){GEditor->RedoTransaction();ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();})]]
   +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SHorizontalBox).IsEnabled_Lambda([](){return !HandyRecording::Session()->Busy;})
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Text(TEXT("Default seconds"))]
    +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Number([this](){return Data->Duration;},[this](double V){Data->Duration=V;})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Text(TEXT("Gap seconds"))]
    +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Number([this](){return Data->Gap;},[this](double V){Data->Gap=V;})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Text(TEXT("Start seconds"))]
    +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Number([this](){return Data->Start;},[this](double V){Data->Start=V;})]
    +SHorizontalBox::Slot().AutoWidth().Padding(4)[SNew(SCheckBox).IsChecked_Lambda([this](){return Data->Loop?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([this](ECheckBoxState S){Data->Loop=S==ECheckBoxState::Checked;})[Text(TEXT("Default: Loop (off = Once)"))]]]
   +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SHorizontalBox).IsEnabled_Lambda([](){return !HandyRecording::Session()->Busy;})
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SCheckBox).IsChecked_Lambda([this](){return Data->Attach?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([this](ECheckBoxState S){Data->Attach=S==ECheckBoxState::Checked;})[Text(TEXT("Default: attach to SKM"))]]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Use Selected SKM Actor"),[this](){for(FSelectionIterator I(*GEditor->GetSelectedActors());I;++I)if(auto* A=Cast<AActor>(*I))if(A->FindComponentByClass<USkeletalMeshComponent>()){Target=A;Data->Target.Invalidate();Report=TEXT("Attachment target: ")+A->GetActorLabel();return;}Report=TEXT("Select a skeletal-mesh actor in the level.");})]
    +SHorizontalBox::Slot().FillWidth(1).Padding(2)[SAssignNew(TargetBox,SComboBox<TSharedPtr<FTarget>>).OptionsSource(&Targets).OnComboBoxOpening_Lambda([this](){RefreshTargets();}).OnGenerateWidget_Lambda([this](TSharedPtr<FTarget> T){return Text(T->Label);}).OnSelectionChanged_Lambda([this](TSharedPtr<FTarget> T,ESelectInfo::Type){if(T){Data->Target=T->ID;Target.Reset();}})[SNew(STextBlock).Text_Lambda([this](){if(Target.IsValid())return FText::FromString(Target->GetActorLabel());for(auto& T:Targets)if(T->ID==Data->Target)return FText::FromString(T->Label);return FText::FromString(TEXT("Skeletal mesh in sequence"));})]]
    +SHorizontalBox::Slot().FillWidth(1).Padding(2)[SNew(SEditableTextBox).HintText(FText::FromString(TEXT("Bone/socket (optional)"))).Text_Lambda([this](){return FText::FromName(Data->Socket);}).OnTextCommitted_Lambda([this](const FText& T,ETextCommit::Type){Data->Socket=FName(*T.ToString());})]]
   +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SCheckBox).IsEnabled_Lambda([](){return !HandyRecording::Session()->Busy;}).IsChecked_Lambda([this](){return Data->SpawnKeys?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([this](ECheckBoxState S){Data->SpawnKeys=S==ECheckBoxState::Checked;}).ToolTipText(FText::FromString(TEXT("On: spawn/despawn at each VFX boundary. Off: keep the Spawned track checked with no keys so existing particles can fade naturally. Build / Update to apply. Actors still end when the sequence stops.")))[Text(TEXT("Add Spawned on/off keys (off = allow natural fade)"))]]
   +SVerticalBox::Slot().FillHeight(1).Padding(4)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(Rows,SVerticalBox)]]
   +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SHorizontalBox).IsEnabled_Lambda([](){return !HandyRecording::Session()->Busy;})
    +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(TEXT("Build / Update Presentation"),[this](){Apply();})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Clear Generated Tracks"),[this](){if(FMessageDialog::Open(EAppMsgType::YesNo,FText::FromString(TEXT("Remove only the VFX tracks generated by Presentation? The list and other sequence tracks are retained. Undo is available.")))==EAppReturnType::Yes){Clear(Sequence.Get(),Report);ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();}})]]
   +SVerticalBox::Slot().AutoHeight().Padding(4)[HandyRecording::MakePanel([this](){return Sequence.Get();},[this](){return Data.Get();})]
   +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this](){return FText::FromString(Report);})]
  ];SetSequence(ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence());
 }
};
TSharedRef<SWidget> MakePanel(){return SNew(SPresentation);}
}
