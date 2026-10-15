#include "HandyAssetCleanup.h"
#include "HandyAssetSelection.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "IContentBrowserDataModule.h"
#include "ContentBrowserDataSubsystem.h"
#include "ObjectTools.h"
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
#include "Widgets/Layout/SSeparator.h"

namespace HandyAssetCleanup
{
static FString AssetType(const FAssetData& Asset)
{
    const FString Type = Asset.AssetClassPath.GetAssetName().ToString();
    if (Type.StartsWith(TEXT("MaterialInstance"))) return TEXT("Material Instance");
    if (Type.StartsWith(TEXT("MaterialFunction"))) return TEXT("Material Function");
    if (Type.StartsWith(TEXT("Texture"))) return TEXT("Texture");
    if (Type == TEXT("StaticMesh")) return TEXT("Static Mesh");
    if (Type == TEXT("SkeletalMesh")) return TEXT("Skeletal Mesh");
    if (Type == TEXT("NiagaraSystem")) return TEXT("Niagara System");
    if (Type == TEXT("NiagaraEmitter")) return TEXT("Niagara Emitter");
    return Type;
}
class SAssetRow : public SMultiColumnTableRow<TSharedPtr<FRow>>
{
public:
    SLATE_BEGIN_ARGS(SAssetRow) {} SLATE_ARGUMENT(TSharedPtr<FRow>, Item) SLATE_END_ARGS()
    void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& Owner)
    {
        Item = Args._Item;
        SMultiColumnTableRow<TSharedPtr<FRow>>::Construct(FSuperRowType::FArguments().Padding(2), Owner);
    }
    virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
    {
        if (Column == TEXT("Select"))
            return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
                [SNew(SCheckBox).IsEnabled(Item->bEligible)
                    .ToolTipText(FText::FromString(Item->bEligible ? TEXT("Select this asset for removal. Confirmation is still required.") : TEXT("Protected asset: cannot be selected for removal.")))
                    .IsChecked_Lambda([Item = Item]() { return Item->bSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([Item = Item](ECheckBoxState State) { Item->bSelected = State == ECheckBoxState::Checked; })];
        FString Value, Tip;
        FLinearColor Color(.86f,.89f,.92f);
        if (Column == TEXT("Name")) Value = Item->Asset.AssetName.ToString();
        else if (Column == TEXT("Type")) { Value = AssetType(Item->Asset); Tip = Item->Asset.AssetClassPath.GetAssetName().ToString(); Color = FLinearColor(.68f,.79f,.88f); }
        else if (Column == TEXT("Status"))
        {
            Value = Item->bEligible ? TEXT("REMOVE") : TEXT("KEEP");
            Tip = Item->bEligible ? TEXT("Eligible for removal; only checked assets are deleted after confirmation.") : TEXT("Protected and excluded from deletion.");
            Color = Item->bEligible ? FLinearColor(.91f,.83f,.68f) : FLinearColor(.68f,.79f,.88f);
        }
        else if (Column == TEXT("Directory")) { Value = Item->Asset.PackagePath.ToString(); Tip = Item->Asset.GetObjectPathString(); Color = FLinearColor(.66f,.69f,.73f); }
        else if (Column == TEXT("Reason")) Value = Item->Reason;
        if (Tip.IsEmpty()) Tip = Value;
        return SNew(SBox).Padding(FMargin(6,5)).VAlign(VAlign_Center)
            [SNew(STextBlock).Text(FText::FromString(Value)).ToolTipText(FText::FromString(Tip))
                .ColorAndOpacity(Color).OverflowPolicy(ETextOverflowPolicy::Ellipsis)];
    }
private:
    TSharedPtr<FRow> Item;
};
class SPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPanel) {} SLATE_END_ARGS()
    void Construct(const FArguments&)
    {
        ChildSlot
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(8)
            [SNew(STextBlock).Text(FText::FromString(TEXT("Asset Cleanup"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 14)).ColorAndOpacity(FLinearColor(.68f,.79f,.88f))]
            + SVerticalBox::Slot().AutoHeight().Padding(8,0)
            [SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("Review materials, material instances, material functions, textures and meshes in selected folders. All Niagara dependencies and other retained project references are protected.")))]
            + SVerticalBox::Slot().AutoHeight().Padding(8)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [SNew(SButton).Text(FText::FromString(TEXT("Use Selected Folders"))).ButtonColorAndOpacity(FLinearColor(.14f,.23f,.30f)).ForegroundColor(FLinearColor(.86f,.92f,.97f)).OnClicked_Lambda([this]()
                {
                    TArray<FString> Selected;
                    FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().GetSelectedPathViewFolders(Selected);
                    ExplicitAssets.Reset(); Folders = IContentBrowserDataModule::Get().GetSubsystem()->TryConvertVirtualPathsToInternal(Selected); Rows.Reset(); FilterRows();
                    Scope = Folders.IsEmpty() ? TEXT("No folders selected") : FString::Join(Folders, TEXT(", "));
                    Status = TEXT("Click Scan to review assets. Nothing has been deleted.");
                    return FReply::Handled();
                })]
                + SHorizontalBox::Slot().AutoWidth().Padding(8,0)
                [SNew(SButton).Text(FText::FromString(TEXT("Add Selected Assets"))).ButtonColorAndOpacity(FLinearColor(.14f,.23f,.30f)).OnClicked_Lambda([this](){for(const auto& A:HandySelection::SelectedAssets())ExplicitAssets.AddUnique(A);Rows.Reset();FilterRows();Scope=FString::Join(Folders,TEXT(", "))+FString::Printf(TEXT(" | %d individual assets"),ExplicitAssets.Num());return FReply::Handled();})]
                + SHorizontalBox::Slot().AutoWidth().Padding(8,0)
                [SNew(SButton).Text(FText::FromString(TEXT("Clear Scope"))).OnClicked_Lambda([this](){Folders.Reset();ExplicitAssets.Reset();Rows.Reset();FilterRows();Scope=TEXT("No scope selected");return FReply::Handled();})]
                + SHorizontalBox::Slot().AutoWidth().Padding(8,0)
                [SNew(SButton).Text(FText::FromString(TEXT("Scan"))).ButtonColorAndOpacity(FLinearColor(.14f,.23f,.30f))
                    .OnClicked_Lambda([this]() { RunScan(); return FReply::Handled(); })]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,0)
            [SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(Scope); }).AutoWrapText(true)]
            + SVerticalBox::Slot().AutoHeight().Padding(8)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1)
                [SNew(SSearchBox).HintText(FText::FromString(TEXT("Filter by name, type, directory or reason")))
                    .OnTextChanged_Lambda([this](const FText& Text) { Search = Text.ToString(); FilterRows(); })]
                + SHorizontalBox::Slot().AutoWidth().Padding(8,0)
                [SNew(SButton).Text(FText::FromString(TEXT("Select All Eligible"))).ButtonColorAndOpacity(FLinearColor(.14f,.23f,.30f)).ForegroundColor(FLinearColor(.86f,.92f,.97f)).OnClicked_Lambda([this]() { for (auto& Row : Rows) Row->bSelected = Row->bEligible; return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth()
                [SNew(SButton).Text(FText::FromString(TEXT("Clear Selection"))).ButtonColorAndOpacity(FLinearColor(.19f,.23f,.24f)).OnClicked_Lambda([this]() { for (auto& Row : Rows) Row->bSelected = false; return FReply::Handled(); })]
            ]
            + SVerticalBox::Slot().FillHeight(1).Padding(8,0)
            [SAssignNew(List, SListView<TSharedPtr<FRow>>).ListItemsSource(&VisibleRows).SelectionMode(ESelectionMode::None)
                .HeaderRow
                (SNew(SHeaderRow)
                    + SHeaderRow::Column(TEXT("Select")).DefaultLabel(FText::GetEmpty()).FixedWidth(32)
                    + SHeaderRow::Column(TEXT("Name")).DefaultLabel(FText::FromString(TEXT("Asset Name"))).FillWidth(.23f)
                    + SHeaderRow::Column(TEXT("Type")).DefaultLabel(FText::FromString(TEXT("Type"))).FillWidth(.14f)
                    + SHeaderRow::Column(TEXT("Status")).DefaultLabel(FText::FromString(TEXT("Keep / Remove"))).FillWidth(.12f)
                    + SHeaderRow::Column(TEXT("Directory")).DefaultLabel(FText::FromString(TEXT("Directory"))).FillWidth(.26f)
                    + SHeaderRow::Column(TEXT("Reason")).DefaultLabel(FText::FromString(TEXT("Reason"))).FillWidth(.25f)
                )
                .OnGenerateRow_Lambda([](TSharedPtr<FRow> Row, const TSharedRef<STableViewBase>& Owner)
                { return SNew(SAssetRow, Owner).Item(Row); })]

            + SVerticalBox::Slot().AutoHeight().Padding(8)
            [SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(Status); }).AutoWrapText(true)]
            + SVerticalBox::Slot().AutoHeight().Padding(8,0)
            [SNew(STextBlock).Text(FText::FromString(TEXT("Deletion removes asset files and is not covered by the parameter tab's Undo/Redo. Code/config string references may not be discoverable. Review the list and keep a backup or source-control revision."))).AutoWrapText(true)]
            + SVerticalBox::Slot().AutoHeight().Padding(8)
            [SNew(SButton).Text_Lambda([this]() { int32 Count = 0; for (const auto& Row : Rows) if (Row->bSelected) ++Count; return FText::FromString(FString::Printf(TEXT("Review and Delete Selected (%d)"), Count)); })
                .ButtonColorAndOpacity(FLinearColor(.24f,.19f,.12f)).ForegroundColor(FLinearColor(.91f,.83f,.68f)).ContentPadding(FMargin(10,5))
                .IsEnabled_Lambda([this]() { return Rows.ContainsByPredicate([](const TSharedPtr<FRow>& Row) { return Row->bEligible && Row->bSelected; }); })
                .OnClicked_Lambda([this]() { DeleteSelected(); return FReply::Handled(); })]
        ];
    }
private:
    TArray<FString> Folders;
    TArray<FAssetData> ExplicitAssets;
    TArray<TSharedPtr<FRow>> Rows, VisibleRows;
    TSharedPtr<SListView<TSharedPtr<FRow>>> List;
    FString Scope = TEXT("Choose project Content folders in the Content Browser."), Status = TEXT("No scan yet. Nothing has been deleted."), Search;
    void FilterRows()
    {
        VisibleRows.Reset();
        for (const auto& Row : Rows) if (Search.IsEmpty() || Row->Asset.PackageName.ToString().Contains(Search) || Row->Reason.Contains(Search) || AssetType(Row->Asset).Contains(Search)) VisibleRows.Add(Row);
        if (List) List->RequestListRefresh();
    }
    void RunScan()
    {
        FScan Result = Scan(Folders,ExplicitAssets);
        Rows = MoveTemp(Result.Rows);
        int32 Eligible = 0; for (const auto& Row : Rows) if (Row->bEligible) ++Eligible;
        Status = Result.Error.IsEmpty() ? FString::Printf(TEXT("%d assets reviewed; %d eligible, %d protected. Checked dependencies of %d Niagara Systems. Select assets to delete."), Rows.Num(), Eligible, Rows.Num()-Eligible, Result.NiagaraSystems) : Result.Error;
        FilterRows();
    }
    void DeleteSelected()
    {
        TSet<FName> Requested;
        for (const auto& Row : Rows) if (Row->bEligible && Row->bSelected) Requested.Add(Row->Asset.PackageName);
        FScan Fresh = Scan(Folders,ExplicitAssets); // saved state may have changed since preview
        if (!Fresh.Error.IsEmpty()) { Status = Fresh.Error; return; }
        TSet<FName> Allowed;
        for (const auto& Row : Fresh.Rows) if (Row->bEligible && Requested.Contains(Row->Asset.PackageName)) Allowed.Add(Row->Asset.PackageName);
        IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        FEdges References;
        for (FName Name : Allowed) Registry.GetReferencers(Name, References.FindOrAdd(Name), UE::AssetRegistry::EDependencyCategory::All);
        Allowed = SafeSubset(Allowed, References); // unselected candidates must also be protected
        if (Allowed.Num() != Requested.Num())
        {
            Rows = MoveTemp(Fresh.Rows); FilterRows();
            Status = TEXT("Deletion stopped: references changed, or selected assets are dependencies of assets you did not select. Review the refreshed list and select the complete unused group. No assets deleted."); return;
        }
        if (Allowed.IsEmpty()) return;
        TArray<FAssetData> Assets;
        TArray<FString> Names;
        for (const auto& Row : Fresh.Rows) if (Allowed.Contains(Row->Asset.PackageName)) { Assets.Add(Row->Asset); Names.Add(Row->Asset.PackageName.ToString()); }
        FString Details;
        for (int32 I = 0; I < FMath::Min(Names.Num(), 12); ++I) Details += Names[I] + TEXT("\n");
        if (Names.Num() > 12) Details += FString::Printf(TEXT("...and %d more selected assets.\n"), Names.Num()-12);
        const FText Warning = FText::FromString(FString::Printf(TEXT("Delete %d selected assets from these folders?\n%s\n\n%s\nThis removes asset files from disk. Parameter Undo/Redo cannot restore them. Keep a backup or source-control revision. Unreal will show its own deletion checks next. Continue?"), Assets.Num(), *Scope, *Details));
        if (FMessageDialog::Open(EAppMsgType::YesNo, Warning) != EAppReturnType::Yes) { Status = TEXT("Deletion cancelled. No assets deleted."); return; }
        const int32 Deleted = ObjectTools::DeleteAssets(Assets, true);
        Rows.Reset(); FilterRows();
        Status = FString::Printf(TEXT("Unreal reported %d of %d assets deleted. Scan again to refresh the remaining assets."), Deleted, Assets.Num());
    }
};
TSharedRef<SWidget> MakePanel() { return SNew(SPanel); }
}
