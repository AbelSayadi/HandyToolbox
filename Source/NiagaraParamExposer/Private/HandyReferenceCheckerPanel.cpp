#include "HandyReferenceChecker.h"
#include "HandyAssetSelection.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "IContentBrowserDataModule.h"
#include "ContentBrowserDataSubsystem.h"
#include "Editor.h"
#include "Misc/MessageDialog.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Layout/SBox.h"
namespace HandyReferences {
class SReferenceRow:public SMultiColumnTableRow<TSharedPtr<FRow>> {
public:
    SLATE_BEGIN_ARGS(SReferenceRow){} SLATE_ARGUMENT(TSharedPtr<FRow>,Item) SLATE_END_ARGS()
    TSharedPtr<FRow> Item;
    void Construct(const FArguments& A,const TSharedRef<STableViewBase>& Owner){Item=A._Item;SMultiColumnTableRow::Construct(FSuperRowType::FArguments().Padding(2),Owner);}
    TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override {
        if(Column==TEXT("Select")) return SNew(SCheckBox).IsEnabled(Item->Replacement.StartsWith(TEXT("/Game/")))
            .IsChecked_Lambda([R=Item](){return R->Selected?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([R=Item](ECheckBoxState S){R->Selected=S==ECheckBoxState::Checked;});
        FString Text=Column==TEXT("Scope")?Item->Scope:Column==TEXT("Source")?Item->Source:Column==TEXT("Target")?Item->Target:Column==TEXT("Detail")?Item->Detail:Column==TEXT("Match")?Item->Replacement:Item->Status;
        return SNew(SBox).Padding(5)[SNew(STextBlock).Text(FText::FromString(Text)).ToolTipText(FText::FromString(Text)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
            .ColorAndOpacity(Item->Outside?FLinearColor(.91f,.83f,.68f):FLinearColor(.66f,.68f,.70f))];
    }
};
class SReferencePanel:public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SReferencePanel){} SLATE_END_ARGS()
    FString Root,Report=TEXT("Select folders or add individual assets. Each folder is checked independently. Engine and Niagara dependencies are allowed. No assets are deleted or saved by this tool."),Filter;
    TArray<FString> Folders;
    TArray<FAssetData> Assets;
    TArray<TSharedPtr<FRow>> Rows,Visible,ExternalRows,LocalRows,AllowedRows;
    TSharedPtr<SListView<TSharedPtr<FRow>>> ExternalList,LocalList,AllowedList;
    TSharedPtr<SExpandableArea> ExternalArea,LocalArea,AllowedArea;
    TSharedRef<SWidget> Button(const TCHAR* Label,TFunction<void()> Action) {
        return SNew(SButton).Text(FText::FromString(Label)).ButtonColorAndOpacity(FLinearColor(.14f,.23f,.30f))
            .OnClicked_Lambda([Action](){Action();return FReply::Handled();});
    }
    void Refresh(){
        Visible.Reset();ExternalRows.Reset();LocalRows.Reset();AllowedRows.Reset();
        for(const auto& R:Rows)if(Filter.IsEmpty()||(R->Scope+R->Source+R->Target+R->Detail+R->Status+R->Replacement).Contains(Filter)){
            Visible.Add(R);if(R->Outside)ExternalRows.Add(R);else if(R->Status==TEXT("LOCAL"))LocalRows.Add(R);else AllowedRows.Add(R);
        }
        for(auto List:{ExternalList,LocalList,AllowedList})if(List)List->RequestListRefresh();
    }
    void UseSelectedFolders(const TArray<FString>& Selected) {
        const auto Paths=IContentBrowserDataModule::Get().GetSubsystem()->TryConvertVirtualPathsToInternal(Selected);
        if(Paths.IsEmpty() || Paths.ContainsByPredicate([](const FString& P){return !HandySelection::IsProjectPath(P);})){Report=TEXT("Select project Content folders.");return;}
        Folders.Reset();for(const auto& P:Paths)Folders.AddUnique(P);Assets.Reset();Root=Folders[0];Rows.Reset();Refresh();Report=TEXT("Folders selected. Each folder will be scanned independently.");
    }
    void RunScan(){
        Rows.Reset();TMap<FString,TArray<FName>> Scopes;
        for(const auto& F:Folders)Scopes.FindOrAdd(F);
        for(const auto& A:Assets){const FString P=A.PackagePath.ToString();if(!Folders.Contains(P))Scopes.FindOrAdd(P).AddUnique(A.PackageName);}
        if(Scopes.IsEmpty()){Report=TEXT("Choose folders or assets first.");Refresh();return;}
        TArray<FString> Messages;for(const auto& Scope:Scopes){FString Message;Rows.Append(Scan(Scope.Key,Message,Scope.Value));Messages.Add(Scope.Key+TEXT(": ")+Message);}
        Report=FString::Join(Messages,TEXT("\n"));Refresh();
    }
    TSharedRef<SWidget> Group(const TCHAR* Label,TArray<TSharedPtr<FRow>>* Items,TSharedPtr<SListView<TSharedPtr<FRow>>>& List,TSharedPtr<SExpandableArea>& Area,bool Collapsed){
        return SAssignNew(Area,SExpandableArea).InitiallyCollapsed(Collapsed)
            .HeaderContent()[SNew(STextBlock).Text_Lambda([Items,Title=FString(Label)](){return FText::FromString(FString::Printf(TEXT("%s (%d)"),*Title,Items->Num()));}).ColorAndOpacity(Collapsed?FLinearColor(.66f,.68f,.70f):FLinearColor(.91f,.83f,.68f))]
            .BodyContent()[SNew(SBox).HeightOverride_Lambda([Items](){return FMath::Clamp(Items->Num()*29.f+34.f,80.f,550.f);})
                [SAssignNew(List,SListView<TSharedPtr<FRow>>).ListItemsSource(Items).SelectionMode(ESelectionMode::None)
                    .OnGenerateRow_Lambda([](TSharedPtr<FRow> R,const TSharedRef<STableViewBase>& Owner){return SNew(SReferenceRow,Owner).Item(R);})
                    .HeaderRow(SNew(SHeaderRow)
                        +SHeaderRow::Column(TEXT("Select")).DefaultLabel(FText::GetEmpty()).FixedWidth(28)
                        +SHeaderRow::Column(TEXT("Scope")).DefaultLabel(FText::FromString(TEXT("Folder scope"))).FillWidth(.15f)
                        +SHeaderRow::Column(TEXT("Status")).DefaultLabel(FText::FromString(TEXT("Status"))).FillWidth(.10f)
                        +SHeaderRow::Column(TEXT("Source")).DefaultLabel(FText::FromString(TEXT("Source asset"))).FillWidth(.19f)
                        +SHeaderRow::Column(TEXT("Detail")).DefaultLabel(FText::FromString(TEXT("Emitter / node / property"))).FillWidth(.20f)
                        +SHeaderRow::Column(TEXT("Target")).DefaultLabel(FText::FromString(TEXT("Referenced asset"))).FillWidth(.18f)
                        +SHeaderRow::Column(TEXT("Match")).DefaultLabel(FText::FromString(TEXT("Verified local replacement"))).FillWidth(.18f))]];
    }
    void Construct(const FArguments&) {
        ChildSlot[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(TEXT("Reference Checker"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",14)).ColorAndOpacity(FLinearColor(.68f,.79f,.88f))]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Use Selected Folders"),[this](){
                    TArray<FString> Paths;FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().GetSelectedPathViewFolders(Paths);
                    UseSelectedFolders(Paths);})]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Add Selected Assets"),[this](){for(const auto& A:HandySelection::SelectedAssets())Assets.AddUnique(A);Rows.Reset();Refresh();Report=FString::Printf(TEXT("%d folders and %d explicit assets selected. Asset-only scopes use each asset's own folder."),Folders.Num(),Assets.Num());})]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Clear Scope"),[this](){Folders.Reset();Assets.Reset();Root.Empty();Rows.Reset();Refresh();})]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Scan References"),[this](){RunScan();})]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Undo"),[this](){if(GEditor)GEditor->UndoTransaction();Rows.Reset();Refresh();Report=TEXT("Undo requested. Save or discard changes before scanning again.");})]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Redo"),[this](){if(GEditor)GEditor->RedoTransaction();Rows.Reset();Refresh();Report=TEXT("Redo requested. Save or discard changes before scanning again.");})]]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text_Lambda([this](){return FText::FromString(FString::Printf(TEXT("Folders: %s | Individual assets: %d"),*FString::Join(Folders,TEXT(", ")),Assets.Num()));})]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SSearchBox).HintText(FText::FromString(TEXT("Filter source, destination, emitter, property, or status"))).OnTextChanged_Lambda([this](const FText& T){Filter=T.ToString();Refresh();})]
            +SVerticalBox::Slot().FillHeight(1).Padding(8)[SNew(SScrollBox)
                +SScrollBox::Slot()[Group(TEXT("External"),&ExternalRows,ExternalList,ExternalArea,false)]
                +SScrollBox::Slot()[Group(TEXT("Local"),&LocalRows,LocalList,LocalArea,true)]
                +SScrollBox::Slot()[Group(TEXT("Engine / Niagara — allowed"),&AllowedRows,AllowedList,AllowedArea,true)]]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Select Verified Matches"),[this](){for(auto& R:Visible)R->Selected=R->Replacement.StartsWith(TEXT("/Game/"));})]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Clear Selection"),[this](){for(auto& R:Rows)R->Selected=false;})]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Relink Selected"),[this](){
                    int32 Count=0;FString Preview;
                    for(const auto& R:Rows)if(R->Selected){++Count;Preview+=R->Scope+TEXT("\n")+R->Source+TEXT("\n  ")+R->Target+TEXT(" -> ")+R->Replacement+TEXT("\n");}
                    if(!Count){Report=TEXT("Select a verified match first.");return;}
                    if(FMessageDialog::Open(EAppMsgType::YesNo,FText::FromString(TEXT("Replace every occurrence of each selected reference in its source asset inside its own folder scope? External originals stay intact. Changes are undoable and will not be saved automatically.\n\n")+Preview))!=EAppReturnType::Yes)return;
                    if(Relink(Root,Rows,Report)){Rows.Reset();Refresh();}})]]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text_Lambda([this](){return FText::FromString(Report);}).AutoWrapText(true)]
        ];
    }
};
TSharedRef<SWidget> MakePanel(){return SNew(SReferencePanel);}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandyReferenceFolderSelectionTest,"NiagaraParamExposer.References.FolderSelection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHandyReferenceFolderSelectionTest::RunTest(const FString&)
{
    auto Panel=SNew(HandyReferences::SReferencePanel);
    const FString Internal=TEXT("/Game/FireAura");
    const FName Virtual=IContentBrowserDataModule::Get().GetSubsystem()->ConvertInternalPathToVirtual(FName(*Internal));
    Panel->UseSelectedFolders({Virtual.ToString()});
    TestEqual(TEXT("Content Browser virtual selection becomes package folder"),Panel->Root,Internal);
    Panel->UseSelectedFolders({TEXT("/All/Game/FireAura")});
    TestEqual(TEXT("All-root virtual path accepted"),Panel->Report,FString(TEXT("Folders selected. Each folder will be scanned independently.")));
    TestEqual(TEXT("All-root path converted"),Panel->Root,Internal);
    Panel->UseSelectedFolders({});
    TestEqual(TEXT("Empty selection rejected"),Panel->Report,FString(TEXT("Select project Content folders.")));
    Panel->UseSelectedFolders({TEXT("/All/Engine")});
    TestEqual(TEXT("Engine selection rejected"),Panel->Report,FString(TEXT("Select project Content folders.")));
    TestEqual(TEXT("Invalid selection preserves previous scope"),Panel->Root,Internal);
    TestTrue(TEXT("External expanded"),Panel->ExternalArea->IsExpanded());
    TestFalse(TEXT("Local collapsed"),Panel->LocalArea->IsExpanded());
    TestFalse(TEXT("Engine collapsed"),Panel->AllowedArea->IsExpanded());
    Panel->UseSelectedFolders({TEXT("/All/Game/FireAura"),TEXT("/All/Game/OtherPack")});
    TestEqual(TEXT("Two independent folder scopes retained"),Panel->Folders.Num(),2);
    return true;
}
#endif
