#include "HandyPresentation.h"
#include "LevelSequence.h"
#include "UniversalObjectLocatorResolveParams.h"
#include "MovieScene.h"
#include "MovieSceneSpawnable.h"
#include "MovieScenePossessable.h"
#include "MovieSceneObjectBindingID.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "Tracks/MovieSceneParticleTrack.h"
#include "Sections/MovieSceneParticleSection.h"
#include "Tracks/MovieScene3DAttachTrack.h"
#include "Sections/MovieScene3DAttachSection.h"
#include "MovieScene/MovieSceneNiagaraSystemTrack.h"
#include "MovieScene/MovieSceneNiagaraSystemSpawnSection.h"
#include "NiagaraActor.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraDataInterfaceSkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "UObject/UObjectHash.h"
namespace HandyPresentation {
bool HasSkeletalSampling(UNiagaraSystem* System){
 bool Found=false;if(System)ForEachObjectWithOuter(System,[&](UObject* O){Found|=O->IsA<UNiagaraDataInterfaceSkeletalMesh>();},true);return Found;
}
bool Plan(const UHandyPresentationData& D,FFrameRate Rate,TArray<FSlot>& Slots,FString& Error){
 Slots.Reset();if(D.Items.IsEmpty()){Error=TEXT("Add Niagara systems first.");return false;}
 if(!FMath::IsFinite(D.Duration)||D.Duration<=0||!FMath::IsFinite(D.Gap)||D.Gap<0||!FMath::IsFinite(D.Start)||D.Start<0){Error=TEXT("Use a positive duration and non-negative start and spacing.");return false;}
 double Cursor=D.Start;
 for(const auto& I:D.Items){double Length=I.Duration==0?D.Duration:I.Duration;
  if(!FMath::IsFinite(Length)||Length<=0||I.Mode<0||I.Mode>2||I.Attach<0||I.Attach>2){Error=TEXT("Invalid duration or row option.");return false;}
  double A=Cursor*Rate.AsDecimal(),B=(Cursor+Length)*Rate.AsDecimal();
  if(B>MAX_int32-1||A<0){Error=TEXT("Presentation exceeds Sequencer's frame range.");return false;}
  FSlot S{FFrameNumber(static_cast<int32>(FMath::RoundToInt(A))),FFrameNumber(static_cast<int32>(FMath::RoundToInt(B))),I.Mode==0?D.Loop:I.Mode==1,I.Attach==0?D.Attach:I.Attach==1};
  if(S.End<=S.Start){Error=TEXT("Duration must cover at least one sequence tick.");return false;}
  Slots.Add(S);Cursor+=Length+D.Gap;
 }return true;
}
static void RemoveGenerated(ULevelSequence* Q,UHandyPresentationData* D){
 auto* M=Q->GetMovieScene();if(!D)return;
 for(const FGuid& G:D->Components){Q->UnbindPossessableObjects(G);M->RemovePossessable(G);}
 for(const FGuid& G:D->Actors)M->RemoveSpawnable(G);
 D->Components.Reset();D->Actors.Reset();
}
bool Clear(ULevelSequence* Q,FString& Report){
 if(!Q||!Q->GetMovieScene()){Report=TEXT("Choose a Level Sequence first.");return false;}
 if(Q->GetMovieScene()->IsReadOnly()){Report=TEXT("The sequence is read-only.");return false;}
 auto* D=Q->FindMetaData<UHandyPresentationData>();if(!D){Report=TEXT("No generated presentation tracks in this sequence.");return false;}
 FScopedTransaction Tx(NSLOCTEXT("HandyToolbox","ClearPresentation","Clear Presentation Tracks"));Q->Modify();Q->GetMovieScene()->Modify();D->Modify();RemoveGenerated(Q,D);Q->MarkPackageDirty();Report=TEXT("Generated VFX tracks cleared. List, cameras, mesh and animation tracks retained. Undo restores the tracks.");return true;
}
bool Build(ULevelSequence* Q,const UHandyPresentationData& Input,AActor* Target,FString& Report){
 if(!Q||!Q->GetMovieScene()){Report=TEXT("Choose or create a Level Sequence first.");return false;}
 auto* M=Q->GetMovieScene();if(M->IsReadOnly()){Report=TEXT("The sequence is read-only.");return false;}
 TArray<FSlot> Slots;if(!Plan(Input,M->GetTickResolution(),Slots,Report))return false;
 TArray<UNiagaraSystem*> Systems;for(const auto& I:Input.Items){auto* S=I.System.LoadSynchronous();if(!S){Report=TEXT("A Niagara system is missing. No tracks changed.");return false;}Systems.Add(S);}
 bool NeedsTarget=Slots.ContainsByPredicate([](const FSlot& S){return S.Attach;});
 FGuid TargetID=Input.Target;USkeletalMeshComponent* Mesh=Target?Target->FindComponentByClass<USkeletalMeshComponent>():nullptr;
 if(NeedsTarget&&!Mesh&&TargetID.IsValid()){
  if(auto* Spawnable=M->FindSpawnable(TargetID))if(auto* Actor=Cast<AActor>(Spawnable->GetObjectTemplate()))Mesh=Actor->FindComponentByClass<USkeletalMeshComponent>();
  if(!Mesh){TArray<UObject*,TInlineAllocator<1>> Bound;Q->LocateBoundObjects(TargetID,UE::UniversalObjectLocator::FResolveParams(GEditor->GetEditorWorldContext().World()),Bound);for(auto* O:Bound)if(auto* Actor=Cast<AActor>(O))Mesh=Actor->FindComponentByClass<USkeletalMeshComponent>();}
 }
 if(NeedsTarget&&!Mesh){Report=TEXT("Choose a skeletal-mesh actor or sequence binding, or switch attachment to World.");return false;}
 if(NeedsTarget&&Mesh&&!Input.Socket.IsNone()&&!Mesh->DoesSocketExist(Input.Socket)){Report=TEXT("The selected skeletal mesh has no such bone/socket.");return false;}
 FScopedTransaction Tx(NSLOCTEXT("HandyToolbox","BuildPresentation","Build Niagara Presentation"));Q->Modify();M->Modify();
 auto* D=Q->FindOrAddMetaData<UHandyPresentationData>();D->SetFlags(RF_Transactional);D->Modify();
 RemoveGenerated(Q,D);
 D->Items=Input.Items;D->Duration=Input.Duration;D->Gap=Input.Gap;D->Start=Input.Start;D->Loop=Input.Loop;D->SpawnKeys=Input.SpawnKeys;D->Attach=Input.Attach;D->Socket=Input.Socket;
 if(NeedsTarget&&Target){TargetID=Q->FindBindingFromObject(Target,Target->GetWorld());if(!TargetID.IsValid()){TargetID=M->AddPossessable(Target->GetActorLabel(),Target->GetClass());Q->BindPossessableObject(TargetID,*Target,Target->GetWorld());}}
 D->Target=TargetID;
 for(int32 Index=0;Index<Systems.Num();++Index){
  const auto& Slot=Slots[Index];
  auto* Template=NewObject<ANiagaraActor>(M,NAME_None,RF_Transactional);Template->SetActorLabel(Systems[Index]->GetName());
  auto* Component=Template->GetNiagaraComponent();Component->SetAsset(Systems[Index]);Component->SetAutoActivate(false);
  const FGuid ActorID=M->AddSpawnable(FString::Printf(TEXT("%02d %s"),Index+1,*Systems[Index]->GetName()),*Template);D->Actors.Add(ActorID);
  auto* SpawnTrack=M->AddTrack<UMovieSceneSpawnTrack>(ActorID);auto* Spawn=CastChecked<UMovieSceneSpawnSection>(SpawnTrack->CreateNewSection());Spawn->SetRange(TRange<FFrameNumber>::All());Spawn->GetChannel().SetDefault(!Input.SpawnKeys);if(Input.SpawnKeys){Spawn->GetChannel().GetData().AddKey(Slot.Start,true);Spawn->GetChannel().GetData().AddKey(Slot.End,false);}SpawnTrack->AddSection(*Spawn);
  const FGuid ComponentID=M->AddPossessable(TEXT("NiagaraComponent"),UNiagaraComponent::StaticClass());D->Components.Add(ComponentID);M->FindPossessable(ComponentID)->SetParent(ActorID,M);M->FindSpawnable(ActorID)->AddChildPossessable(ComponentID);Q->BindPossessableObject(ComponentID,*Component,Template);
  if(Slot.Loop){auto* Track=M->AddTrack<UMovieSceneNiagaraSystemTrack>(ComponentID);auto* Section=CastChecked<UMovieSceneNiagaraSystemSpawnSection>(Track->CreateNewSection());Section->SetRange(TRange<FFrameNumber>(Slot.Start,Slot.End));Section->SetAgeUpdateMode(ENiagaraAgeUpdateMode::TickDeltaTime);Track->AddSection(*Section);}
  else{auto* Track=M->AddTrack<UMovieSceneParticleTrack>(ComponentID);auto* Section=CastChecked<UMovieSceneParticleSection>(Track->CreateNewSection());Section->SetRange(TRange<FFrameNumber>(Slot.Start,Slot.End));Section->ParticleKeys.GetData().AddKey(Slot.Start,static_cast<uint8>(EParticleKey::Trigger));Track->AddSection(*Section);}
  if(Slot.Attach){auto* Track=M->AddTrack<UMovieScene3DAttachTrack>(ActorID);auto* Section=CastChecked<UMovieScene3DAttachSection>(Track->CreateNewSection());Section->SetRange(Input.SpawnKeys?TRange<FFrameNumber>(Slot.Start,Slot.End):TRange<FFrameNumber>::All());Section->SetConstraintBindingID(FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(TargetID)));Section->AttachSocketName=Input.Socket;Section->AttachComponentName=Mesh?Mesh->GetFName():NAME_None;Section->AttachmentLocationRule=EAttachmentRule::SnapToTarget;Section->AttachmentRotationRule=EAttachmentRule::SnapToTarget;Section->AttachmentScaleRule=EAttachmentRule::KeepRelative;Track->AddSection(*Section);}
 }
 M->SetPlaybackRange(FFrameNumber(0),Slots.Last().End.Value);Q->MarkPackageDirty();Report=FString::Printf(TEXT("Built %d VFX. Later effects follow duration changes and spacing. Save the sequence to keep the list. Undo restores the previous build."),Systems.Num());return true;
}
}
