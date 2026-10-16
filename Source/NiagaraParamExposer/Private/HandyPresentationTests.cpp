#include "HandyPresentation.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "LevelSequence.h"
#include "UniversalObjectLocatorResolveParams.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceActor.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneSpawnable.h"
#include "Tracks/MovieSceneParticleTrack.h"
#include "Sections/MovieSceneParticleSection.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "Tracks/MovieScene3DAttachTrack.h"
#include "MovieScene/MovieSceneNiagaraSystemTrack.h"
#include "NiagaraSystem.h"
#include "NiagaraActor.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemFactoryNew.h"
#include "Animation/SkeletalMeshActor.h"
#include "Engine/World.h"
#include "Editor.h"
#include "UObject/StrongObjectPtr.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandyPresentationPlanTest,"NiagaraParamExposer.Presentation.Schedule",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHandyPresentationPlanTest::RunTest(const FString&){
 TStrongObjectPtr<UHandyPresentationData> D(NewObject<UHandyPresentationData>());D->Items.SetNum(3);D->Duration=5;D->Gap=1;D->Items[1].Duration=2;D->Items[1].Mode=2;D->Items[1].Attach=2;
 TArray<HandyPresentation::FSlot> Slots;FString Error;TestTrue(TEXT("Schedule succeeds"),HandyPresentation::Plan(*D,FFrameRate(30,1),Slots,Error));
 TestEqual(TEXT("First start"),Slots[0].Start.Value,0);TestEqual(TEXT("Second start"),Slots[1].Start.Value,180);TestEqual(TEXT("Third follows override"),Slots[2].Start.Value,270);TestFalse(TEXT("Individual Once override"),Slots[1].Loop);TestFalse(TEXT("World override"),Slots[1].Attach);
 D->Items[0].Duration=7;HandyPresentation::Plan(*D,FFrameRate(30,1),Slots,Error);TestEqual(TEXT("Later effects shift after duration edit"),Slots[2].Start.Value,330);
 D->Items.Swap(0,1);HandyPresentation::Plan(*D,FFrameRate(30,1),Slots,Error);TestFalse(TEXT("Mode follows reordered item"),Slots[0].Loop);TestEqual(TEXT("Order changes second start"),Slots[1].Start.Value,90);
 D->Gap=-1;TestFalse(TEXT("Reject negative gap"),HandyPresentation::Plan(*D,FFrameRate(30,1),Slots,Error));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandyPresentationBuildTest,"NiagaraParamExposer.Presentation.TracksAndUndo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHandyPresentationBuildTest::RunTest(const FString&){
 TStrongObjectPtr<ULevelSequence> Q(NewObject<ULevelSequence>(GetTransientPackage(),NAME_None,RF_Transactional));Q->Initialize();auto* M=Q->GetMovieScene();M->SetFlags(RF_Transactional);
 TStrongObjectPtr<UNiagaraSystem> System(NewObject<UNiagaraSystem>());UNiagaraSystemFactoryNew::InitializeSystem(System.Get(),true);
 TStrongObjectPtr<UHandyPresentationData> D(NewObject<UHandyPresentationData>());D->Items.SetNum(2);for(auto& I:D->Items)I.System=System.Get();D->Items[1].Mode=2;D->Attach=false;
 const FGuid Existing=M->AddPossessable(TEXT("Existing camera"),AActor::StaticClass());FString Report;
 if(!TestTrue(TEXT("Build succeeds"),HandyPresentation::Build(Q.Get(),*D,nullptr,Report))){AddError(Report);return false;}
 auto* Saved=Q->FindMetaData<UHandyPresentationData>();TestEqual(TEXT("Two generated spawnables"),Saved->Actors.Num(),2);TestEqual(TEXT("Component bindings"),Saved->Components.Num(),2);
 TestNotNull(TEXT("Loop track"),M->FindTrack<UMovieSceneNiagaraSystemTrack>(Saved->Components[0]));
 auto* Once=M->FindTrack<UMovieSceneParticleTrack>(Saved->Components[1]);if(!TestNotNull(TEXT("Once track"),Once))return false;
 auto* Section=CastChecked<UMovieSceneParticleSection>(Once->GetAllSections()[0]);TestEqual(TEXT("One trigger key"),Section->ParticleKeys.GetData().GetTimes().Num(),1);TestEqual(TEXT("Trigger at effect start"),Section->ParticleKeys.GetData().GetTimes()[0].Value,M->GetTickResolution().AsFrameNumber(6).Value);TestEqual(TEXT("Trigger mode"),Section->ParticleKeys.GetData().GetValues()[0],static_cast<uint8>(EParticleKey::Trigger));
 auto* Spawn=CastChecked<UMovieSceneSpawnSection>(M->FindTrack<UMovieSceneSpawnTrack>(Saved->Actors[0])->GetAllSections()[0]);bool Value=true;Spawn->GetChannel().Evaluate(M->GetTickResolution().AsFrameNumber(5.5),Value);TestFalse(TEXT("Effect absent during gap"),Value);
 auto* Template=CastChecked<ANiagaraActor>(M->FindSpawnable(Saved->Actors[0])->GetObjectTemplate());TestFalse(TEXT("Autoactivation disabled"),Template->GetNiagaraComponent()->bAutoActivate);
 TArray<UObject*,TInlineAllocator<1>> Objects;Q->LocateBoundObjects(Saved->Components[0],UE::UniversalObjectLocator::FResolveParams(Template),Objects);TestTrue(TEXT("Child component binding resolves"),Objects.Contains(Template->GetNiagaraComponent()));
 ALevelSequenceActor* PlaybackActor=nullptr;FMovieSceneSequencePlaybackSettings PlaybackSettings;
 auto* Player=ULevelSequencePlayer::CreateLevelSequencePlayer(GEditor->GetEditorWorldContext().World(),Q.Get(),PlaybackSettings,PlaybackActor);
 if(TestNotNull(TEXT("Presentation player created"),Player)){
  auto Binding=[](FGuid ID){return FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(ID));};
  Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(1.f,EUpdatePositionMethod::Play));
  TestEqual(TEXT("First VFX spawned during its window"),Player->GetBoundObjects(Binding(Saved->Actors[0])).Num(),1);
  TestEqual(TEXT("Component resolves on spawned actor"),Player->GetBoundObjects(Binding(Saved->Components[0])).Num(),1);
  Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(5.5f,EUpdatePositionMethod::Play));
  TestEqual(TEXT("No first VFX during gap"),Player->GetBoundObjects(Binding(Saved->Actors[0])).Num(),0);
  Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(6.f,EUpdatePositionMethod::Play));
  TestEqual(TEXT("Once VFX spawned at its trigger"),Player->GetBoundObjects(Binding(Saved->Actors[1])).Num(),1);
  Player->Stop();
 }
 if(PlaybackActor)PlaybackActor->Destroy();
 D->SpawnKeys=false;TestTrue(TEXT("Build without spawn keys"),HandyPresentation::Build(Q.Get(),*D,nullptr,Report));Saved=Q->FindMetaData<UHandyPresentationData>();
 auto* Unkeyed=CastChecked<UMovieSceneSpawnSection>(M->FindTrack<UMovieSceneSpawnTrack>(Saved->Actors[0])->GetAllSections()[0]);
 TestEqual(TEXT("No on/off spawn keys"),Unkeyed->GetChannel().GetData().GetTimes().Num(),0);bool Alive=false;Unkeyed->GetChannel().Evaluate(M->GetTickResolution().AsFrameNumber(5.5),Alive);TestTrue(TEXT("Unkeyed actor remains alive during fade"),Alive);
 PlaybackActor=nullptr;Player=ULevelSequencePlayer::CreateLevelSequencePlayer(GEditor->GetEditorWorldContext().World(),Q.Get(),PlaybackSettings,PlaybackActor);
 if(Player){Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(1.f,EUpdatePositionMethod::Play));auto ID=FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(Saved->Actors[0]));auto Initial=Player->GetBoundObjects(ID);Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(5.5f,EUpdatePositionMethod::Play));auto After=Player->GetBoundObjects(ID);TestEqual(TEXT("Actor retained after effect end"),After.Num(),1);if(!Initial.IsEmpty()&&!After.IsEmpty())TestEqual(TEXT("Same actor survives boundary"),After[0],Initial[0]);Player->Stop();}if(PlaybackActor)PlaybackActor->Destroy();
 D->SpawnKeys=true;
 TestTrue(TEXT("Clear succeeds"),HandyPresentation::Clear(Q.Get(),Report));TestNotNull(TEXT("Unrelated binding preserved"),M->FindBinding(Existing));TestEqual(TEXT("Generated actors cleared"),M->GetSpawnableCount(),0);
 GEditor->UndoTransaction();TestEqual(TEXT("Undo restores both VFX"),M->GetSpawnableCount(),2);GEditor->RedoTransaction();TestEqual(TEXT("Redo clears both VFX"),M->GetSpawnableCount(),0);
 D->Attach=true;TestFalse(TEXT("Missing mesh blocks build"),HandyPresentation::Build(Q.Get(),*D,nullptr,Report));TestEqual(TEXT("Failed build leaves sequence intact"),M->GetSpawnableCount(),0);
 auto* MeshTemplate=NewObject<ASkeletalMeshActor>(M,NAME_None,RF_Transactional);D->Target=M->AddSpawnable(TEXT("Presenter"),*MeshTemplate);
 TestTrue(TEXT("Attach to existing sequence mesh"),HandyPresentation::Build(Q.Get(),*D,nullptr,Report));Saved=Q->FindMetaData<UHandyPresentationData>();TestNotNull(TEXT("Attach track created"),M->FindTrack<UMovieScene3DAttachTrack>(Saved->Actors[0]));
 D->Items[0].Duration=8;TestTrue(TEXT("Rebuild succeeds"),HandyPresentation::Build(Q.Get(),*D,nullptr,Report));TestEqual(TEXT("Rebuild replaces rather than duplicates actors"),M->GetSpawnableCount(),3);TestNotNull(TEXT("Existing mesh retained"),M->FindSpawnable(D->Target));
 auto* World=GEditor->GetEditorWorldContext().World();const bool WasWorldDirty=World->GetOutermost()->IsDirty();ON_SCOPE_EXIT{World->GetOutermost()->SetDirtyFlag(WasWorldDirty);};FActorSpawnParameters SpawnParams;SpawnParams.ObjectFlags=RF_Transient;auto* Presenter=World->SpawnActor<ASkeletalMeshActor>(SpawnParams);
 if(TestNotNull(TEXT("Test presenter created"),Presenter)){
  TestTrue(TEXT("Build with level skeletal actor"),HandyPresentation::Build(Q.Get(),*D,Presenter,Report));
  Saved=Q->FindMetaData<UHandyPresentationData>();FGuid PresenterBinding=Saved->Target;
  TestTrue(TEXT("Rebuild reuses presenter binding"),HandyPresentation::Build(Q.Get(),*D,Presenter,Report));
  TestEqual(TEXT("Same presenter binding"),Q->FindMetaData<UHandyPresentationData>()->Target,PresenterBinding);
  PlaybackActor=nullptr;Player=ULevelSequencePlayer::CreateLevelSequencePlayer(World,Q.Get(),PlaybackSettings,PlaybackActor);
  if(Player){Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(1.f,EUpdatePositionMethod::Play));auto Bound=Player->GetBoundObjects(FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(Saved->Actors[0])));auto* VFX=Bound.IsEmpty()?nullptr:Cast<AActor>(Bound[0]);if(TestNotNull(TEXT("Attached VFX spawned"),VFX))TestEqual(TEXT("VFX actually attached to presenter"),VFX->GetAttachParentActor(),static_cast<AActor*>(Presenter));Player->Stop();}
  if(PlaybackActor)PlaybackActor->Destroy();Presenter->Destroy();
 }
 TStrongObjectPtr<ULevelSequence> Copy(DuplicateObject<ULevelSequence>(Q.Get(),GetTransientPackage()));
 auto* CopiedSettings=Copy->FindMetaData<UHandyPresentationData>();if(TestNotNull(TEXT("Sequence carries presentation metadata"),CopiedSettings))TestEqual(TEXT("Stored playlist survives duplication"),CopiedSettings->Items.Num(),2);
 return true;
}
#endif


