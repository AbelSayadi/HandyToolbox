#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HandyPresentation.generated.h"
class UNiagaraSystem;
class ULevelSequence;
class AActor;
class SWidget;
USTRUCT()
struct FHandyPresentationItem {
 GENERATED_BODY()
 UPROPERTY() TSoftObjectPtr<UNiagaraSystem> System;
 UPROPERTY() bool Render=true;
 UPROPERTY() double Duration=0; // Zero inherits the default.
 UPROPERTY() int32 Mode=0; // Inherit, Loop, Once.
 UPROPERTY() int32 Attach=0; // Inherit, Attach, World.
};
UCLASS()
class UHandyPresentationData:public UObject {
 GENERATED_BODY()
public:
 virtual bool IsEditorOnly() const override { return true; }
 UPROPERTY() TArray<FHandyPresentationItem> Items;
 UPROPERTY() double Duration=5;
 UPROPERTY() double Gap=1;
 UPROPERTY() double Start=0;
 UPROPERTY() bool Loop=true;
 UPROPERTY() bool SpawnKeys=true; // Off keeps sequence-owned actors alive for natural fades.
 UPROPERTY() bool Attach=true;
 UPROPERTY() FGuid Target;
 UPROPERTY() FName Socket;
 UPROPERTY() TArray<FGuid> Actors;
 UPROPERTY() TArray<FGuid> Components;
};
namespace HandyPresentation {
struct FSlot { FFrameNumber Start,End; bool Loop,Attach; };
bool Plan(const UHandyPresentationData& Data,FFrameRate Rate,TArray<FSlot>& Slots,FString& Error);
bool Build(ULevelSequence* Sequence,const UHandyPresentationData& Settings,AActor* Target,FString& Report);
bool Clear(ULevelSequence* Sequence,FString& Report);
bool HasSkeletalSampling(UNiagaraSystem* System);
TSharedRef<SWidget> MakePanel();
}
