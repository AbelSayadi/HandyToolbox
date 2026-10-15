#include "HandyBatchParameters.h"
#include "HandyAssetSelection.h"
#include "NiagaraParamExposerCleanup.h"
#include "NiagaraSystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ScopedTransaction.h"
#include "Misc/MessageDialog.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
namespace HandyBatchParameters {
class SBatchPanel:public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SBatchPanel){} SLATE_END_ARGS()
    TArray<FString> Folders;TArray<FAssetData> Assets;TArray<TWeakObjectPtr<UNiagaraSystem>> Systems;
    FString Report=TEXT("Choose folders or Niagara systems, then preview. Cleanup preserves referenced parameters, live-emitter parameters, and existing order. Nothing is saved automatically.");bool HasPreview=false;
    TSharedRef<SWidget> Button(const TCHAR* T,TFunction<void()> F){return SNew(SButton).Text(FText::FromString(T)).ButtonColorAndOpacity(FLinearColor(.14f,.23f,.30f)).OnClicked_Lambda([F](){F();return FReply::Handled();});}
    bool Preview(){
        HasPreview=false;Systems.Reset();auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();Registry.SearchAllAssets(true);
        TArray<FString> Lines;
        for(const auto& A:HandySelection::Gather(Folders,Assets))if(A.AssetClassPath==UNiagaraSystem::StaticClass()->GetClassPathName()){
            auto* System=Cast<UNiagaraSystem>(A.GetAsset());if(!System){Report=TEXT("Could not load ")+A.GetObjectPathString();return false;}
            auto R=NiagaraParamExposerCleanup::Cleanup(System,true);if(!R.bSuccess){Report=R.Message;return false;}
            Systems.Add(System);Lines.Add(System->GetPathName()+TEXT("\n  ")+R.Message);
        }
        Report=Lines.IsEmpty()?TEXT("No Niagara systems in the selected scope."):FString::Join(Lines,TEXT("\n\n"));HasPreview=!Systems.IsEmpty();return HasPreview;
    }
    void Apply(){
        if(!HasPreview){Report=TEXT("Preview the batch first.");return;}
        if(!Preview())return;
        if(FMessageDialog::Open(EAppMsgType::YesNo,FText::FromString(FString::Printf(TEXT("Clean unused parameters in %d systems? Undo restores this entire batch. Assets are not saved automatically.\n\n"),Systems.Num())+Report))!=EAppReturnType::Yes)return;
        FScopedTransaction Transaction(NSLOCTEXT("HandyToolbox","BatchParameters","Clean Unused Parameters in Niagara Systems"));TArray<FString> Lines;
        for(const auto& Weak:Systems)if(auto* System=Weak.Get()){auto R=NiagaraParamExposerCleanup::Cleanup(System);Lines.Add(System->GetPathName()+TEXT(": ")+R.Message);}
        Report=FString::Join(Lines,TEXT("\n"));HasPreview=false;
    }
    void Construct(const FArguments&){
        ChildSlot[SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Batch unused-parameter cleanup"))).ColorAndOpacity(FLinearColor(.68f,.79f,.88f))]
            .BodyContent()[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(3)[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Use Selected Folders"),[this](){auto F=HandySelection::SelectedFolders();if(F.IsEmpty()||F.ContainsByPredicate([](const FString& P){return !HandySelection::IsProjectPath(P);})){Report=TEXT("Select project Content folders.");return;}Folders=F;Assets.Reset();HasPreview=false;})]
                    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Add Selected Systems"),[this](){for(const auto& A:HandySelection::SelectedAssets())if(A.AssetClassPath==UNiagaraSystem::StaticClass()->GetClassPathName())Assets.AddUnique(A);HasPreview=false;})]
                    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Clear Scope"),[this](){Folders.Reset();Assets.Reset();Systems.Reset();HasPreview=false;Report=TEXT("Scope cleared.");})]]
                +SVerticalBox::Slot().AutoHeight().Padding(3)[SNew(STextBlock).Text_Lambda([this](){return FText::FromString(FString::Join(Folders,TEXT(", "))+FString::Printf(TEXT(" | %d individual systems"),Assets.Num()));}).AutoWrapText(true)]
                +SVerticalBox::Slot().AutoHeight().Padding(3)[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Preview Batch"),[this](){Preview();})]
                    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Clean Up Batch"),[this](){Apply();})]]
                +SVerticalBox::Slot().AutoHeight().Padding(3)[SNew(SBox).HeightOverride(140)[SNew(SScrollBox)+SScrollBox::Slot()[SNew(STextBlock).Text_Lambda([this](){return FText::FromString(Report);}).AutoWrapText(true)]]]]];
    }
};
TSharedRef<SWidget> MakePanel(){return SNew(SBatchPanel);}
}
