#include "HandyNaming.h"
#include "HandyAssetSelection.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Text/STextBlock.h"
namespace HandyNaming {
class SNamingRow:public SMultiColumnTableRow<TSharedPtr<FRow>>{
public:
    SLATE_BEGIN_ARGS(SNamingRow){} SLATE_ARGUMENT(TSharedPtr<FRow>,Item) SLATE_END_ARGS()
    TSharedPtr<FRow> Item;
    void Construct(const FArguments& A,const TSharedRef<STableViewBase>& Owner){Item=A._Item;SMultiColumnTableRow::Construct(FSuperRowType::FArguments().Padding(3),Owner);}
    TSharedRef<SWidget> GenerateWidgetForColumn(const FName& C)override{
        if(C==TEXT("Select"))return SNew(SCheckBox).IsEnabled(Item->Ready).IsChecked_Lambda([R=Item](){return R->Selected?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([R=Item](ECheckBoxState S){R->Selected=S==ECheckBoxState::Checked;});
        FString T=C==TEXT("Type")?Item->Type:C==TEXT("Old")?Item->Asset.AssetName.ToString():C==TEXT("New")?Item->Proposed:C==TEXT("Path")?Item->Asset.PackagePath.ToString():Item->Status;
        return SNew(STextBlock).Text(FText::FromString(T)).ToolTipText(FText::FromString(T)).OverflowPolicy(ETextOverflowPolicy::Ellipsis).ColorAndOpacity(Item->Ready?FLinearColor(.68f,.79f,.88f):FLinearColor(.70f,.70f,.70f));
    }
};
class SNamingPanel:public SCompoundWidget{
public:
    SLATE_BEGIN_ARGS(SNamingPanel){} SLATE_END_ARGS()
    TArray<FString> Folders,Prefixes,Suffixes;TArray<FAssetData> Assets;
    TArray<TSharedPtr<FRule>> Rules;TArray<TSharedPtr<FRow>> Rows,Visible;TArray<FHistory> History;
    TSharedPtr<SVerticalBox> RuleBox;TSharedPtr<SListView<TSharedPtr<FRow>>> List;
    FString Report=TEXT("Choose folders or individual assets, edit rules, and preview. Renaming and redirector fix-up use Unreal's asset tools and may save referring packages outside the selected folders."),Filter;
    TSharedRef<SWidget> Button(const TCHAR* T,TFunction<void()> F){return SNew(SButton).Text(FText::FromString(T)).ButtonColorAndOpacity(FLinearColor(.14f,.23f,.30f)).OnClicked_Lambda([F](){F();return FReply::Handled();});}
    void Refresh(){Visible.Reset();for(const auto& R:Rows)if(Filter.IsEmpty()||(R->Asset.GetObjectPathString()+R->Type+R->Proposed+R->Status).Contains(Filter))Visible.Add(R);if(List)List->RequestListRefresh();}
    void Invalidate(){Rows.Reset();Refresh();Report=TEXT("Selection or rules changed. Preview again before renaming.");}
    void RuleHistory(){for(const auto& R:Rules){if(!R->Prefix.IsEmpty())Prefixes.AddUnique(R->Prefix);if(!R->Suffix.IsEmpty())Suffixes.AddUnique(R->Suffix);}}
    void RebuildRules(){
        RuleBox->ClearChildren();
        RuleBox->AddSlot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("Enabled    Asset type / class                         Prefix                     Suffix")))];
        for(const auto& R:Rules)RuleBox->AddSlot().AutoHeight().Padding(0,2)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().Padding(3)[SNew(SCheckBox).IsChecked(R->Enabled?ECheckBoxState::Checked:ECheckBoxState::Unchecked).OnCheckStateChanged_Lambda([this,R](ECheckBoxState S){R->Enabled=S==ECheckBoxState::Checked;Invalidate();})]
            +SHorizontalBox::Slot().FillWidth(.42f)[SNew(SEditableTextBox).Text(FText::FromString(R->Type)).HintText(FText::FromString(TEXT("Asset class, e.g. SoundWave"))).OnTextChanged_Lambda([this,R](const FText& T){R->Type=T.ToString();Invalidate();})]
            +SHorizontalBox::Slot().FillWidth(.24f).Padding(4,0)[SNew(SEditableTextBox).Text(FText::FromString(R->Prefix)).HintText(FText::FromString(TEXT("Prefix"))).OnTextCommitted_Lambda([this,R](const FText& T,ETextCommit::Type){if(!R->Prefix.IsEmpty())Prefixes.AddUnique(R->Prefix);R->Prefix=T.ToString();Invalidate();})]
            +SHorizontalBox::Slot().FillWidth(.24f)[SNew(SEditableTextBox).Text(FText::FromString(R->Suffix)).HintText(FText::FromString(TEXT("Suffix (optional)"))).OnTextCommitted_Lambda([this,R](const FText& T,ETextCommit::Type){if(!R->Suffix.IsEmpty())Suffixes.AddUnique(R->Suffix);R->Suffix=T.ToString();Invalidate();})]
            +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[Button(TEXT("Remove"),[this,R](){RuleHistory();Rules.Remove(R);RebuildRules();Invalidate();})]];
    }
    void Scan(){
        if(HandySelection::HasUnsavedContent(Report))return;
        auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();Registry.SearchAllAssets(true);if(!Folders.IsEmpty())Registry.ScanPathsSynchronous(Folders,true);
        RuleHistory();Rows=Preview(HandySelection::Gather(Folders,Assets),Rules,Prefixes,Suffixes);Refresh();int32 Ready=0;for(const auto& R:Rows)Ready+=R->Ready?1:0;
        Report=FString::Printf(TEXT("%d assets reviewed; %d ready. Existing recognized prefixes/suffixes are replaced, not stacked. Collisions and unsupported types are skipped."),Rows.Num(),Ready);
    }
    void Rename(){
        TArray<FAssetData> Selected;for(const auto& R:Rows)if(R->Selected&&R->Ready)Selected.Add(R->Asset);
        auto Fresh=Preview(Selected,Rules,Prefixes,Suffixes);for(const auto& R:Fresh)if(!R->Ready){Report=TEXT("Preview is no longer valid. Preview again; nothing renamed.");return;}
        if(Fresh.IsEmpty()){Report=TEXT("Select ready assets first.");return;}
        FString Details;for(const auto& R:Fresh)Details+=R->Asset.GetObjectPathString()+TEXT(" -> ")+R->Proposed+TEXT("\n");
        if(FMessageDialog::Open(EAppMsgType::YesNo,FText::FromString(TEXT("Rename these assets and run Unreal's redirector fix-up? This can save referencing packages outside the selection. Code/config text paths require manual checking. Fix-up is not ordinary Undo; use Revert Last Rename for this batch after fix-up completes. Keep a backup/source-control revision.\n\n")+Details))!=EAppReturnType::Yes)return;
        if(Apply(Fresh,History,Report)){Rows.Reset();Refresh();}
    }
    void Construct(const FArguments&){
        Rules=Defaults();RuleHistory();TArray<FString> Saved;
        bool Configured=false;GConfig->GetBool(TEXT("HandyToolbox.Naming"),TEXT("RulesConfigured"),Configured,GEditorPerProjectIni);
        const bool HasSavedRules=GConfig->GetArray(TEXT("HandyToolbox.Naming"),TEXT("Rules"),Saved,GEditorPerProjectIni)>0;
        if(Configured||HasSavedRules){
            Rules.Reset();for(const FString& Line:Saved){TArray<FString> P;Line.ParseIntoArray(P,TEXT("|"),false);if(P.Num()==4){auto R=MakeShared<FRule>();R->Type=P[0];R->Prefix=P[1];R->Suffix=P[2];R->Enabled=P[3]==TEXT("1");Rules.Add(R);}}
        }
        TArray<FString> Old;GConfig->GetArray(TEXT("HandyToolbox.Naming"),TEXT("KnownPrefixes"),Old,GEditorPerProjectIni);for(const auto& P:Old)Prefixes.AddUnique(P);
        GConfig->GetArray(TEXT("HandyToolbox.Naming"),TEXT("KnownSuffixes"),Suffixes,GEditorPerProjectIni);RuleHistory();
        ChildSlot[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(TEXT("Naming Convention"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",14)).ColorAndOpacity(FLinearColor(.68f,.79f,.88f))]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Use Selected Folders"),[this](){auto F=HandySelection::SelectedFolders();if(F.IsEmpty()||F.ContainsByPredicate([](const FString& P){return !HandySelection::IsProjectPath(P);})){Report=TEXT("Select project Content folders.");return;}Folders=F;Assets.Reset();Invalidate();})]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Add Selected Assets"),[this](){for(const auto& A:HandySelection::SelectedAssets())Assets.AddUnique(A);Invalidate();})]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Clear Scope"),[this](){Folders.Reset();Assets.Reset();Invalidate();})]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Preview Names"),[this](){Scan();})]]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text_Lambda([this](){return FText::FromString(FString::Join(Folders,TEXT(", "))+FString::Printf(TEXT(" | %d individual assets"),Assets.Num()));}).AutoWrapText(true)]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SExpandableArea).InitiallyCollapsed(false).HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Prefix and suffix rules")))]
                .BodyContent()[SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(225)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(RuleBox,SVerticalBox)]]]
                    +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                        +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Add Rule"),[this](){Rules.Add(MakeShared<FRule>());RebuildRules();Invalidate();})]
                        +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Save Rules"),[this](){RuleHistory();TArray<FString> Lines;for(const auto& R:Rules)Lines.Add(R->Type+TEXT("|")+R->Prefix+TEXT("|")+R->Suffix+(R->Enabled?TEXT("|1"):TEXT("|0")));GConfig->SetBool(TEXT("HandyToolbox.Naming"),TEXT("RulesConfigured"),true,GEditorPerProjectIni);GConfig->SetArray(TEXT("HandyToolbox.Naming"),TEXT("Rules"),Lines,GEditorPerProjectIni);GConfig->SetArray(TEXT("HandyToolbox.Naming"),TEXT("KnownPrefixes"),Prefixes,GEditorPerProjectIni);GConfig->SetArray(TEXT("HandyToolbox.Naming"),TEXT("KnownSuffixes"),Suffixes,GEditorPerProjectIni);GConfig->Flush(false,GEditorPerProjectIni);Report=TEXT("Rules saved for this project.");})]
                        +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Restore Defaults"),[this](){RuleHistory();Rules=Defaults();RebuildRules();Invalidate();})]]]]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SSearchBox).HintText(FText::FromString(TEXT("Filter preview by asset, type, path or status"))).OnTextChanged_Lambda([this](const FText& T){Filter=T.ToString();Refresh();})]
            +SVerticalBox::Slot().FillHeight(1).Padding(8)[SAssignNew(List,SListView<TSharedPtr<FRow>>).ListItemsSource(&Visible).SelectionMode(ESelectionMode::None)
                .OnGenerateRow_Lambda([](TSharedPtr<FRow> R,const TSharedRef<STableViewBase>& O){return SNew(SNamingRow,O).Item(R);})
                .HeaderRow(SNew(SHeaderRow)+SHeaderRow::Column(TEXT("Select")).FixedWidth(26).DefaultLabel(FText::GetEmpty())
                    +SHeaderRow::Column(TEXT("Type")).FillWidth(.17f).DefaultLabel(FText::FromString(TEXT("Asset type")))
                    +SHeaderRow::Column(TEXT("Old")).FillWidth(.20f).DefaultLabel(FText::FromString(TEXT("Current name")))
                    +SHeaderRow::Column(TEXT("New")).FillWidth(.20f).DefaultLabel(FText::FromString(TEXT("New name")))
                    +SHeaderRow::Column(TEXT("Path")).FillWidth(.23f).DefaultLabel(FText::FromString(TEXT("Directory")))
                    +SHeaderRow::Column(TEXT("Status")).FillWidth(.20f).DefaultLabel(FText::FromString(TEXT("Status"))))]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Select Ready"),[this](){for(auto& R:Visible)R->Selected=R->Ready;})]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Clear Selection"),[this](){for(auto& R:Rows)R->Selected=false;})]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Rename and Fix Redirectors"),[this](){Rename();})]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Revert Last Rename"),[this](){if(FMessageDialog::Open(EAppMsgType::YesNo,FText::FromString(TEXT("Restore names from the last completed batch and fix its redirectors? This saves affected packages.")))==EAppReturnType::Yes){Revert(History,Report);Rows.Reset();Refresh();}})]]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text_Lambda([this](){return FText::FromString(Report);}).AutoWrapText(true)]];
        RebuildRules();
    }
};
TSharedRef<SWidget> MakePanel(){return SNew(SNamingPanel);}
}
