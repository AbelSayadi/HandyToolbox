#include "HandyPackSanity.h"
#include "HandyAssetSelection.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/SecureHash.h"
#include "Misc/PackageName.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Text/STextBlock.h"
namespace HandySanity {
static FString ReviewKey(const FRow& R){return FMD5::HashAnsiString(*R.Key());}
class SSanityRow:public SMultiColumnTableRow<TSharedPtr<FRow>>{
public:
 SLATE_BEGIN_ARGS(SSanityRow){} SLATE_ARGUMENT(TSharedPtr<FRow>,Item) SLATE_ARGUMENT(bool,Accepted) SLATE_END_ARGS()
 TSharedPtr<FRow> Item;bool Accepted=false;
 void Construct(const FArguments& A,const TSharedRef<STableViewBase>& Owner){Item=A._Item;Accepted=A._Accepted;SMultiColumnTableRow::Construct(FSuperRowType::FArguments().Padding(4),Owner);}
 TSharedRef<SWidget> GenerateWidgetForColumn(const FName& C)override{
  FString T=C==TEXT("Severity")?(Accepted?TEXT("Reviewed"):Item->Severity==0?TEXT("Error"):Item->Severity==1?TEXT("Warning"):TEXT("Info")):C==TEXT("Check")?Item->Check:C==TEXT("Asset")?Item->Asset:C==TEXT("Type")?Item->Type:C==TEXT("Scope")?Item->Scope:C==TEXT("Related")?Item->Related:Item->Detail;
  FLinearColor Color=Accepted?FLinearColor(.68f,.79f,.88f):Item->Severity==0?FLinearColor(.95f,.5f,.45f):Item->Severity==1?FLinearColor(.91f,.83f,.68f):FLinearColor(.66f,.68f,.70f);
  return SNew(STextBlock).Text(FText::FromString(T)).ToolTipText(FText::FromString(T)).ColorAndOpacity(Color).OverflowPolicy(ETextOverflowPolicy::Ellipsis);
 }
};
class SSanityPanel:public SCompoundWidget{
public:
 SLATE_BEGIN_ARGS(SSanityPanel){} SLATE_END_ARGS()
 FOptions Options;TArray<FString> Folders;TArray<FAssetData> Assets;FResult Result;TArray<TSharedPtr<FRow>> Visible;TSharedPtr<FRow> Selected;
 TSharedPtr<SListView<TSharedPtr<FRow>>> List;TMap<FString,FString> Reviews;TFunction<void(int32)> Navigate;
 FString Filter,Note,Report=TEXT("Select pack folders and run a read-only check. Each folder is independent. Scan saved assets; nothing is renamed, relinked, resized or deleted.");bool ShowReviewed=true;
 TSharedRef<SWidget> Label(const TCHAR* S){return SNew(STextBlock).Text(FText::FromString(S));}
 TSharedRef<SWidget> Button(const TCHAR* S,TFunction<void()> F){return SNew(SButton).Text(FText::FromString(S)).ButtonColorAndOpacity(FLinearColor(.14f,.23f,.30f)).OnClicked_Lambda([F](){F();return FReply::Handled();});}
 void Refresh(){Visible.Reset();for(const auto& R:Result.Rows)if((ShowReviewed||!Reviews.Contains(ReviewKey(*R)))&&(Filter.IsEmpty()||(R->Scope+R->Asset+R->Type+R->Check+R->Detail+R->Related).Contains(Filter)))Visible.Add(R);if(List)List->RequestListRefresh();}
 void Invalidate(){Result=FResult();Selected.Reset();Refresh();Report=TEXT("Scope or settings changed. Run Pack Sanity Check again.");}
 void Save(){const TCHAR* S=TEXT("HandyToolbox.PackSanity");GConfig->SetInt(S,TEXT("TextureWarning"),Options.TextureWarning,GEditorPerProjectIni);GConfig->SetInt(S,TEXT("TriangleWarning"),Options.TriangleWarning,GEditorPerProjectIni);
  TArray<FString> Values;for(const auto& P:Reviews)Values.Add(P.Key+TEXT("|")+P.Value);GConfig->SetArray(S,TEXT("Reviews"),Values,GEditorPerProjectIni);GConfig->Flush(false,GEditorPerProjectIni);
 }
 TSharedRef<SWidget> Check(const TCHAR* Text,bool* Value){return SNew(SCheckBox).IsChecked_Lambda([Value](){return *Value?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([this,Value](ECheckBoxState C){*Value=C==ECheckBoxState::Checked;Invalidate();})[Label(Text)];}
 FAssetData Resolve(const FString& Path){auto& R=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();auto A=R.GetAssetByObjectPath(FSoftObjectPath(Path));if(!A.IsValid()){TArray<FAssetData> Matches;R.GetAssetsByPackageName(FName(*FPackageName::ObjectPathToPackageName(Path)),Matches);if(!Matches.IsEmpty())A=Matches[0];}return A;}
 void Open(bool Related){if(!Selected)return;auto A=Resolve(Related?Selected->Related:Selected->Asset);if(auto* O=A.GetAsset())GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(O);else Report=TEXT("This asset is missing or the finding applies to the whole folder.");}
 void Run(){Result=Scan(Folders,Assets,Options);Selected.Reset();Refresh();int32 Errors=0,Warnings=0;for(const auto& R:Result.Rows){Errors+=R->Severity==0;Warnings+=R->Severity==1;}
  Report=!Result.Error.IsEmpty()?Result.Error:FString::Printf(TEXT("%d asset entries across %d independent scopes: %d errors, %d warnings, %d findings total. %sRead-only report; Info/pending findings and reviewed exceptions still need judgment."),Result.Assets,Result.Scopes,Errors,Warnings,Result.Rows.Num(),Result.Canceled?TEXT("Canceled: report is incomplete. "):TEXT(""));
 }
 void Construct(const FArguments&,TFunction<void(int32)> OpenTool){
  Navigate=MoveTemp(OpenTool);const TCHAR* S=TEXT("HandyToolbox.PackSanity");GConfig->GetInt(S,TEXT("TextureWarning"),Options.TextureWarning,GEditorPerProjectIni);GConfig->GetInt(S,TEXT("TriangleWarning"),Options.TriangleWarning,GEditorPerProjectIni);
  TArray<FString> Values;GConfig->GetArray(S,TEXT("Reviews"),Values,GEditorPerProjectIni);for(const auto& V:Values){FString K,N;if(V.Split(TEXT("|"),&K,&N))Reviews.Add(K,N);}
  ChildSlot[SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(TEXT("Pack Sanity Check"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",16)).ColorAndOpacity(FLinearColor(.68f,.79f,.88f))]
   +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Use Selected Folders"),[this](){auto F=HandySelection::SelectedFolders();if(F.IsEmpty()){Report=TEXT("Select Content Browser folders first.");return;}Folders=F;Assets.Reset();Invalidate();})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Add Selected Assets"),[this](){for(const auto& A:HandySelection::SelectedAssets())Assets.AddUnique(A);Invalidate();})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Clear Scope"),[this](){Folders.Reset();Assets.Reset();Invalidate();})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Run Pack Sanity Check"),[this](){Run();})]]
   +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this](){return FText::FromString(FString::Join(Folders,TEXT(", "))+FString::Printf(TEXT(" | %d individual assets"),Assets.Num()));})]
   +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SExpandableArea).InitiallyCollapsed(false).HeaderContent()[Label(TEXT("Checks and warning limits"))].BodyContent()[SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
     +SHorizontalBox::Slot().AutoWidth().Padding(3)[Check(TEXT("References / missing assets"),&Options.References)]
     +SHorizontalBox::Slot().AutoWidth().Padding(3)[Check(TEXT("Naming"),&Options.Naming)]
     +SHorizontalBox::Slot().AutoWidth().Padding(3)[Check(TEXT("Compile status"),&Options.Compile)]
     +SHorizontalBox::Slot().AutoWidth().Padding(3)[Check(TEXT("Unused content"),&Options.Unused)]
     +SHorizontalBox::Slot().AutoWidth().Padding(3)[Check(TEXT("Duplicates"),&Options.Duplicates)]]
    +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
     +SHorizontalBox::Slot().AutoWidth().Padding(3)[Check(TEXT("Textures: size / power of two"),&Options.Textures)]
     +SHorizontalBox::Slot().AutoWidth().Padding(3)[Label(TEXT("Warn at pixels >="))]
     +SHorizontalBox::Slot().FillWidth(1).Padding(3)[SNew(SNumericEntryBox<int32>).MinValue(1).Value_Lambda([this](){return TOptional<int32>(Options.TextureWarning);}).OnValueCommitted_Lambda([this](int32 N,ETextCommit::Type){Options.TextureWarning=N;Save();Invalidate();})]
     +SHorizontalBox::Slot().AutoWidth().Padding(3)[Check(TEXT("Meshes: LOD0 triangles >"),&Options.Meshes)]
     +SHorizontalBox::Slot().FillWidth(1).Padding(3)[SNew(SNumericEntryBox<int32>).MinValue(1).Value_Lambda([this](){return TOptional<int32>(Options.TriangleWarning);}).OnValueCommitted_Lambda([this](int32 N,ETextCommit::Type){Options.TriangleWarning=N;Save();Invalidate();})]]
    +SVerticalBox::Slot().AutoHeight().Padding(3)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("2K+ flipbooks can be intentional: select a warning and record a review note. Limits are configurable review thresholds, not performance measurements. Compile checks use cached editor results; duplicate verification covers Texture2D and native materials/functions/instances. Engine and Niagara references are allowed.")))]]]
   +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SSearchBox).HintText(FText::FromString(TEXT("Filter by folder, asset, check or details"))).OnTextChanged_Lambda([this](const FText& T){Filter=T.ToString();Refresh();})]
   +SVerticalBox::Slot().FillHeight(1).Padding(6)[SAssignNew(List,SListView<TSharedPtr<FRow>>).ListItemsSource(&Visible).SelectionMode(ESelectionMode::Single).OnSelectionChanged_Lambda([this](TSharedPtr<FRow> R,ESelectInfo::Type){Selected=R;Note=R?Reviews.FindRef(ReviewKey(*R)):FString();})
    .OnGenerateRow_Lambda([this](TSharedPtr<FRow> R,const TSharedRef<STableViewBase>& Owner){return SNew(SSanityRow,Owner).Item(R).Accepted(Reviews.Contains(ReviewKey(*R)));})
    .HeaderRow(SNew(SHeaderRow)
     +SHeaderRow::Column(TEXT("Severity")).DefaultLabel(FText::FromString(TEXT("Status"))).FixedWidth(80)
     +SHeaderRow::Column(TEXT("Check")).DefaultLabel(FText::FromString(TEXT("Check"))).FillWidth(.13f)
     +SHeaderRow::Column(TEXT("Type")).DefaultLabel(FText::FromString(TEXT("Type"))).FillWidth(.09f)
     +SHeaderRow::Column(TEXT("Asset")).DefaultLabel(FText::FromString(TEXT("Asset / directory"))).FillWidth(.21f)
     +SHeaderRow::Column(TEXT("Scope")).DefaultLabel(FText::FromString(TEXT("Pack folder"))).FillWidth(.12f)
     +SHeaderRow::Column(TEXT("Detail")).DefaultLabel(FText::FromString(TEXT("Details"))).FillWidth(.28f)
     +SHeaderRow::Column(TEXT("Related")).DefaultLabel(FText::FromString(TEXT("Reference / duplicate"))).FillWidth(.17f))]
   +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this](){return FText::FromString(Selected?Selected->Asset+TEXT("\n")+Selected->Detail+(Selected->Related.IsEmpty()?FString():TEXT("\n")+Selected->Related):TEXT("Select a finding for full details."));})]
   +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Open Asset"),[this](){Open(false);})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Open Related Asset"),[this](){Open(true);})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Open Repair Tool"),[this](){if(!Selected||Selected->Tool<0){Report=TEXT("Open this asset to review its settings.");return;}auto A=Resolve(Selected->Asset);if(A.IsValid())FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().SyncBrowserToAssets({A});Navigate(Selected->Tool);})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Copy Report"),[this](){FString Text=Report;for(const auto& R:Result.Rows)Text+=TEXT("\n")+R->Scope+TEXT(" | ")+R->Asset+TEXT(" | ")+R->Check+TEXT(" | ")+R->Detail+TEXT(" | ")+R->Related+(Reviews.Contains(ReviewKey(*R))?TEXT(" | REVIEWED: ")+Reviews.FindRef(ReviewKey(*R)):FString());FPlatformApplicationMisc::ClipboardCopy(*Text);})]]
   +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().FillWidth(1).Padding(2)[SNew(SEditableTextBox).HintText(FText::FromString(TEXT("Review note, e.g. intentional 8x8 flipbook atlas"))).Text_Lambda([this](){return FText::FromString(Note);}).OnTextChanged_Lambda([this](const FText& T){Note=T.ToString();})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Mark Reviewed"),[this](){if(!Selected||Selected->Severity==0||Note.TrimStartAndEnd().IsEmpty()){Report=TEXT("Select a non-error finding and enter a reason. Errors cannot be dismissed.");return;}Reviews.Add(ReviewKey(*Selected),Note.Replace(TEXT("|"),TEXT("/")).Replace(TEXT("\n"),TEXT(" ")));Save();List->RebuildList();Refresh();})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[Button(TEXT("Reset Review"),[this](){if(Selected){Reviews.Remove(ReviewKey(*Selected));Note.Empty();Save();List->RebuildList();Refresh();}})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SCheckBox).IsChecked(ECheckBoxState::Checked).OnCheckStateChanged_Lambda([this](ECheckBoxState S){ShowReviewed=S==ECheckBoxState::Checked;Refresh();})[Label(TEXT("Show reviewed"))]]]
   +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this](){return FText::FromString(Report);})]
  ];
 }
};
TSharedRef<SWidget> MakePanel(TFunction<void(int32)> OpenTool){return SNew(SSanityPanel,MoveTemp(OpenTool));}
}
