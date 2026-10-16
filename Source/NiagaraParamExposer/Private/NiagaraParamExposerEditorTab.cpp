// Copyright NiagaraParamExposer. All Rights Reserved.

#include "NiagaraParamExposerEditorTab.h"
#include "HandyReferenceChecker.h"
#include "HandyNaming.h"
#include "HandyPresentation.h"
#include "HandyPackSanity.h"
#include "HandyBatchParameters.h"

#include "NiagaraParamExposerCore.h"
#include "NiagaraParamExposerCleanup.h"
#include "HandyAssetCleanup.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SBox.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "NiagaraParamExposerLibrary.h"
#include "NiagaraParamExposerInputFilter.h"
#include "NiagaraParamExposerNaming.h"
#include "NiagaraParamExposerTypes.h"
#include "NiagaraSystem.h"
#include "NiagaraNodeFunctionCall.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Framework/Docking/TabManager.h"
#include "PropertyCustomizationHelpers.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Misc/Parse.h"

#define LOCTEXT_NAMESPACE "NiagaraParamExposerEditor"

static const FName NiagaraParamExposerEditorTabId(TEXT("NiagaraParamExposerEditorTab"));
static TWeakObjectPtr<UNiagaraSystem> GNiagaraParamExposerEditorSystem;

struct FNiagaraParamExposerEditorInputRow
{
	bool bSelected = false;
	bool bImportantTier = false;
	bool bHideUnlessShowAll = false;
	bool bHideAlwaysUnlessSearch = false;
	FString StageLabel;
	FString ModuleName;
	FString ModuleKey;
	FString InputName;
	FString TypeName;
	FString CurrentValue;
	FString ProposedUserName;
	ENiagaraParamExposerScriptUsage ScriptUsage = ENiagaraParamExposerScriptUsage::Any;
};

class SNiagaraParamExposerEditorTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraParamExposerEditorTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
        for (const TCHAR* Type : {TEXT("Float"), TEXT("Vector2D"), TEXT("Vector"), TEXT("Color"), TEXT("Int"), TEXT("Bool")})
            SetParamTypes.Add(MakeShared<FString>(Type));
        SelectedSetParamType = SetParamTypes[0];
        SelectedCreateType = SetParamTypes[0];
		bAutoName = true;
		bAssignEmitterCategory = true;

		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(6)
			[
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [
                    SNew(STextBlock).Text(LOCTEXT("Title", "Niagara Param Exposer"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                    .ColorAndOpacity(FLinearColor(0.68f, 0.79f, 0.88f))
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(6, 0)
                [SNew(SButton).Text(LOCTEXT("UndoAction", "Undo"))
                    .ToolTipText(LOCTEXT("UndoActionTip", "Undo the latest Unreal Editor action, including every plugin edit. Uses the same history as Ctrl+Z."))
                    .IsEnabled_Lambda([]() { return GEditor && GEditor->Trans && GEditor->Trans->CanUndo(); })
                    .OnClicked_Lambda([this]() { GEditor->UndoTransaction(); RefreshEmitters(); AppendLog(TEXT("Undid the latest editor action.")); return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth()
                [SNew(SButton).Text(LOCTEXT("RedoAction", "Redo"))
                    .ToolTipText(LOCTEXT("RedoActionTip", "Redo the latest undone editor action, including every plugin edit."))
                    .IsEnabled_Lambda([]() { return GEditor && GEditor->Trans && GEditor->Trans->CanRedo(); })
                    .OnClicked_Lambda([this]() { GEditor->RedoTransaction(); RefreshEmitters(); AppendLog(TEXT("Redid the latest editor action.")); return FReply::Handled(); })]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SAssignNew(SystemPicker, SObjectPropertyEntryBox)
					.AllowedClass(UNiagaraSystem::StaticClass())
					.ObjectPath_Lambda([this]() { return GetSystemPath(); })
					.OnObjectChanged_Lambda([this](const FAssetData& AssetData) { OnSystemChanged(AssetData); })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("UseSelected", "Use Content Browser Selection"))
					.OnClicked(this, &SNiagaraParamExposerEditorTab::OnUseContentBrowserSelection)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(6, 0)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("EmitterSearch", "Search emitters"))
				.OnTextChanged_Lambda([this](const FText& Text)
				{
					EmitterSearchText = Text.ToString();
					FilterEmitters();
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.4f).Padding(2)
				[
					SAssignNew(EmitterCombo, SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&EmitterOptions)
					.OnGenerateWidget(this, &SNiagaraParamExposerEditorTab::OnGenerateStringWidget)
					.OnSelectionChanged(this, &SNiagaraParamExposerEditorTab::OnEmitterChanged)
					[
						SNew(STextBlock).Text(this, &SNiagaraParamExposerEditorTab::GetEmitterText)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
				[
					SNew(SButton).Text(LOCTEXT("RenameEmitter", "Rename Emitter"))
					.OnClicked(this, &SNiagaraParamExposerEditorTab::OnRenameEmitterClicked)
				]
				+ SHorizontalBox::Slot().FillWidth(0.4f).Padding(2)
				[
					SAssignNew(RenameEmitterNameBox, SEditableTextBox)
					.HintText(LOCTEXT("NewEmitterName", "New emitter name"))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(4)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bAutoName ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bAutoName = State == ECheckBoxState::Checked; RefreshInputRows(); })
					[
						SNew(STextBlock).Text(LOCTEXT("AutoName", "Auto-name User parameters"))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bAssignEmitterCategory ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bAssignEmitterCategory = State == ECheckBoxState::Checked; })
					[
						SNew(STextBlock).Text(LOCTEXT("EmitterCategory", "Put in emitter category"))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(6, 0)
			[
				SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Filter by stage, module, input, or user parameter name"))
				.OnTextChanged(this, &SNiagaraParamExposerEditorTab::OnSearchTextChanged)
			]
			+ SVerticalBox::Slot().FillHeight(0.55f).Padding(6)
			[
				SAssignNew(InputScrollBox, SScrollBox)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(6)
			[
				SNew(SButton).Text(LOCTEXT("ExposeSelected", "Expose Selected Inputs"))
                    .ButtonColorAndOpacity(FLinearColor(0.14f, 0.23f, 0.30f))
                    .ForegroundColor(FLinearColor(0.86f, 0.92f, 0.97f))
				.OnClicked(this, &SNiagaraParamExposerEditorTab::OnExposeSelected)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(6)
			[
				SNew(SButton).Text(LOCTEXT("RepairCategories", "Repair Emitter Categories"))
                    .ButtonColorAndOpacity(FLinearColor(0.19f, 0.23f, 0.24f))
				.ToolTipText(LOCTEXT("RepairCategoriesTip", "Group all existing parameters by their emitter-name prefix, preserving values and links."))
				.OnClicked_Lambda([this]()
				{
					const auto Result = UNiagaraParamExposerLibrary::RepairEmitterParameterCategories(WeakSystem.Get());
					AppendLog(Result.Message);
					return FReply::Handled();
				})
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(6)
			[
				SNew(SExpandableArea).AreaTitle(LOCTEXT("CreateParam", "Create custom User parameter"))
				.BodyContent()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(2)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(0.3f)[SAssignNew(CreateNameBox, SEditableTextBox).HintText(LOCTEXT("CreateName", "Name"))]
						+ SHorizontalBox::Slot().FillWidth(0.2f)[SNew(SComboBox<TSharedPtr<FString>>)
                            .OptionsSource(&SetParamTypes).InitiallySelectedItem(SelectedCreateType)
                            .OnGenerateWidget(this, &SNiagaraParamExposerEditorTab::OnGenerateStringWidget)
                            .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Type, ESelectInfo::Type)
                            {
                                SelectedCreateType = Type;
                                const FString Default = *Type == TEXT("Color") ? TEXT("1,1,1,1") : *Type == TEXT("Vector") ? TEXT("0,0,0") : *Type == TEXT("Vector2D") ? TEXT("0,0") : *Type == TEXT("Bool") ? TEXT("false") : TEXT("0");
                                CreateValueBox->SetText(FText::FromString(Default));
                            })
                            [SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(*SelectedCreateType); })]]
						+ SHorizontalBox::Slot().FillWidth(0.2f)[SAssignNew(CreateValueBox, SEditableTextBox).Text(FText::FromString(TEXT("0")))]
						+ SHorizontalBox::Slot().FillWidth(0.3f)[SAssignNew(CreateCategoryBox, SEditableTextBox).HintText(LOCTEXT("CreateCategory", "Optional category"))]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(2)
					[
						SNew(SButton).Text(LOCTEXT("CreateParamBtn", "Create parameter"))
                    .ButtonColorAndOpacity(FLinearColor(0.14f, 0.23f, 0.30f))
                    .ForegroundColor(FLinearColor(0.86f, 0.92f, 0.97f))
						.OnClicked(this, &SNiagaraParamExposerEditorTab::OnCreateParameter)
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(6)
			[
				SNew(SExpandableArea).AreaTitle(LOCTEXT("SetParams", "Add Set Parameters module"))
				.BodyContent()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(2)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(0.35f)[SAssignNew(SetParamNameBox, SEditableTextBox).Text(FText::FromString(TEXT("Emitter.HueShift")))]
                        + SHorizontalBox::Slot().FillWidth(0.15f)
                        [
                            SNew(SComboBox<TSharedPtr<FString>>)
                            .OptionsSource(&SetParamTypes).InitiallySelectedItem(SelectedSetParamType)
                            .OnGenerateWidget(this, &SNiagaraParamExposerEditorTab::OnGenerateStringWidget)
                            .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Type, ESelectInfo::Type)
                            {
                                SelectedSetParamType = Type;
                                const FString Default = *Type == TEXT("Color") ? TEXT("1,1,1,1") : *Type == TEXT("Vector") ? TEXT("0,0,0") : *Type == TEXT("Vector2D") ? TEXT("0,0") : *Type == TEXT("Bool") ? TEXT("false") : TEXT("0");
                                SetParamValueBox->SetText(FText::FromString(Default));
                            })
                            [SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(*SelectedSetParamType); })]
                        ]
						+ SHorizontalBox::Slot().FillWidth(0.15f)[SAssignNew(SetParamValueBox, SEditableTextBox).ToolTipText(LOCTEXT("TypedDefault", "Default value. Vector: X,Y,Z. Color: R,G,B,A (0 to 1)." )).Text(FText::FromString(TEXT("0")))]
						+ SHorizontalBox::Slot().FillWidth(0.25f)[SAssignNew(SetParamLinkBox, SEditableTextBox).Text(FText::FromString(TEXT("auto")))]
						+ SHorizontalBox::Slot().FillWidth(0.25f)
						[
							SAssignNew(SetParamStageCombo, SComboBox<TSharedPtr<FString>>)
							.OptionsSource(&StageOptions)
							.OnGenerateWidget(this, &SNiagaraParamExposerEditorTab::OnGenerateStringWidget)
							.OnSelectionChanged(this, &SNiagaraParamExposerEditorTab::OnSetParamStageChanged)
							[
								SNew(STextBlock).Text(this, &SNiagaraParamExposerEditorTab::GetSetParamStageText)
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(2)
					[
						SNew(SButton).Text(LOCTEXT("AddSetParam", "Add Set Parameters module"))
                    .ButtonColorAndOpacity(FLinearColor(0.14f, 0.23f, 0.30f))
                    .ForegroundColor(FLinearColor(0.86f, 0.92f, 0.97f))
						.OnClicked(this, &SNiagaraParamExposerEditorTab::OnAddSetParameterModule)
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(6)
			[
				SNew(SSeparator)
			]
			+ SVerticalBox::Slot().FillHeight(0.2f).Padding(6)
			[
				SAssignNew(LogBox, SMultiLineEditableTextBox)
				.IsReadOnly(true)
				.AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6, 2, 6, 8)
            [
                SNew(SButton).Text(LOCTEXT("CleanupUnused", "Clean Up Unused User Parameters"))
                .ButtonColorAndOpacity(FLinearColor(0.24f, 0.19f, 0.12f))
                .ForegroundColor(FLinearColor(0.91f, 0.83f, 0.68f))
                .ContentPadding(FMargin(10, 5))
                .ToolTipText(LOCTEXT("CleanupUnusedTip", "Remove unreferenced User parameters. Protects existing emitter names/categories and references, including disabled emitters. Removes newly empty categories and preserves remaining order. Undo restores the cleanup."))
                .OnClicked_Lambda([this]() { AppendLog(NiagaraParamExposerCleanup::Cleanup(WeakSystem.Get()).Message); RefreshInputRows(); return FReply::Handled(); })
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,4)[HandyBatchParameters::MakePanel()]
        ];

		StageOptions.Add(MakeShared<FString>(TEXT("Emitter Spawn")));
		StageOptions.Add(MakeShared<FString>(TEXT("Emitter Update")));
		StageOptions.Add(MakeShared<FString>(TEXT("Particle Spawn")));
		StageOptions.Add(MakeShared<FString>(TEXT("Particle Update")));
		SelectedSetParamStage = StageOptions[0];
		if (SetParamStageCombo.IsValid())
		{
			SetParamStageCombo->SetSelectedItem(SelectedSetParamStage);
		}

		SetSystem(GNiagaraParamExposerEditorSystem.Get());
	}

	void SetSystem(UNiagaraSystem* InSystem)
	{
		WeakSystem = InSystem;
		GNiagaraParamExposerEditorSystem = InSystem;
		RefreshEmitters();
		AppendLog(InSystem ? FString::Printf(TEXT("Loaded system: %s"), *InSystem->GetName()) : TEXT("No system selected."));
	}

private:
	TWeakObjectPtr<UNiagaraSystem> WeakSystem;
	TSharedPtr<SObjectPropertyEntryBox> SystemPicker;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> EmitterCombo;
	TArray<TSharedPtr<FString>> EmitterOptions;
	TArray<TSharedPtr<FString>> AllEmitterOptions;
	FString EmitterSearchText;
	TSharedPtr<FString> SelectedEmitter;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SScrollBox> InputScrollBox;
	TArray<TSharedPtr<FNiagaraParamExposerEditorInputRow>> InputRows;
	TArray<FString> ModuleSectionOrder;
	TMap<FString, TArray<TSharedPtr<FNiagaraParamExposerEditorInputRow>>> RowsByModuleKey;
	TMap<FString, bool> ModuleShowAllExpanded;
	FString SearchFilterText;
	TSharedPtr<SEditableTextBox> RenameEmitterNameBox;
	TSharedPtr<SEditableTextBox> CreateNameBox;
	TSharedPtr<FString> SelectedCreateType;
	TSharedPtr<SEditableTextBox> CreateValueBox;
	TSharedPtr<SEditableTextBox> CreateCategoryBox;
	TArray<TSharedPtr<FString>> SetParamTypes;
	TSharedPtr<FString> SelectedSetParamType;
	TSharedPtr<SEditableTextBox> SetParamNameBox;
	TSharedPtr<SEditableTextBox> SetParamValueBox;
	TSharedPtr<SEditableTextBox> SetParamLinkBox;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> SetParamStageCombo;
	TArray<TSharedPtr<FString>> StageOptions;
	TSharedPtr<FString> SelectedSetParamStage;
	TSharedPtr<SMultiLineEditableTextBox> LogBox;
	bool bAutoName = true;
	bool bAssignEmitterCategory = true;

	FString GetSystemPath() const
	{
		return WeakSystem.IsValid() ? WeakSystem->GetPathName() : FString();
	}

	void OnSystemChanged(const FAssetData& AssetData)
	{
		SetSystem(Cast<UNiagaraSystem>(AssetData.GetAsset()));
	}

	FReply OnUseContentBrowserSelection()
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		TArray<FAssetData> SelectedAssets;
		ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);
		for (const FAssetData& AssetData : SelectedAssets)
		{
			if (AssetData.AssetClassPath == UNiagaraSystem::StaticClass()->GetClassPathName())
			{
				SetSystem(Cast<UNiagaraSystem>(AssetData.GetAsset()));
				break;
			}
		}
		return FReply::Handled();
	}

	static FString StageToLabel(ENiagaraParamExposerScriptUsage Usage)
	{
		switch (Usage)
		{
		case ENiagaraParamExposerScriptUsage::EmitterSpawn: return TEXT("Emitter Spawn");
		case ENiagaraParamExposerScriptUsage::EmitterUpdate: return TEXT("Emitter Update");
		case ENiagaraParamExposerScriptUsage::ParticleSpawn: return TEXT("Particle Spawn");
		case ENiagaraParamExposerScriptUsage::ParticleUpdate: return TEXT("Particle Update");
		default: return TEXT("Any");
		}
	}

	static ENiagaraParamExposerScriptUsage LabelToStage(const FString& Label)
	{
		if (Label.Contains(TEXT("Emitter")) && Label.Contains(TEXT("Spawn"))) return ENiagaraParamExposerScriptUsage::EmitterSpawn;
		if (Label.Contains(TEXT("Emitter")) && Label.Contains(TEXT("Update"))) return ENiagaraParamExposerScriptUsage::EmitterUpdate;
		if (Label.Contains(TEXT("Particle")) && Label.Contains(TEXT("Spawn"))) return ENiagaraParamExposerScriptUsage::ParticleSpawn;
		if (Label.Contains(TEXT("Particle")) && Label.Contains(TEXT("Update"))) return ENiagaraParamExposerScriptUsage::ParticleUpdate;
		return ENiagaraParamExposerScriptUsage::EmitterSpawn;
	}

	void FilterEmitters()
	{
		EmitterOptions.Reset();
		for (const auto& Emitter : AllEmitterOptions)
		{
			if (NiagaraParamExposerInputFilter::MatchesEmitterSearch(*Emitter, EmitterSearchText)) EmitterOptions.Add(Emitter);
		}
		if (EmitterCombo.IsValid()) EmitterCombo->RefreshOptions();
		if (!EmitterOptions.Contains(SelectedEmitter))
		{
			SelectedEmitter = EmitterOptions.IsEmpty() ? nullptr : EmitterOptions[0];
			if (EmitterCombo.IsValid()) EmitterCombo->SetSelectedItem(SelectedEmitter);
			RefreshInputRows();
		}
	}

	void RefreshEmitters()
	{
		const FString PreviousName = SelectedEmitter.IsValid() ? *SelectedEmitter : FString();
		AllEmitterOptions.Reset();
		SelectedEmitter.Reset();
		if (UNiagaraSystem* System = WeakSystem.Get())
		{
			for (const auto& Emitter : NiagaraParamExposerCore::ListEmitters(System))
			{
				auto Name = MakeShared<FString>(Emitter.EmitterName);
				AllEmitterOptions.Add(Name);
				if (Emitter.EmitterName == PreviousName) SelectedEmitter = Name;
			}
		}
		FilterEmitters();
		if (EmitterCombo.IsValid()) EmitterCombo->SetSelectedItem(SelectedEmitter);
		RefreshInputRows();
	}

	static FString MakeModuleKey(const FString& StageLabel, const FString& ModuleName)
	{
		return StageLabel + TEXT("|") + ModuleName;
	}

	void OnSearchTextChanged(const FText& NewText)
	{
		SearchFilterText = NewText.ToString();
		RebuildInputListUI();
	}

	void RefreshInputRows()
	{
		InputRows.Reset();
		ModuleSectionOrder.Reset();
		RowsByModuleKey.Reset();
		UNiagaraSystem* System = WeakSystem.Get();
		if (!System || !SelectedEmitter.IsValid())
		{
			RebuildInputListUI();
			return;
		}

		const TArray<FNiagaraParamExposerModuleInfo> Modules =
			NiagaraParamExposerCore::ListModules(System, *SelectedEmitter, ENiagaraParamExposerScriptUsage::Any);
		for (const FNiagaraParamExposerModuleInfo& Module : Modules)
		{
			const TArray<FNiagaraParamExposerModuleInputInfo> Inputs = NiagaraParamExposerCore::ListModuleInputs(
				System,
				*SelectedEmitter,
				Module.ModuleName,
				Module.ScriptUsage);

			TArray<FString> InputNamesOnModule;
			for (const FNiagaraParamExposerModuleInputInfo& Input : Inputs)
			{
				InputNamesOnModule.Add(Input.InputName);
			}

			ENiagaraParamExposerScriptUsage ResolvedUsage = Module.ScriptUsage;
			UNiagaraNodeFunctionCall* FunctionCall = NiagaraParamExposerCore::FindModuleFunctionCall(
				System,
				*SelectedEmitter,
				Module.ModuleName,
				Module.ScriptUsage,
				ResolvedUsage);

			const FString StageLabel = StageToLabel(Module.ScriptUsage);
			const FString ModuleKey = MakeModuleKey(StageLabel, Module.ModuleName);
			if (!RowsByModuleKey.Contains(ModuleKey))
			{
				ModuleSectionOrder.Add(ModuleKey);
			}

			for (const FNiagaraParamExposerModuleInputInfo& Input : Inputs)
			{
				if (FunctionCall && NiagaraParamExposerInputFilter::IsParticleStateLifetime(*FunctionCall, Input.InputName)) continue;
				TSharedPtr<FNiagaraParamExposerEditorInputRow> Row = MakeShared<FNiagaraParamExposerEditorInputRow>();
				Row->StageLabel = StageLabel;
				Row->ModuleName = Module.ModuleName;
				Row->ModuleKey = ModuleKey;
				Row->InputName = Input.InputName;
				Row->TypeName = Input.TypeName;
				Row->CurrentValue = Input.ValueAsString;
				Row->ScriptUsage = Module.ScriptUsage;
				Row->ProposedUserName = bAutoName
					? NiagaraParamExposerNaming::MakeAutoUserParameterName(*SelectedEmitter, Input.InputName)
					: Input.InputName;

				const bool bTypeHidden =
					NiagaraParamExposerInputFilter::ShouldHideFromDefaultView(Input.TypeName, Input.InputName);
				const bool bSwitchHidden = FunctionCall
					? NiagaraParamExposerInputFilter::ShouldHideModuleInputForDisplay(
						  *FunctionCall,
						  Input.InputName,
						  Input.TypeName,
						  InputNamesOnModule)
					: false;
				const bool bImportant = NiagaraParamExposerInputFilter::IsImportantInputName(Input.InputName);

				Row->bHideAlwaysUnlessSearch = bTypeHidden;
				Row->bHideUnlessShowAll = bSwitchHidden || (!bImportant && !bTypeHidden);
				Row->bImportantTier = bImportant && !bSwitchHidden && !bTypeHidden;

				InputRows.Add(Row);
				RowsByModuleKey.FindOrAdd(ModuleKey).Add(Row);
			}
		}

		RebuildInputListUI();
	}

	TSharedRef<SWidget> BuildInputRowWidget(const TSharedPtr<FNiagaraParamExposerEditorInputRow>& Item)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([Item]() { return Item->bSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([Item](ECheckBoxState State) { Item->bSelected = State == ECheckBoxState::Checked; })
			]
			+ SHorizontalBox::Slot().FillWidth(0.15f)[SNew(STextBlock).Text(FText::FromString(Item->StageLabel))]
			+ SHorizontalBox::Slot().FillWidth(0.22f)[SNew(STextBlock).Text(FText::FromString(Item->ModuleName))]
			+ SHorizontalBox::Slot().FillWidth(0.18f)[SNew(STextBlock).Text(FText::FromString(Item->InputName))]
			+ SHorizontalBox::Slot().FillWidth(0.1f)[SNew(STextBlock).Text(FText::FromString(Item->TypeName))]
			+ SHorizontalBox::Slot().FillWidth(0.12f)[SNew(STextBlock).Text(FText::FromString(Item->CurrentValue))]
			+ SHorizontalBox::Slot().FillWidth(0.23f)
			[
				SNew(SEditableTextBox)
				.Text(FText::FromString(Item->ProposedUserName))
				.OnTextCommitted_Lambda([Item](const FText& Text, ETextCommit::Type) { Item->ProposedUserName = Text.ToString(); })
			];
	}

	bool RowPassesSearchFilter(const FNiagaraParamExposerEditorInputRow& Row) const
	{
		return NiagaraParamExposerInputFilter::MatchesSearchFilter(
			SearchFilterText,
			Row.ModuleName,
			Row.InputName,
			Row.ProposedUserName);
	}

	bool ShouldShowRowInSection(const FNiagaraParamExposerEditorInputRow& Row, bool bShowAllSection) const
	{
		if (!RowPassesSearchFilter(Row))
		{
			return false;
		}
		if (Row.bHideAlwaysUnlessSearch && SearchFilterText.IsEmpty())
		{
			return false;
		}
		if (bShowAllSection)
		{
			return !Row.bImportantTier;
		}
		return Row.bImportantTier;
	}

	void RebuildInputListUI()
	{
		if (!InputScrollBox.IsValid())
		{
			return;
		}

		InputScrollBox->ClearChildren();

		InputScrollBox->AddSlot().Padding(0, 2)
		[
			SNew(SHeaderRow)
			+ SHeaderRow::Column(TEXT("Select")).DefaultLabel(LOCTEXT("ColSelect", "")).FixedWidth(28)
			+ SHeaderRow::Column(TEXT("Stage")).DefaultLabel(LOCTEXT("ColStage", "Stage")).FillWidth(0.15f)
			+ SHeaderRow::Column(TEXT("Module")).DefaultLabel(LOCTEXT("ColModule", "Module")).FillWidth(0.22f)
			+ SHeaderRow::Column(TEXT("Input")).DefaultLabel(LOCTEXT("ColInput", "Input")).FillWidth(0.18f)
			+ SHeaderRow::Column(TEXT("Type")).DefaultLabel(LOCTEXT("ColType", "Type")).FillWidth(0.1f)
			+ SHeaderRow::Column(TEXT("Value")).DefaultLabel(LOCTEXT("ColValue", "Current")).FillWidth(0.12f)
			+ SHeaderRow::Column(TEXT("UserName")).DefaultLabel(LOCTEXT("ColUser", "User parameter")).FillWidth(0.23f)
		];

		for (const FString& ModuleKey : ModuleSectionOrder)
		{
			const TArray<TSharedPtr<FNiagaraParamExposerEditorInputRow>>* ModuleRows = RowsByModuleKey.Find(ModuleKey);
			if (!ModuleRows || ModuleRows->Num() == 0)
			{
				continue;
			}

			const FString& ModuleName = (*ModuleRows)[0]->ModuleName;
			const FString& StageLabel = (*ModuleRows)[0]->StageLabel;

			TArray<TSharedPtr<FNiagaraParamExposerEditorInputRow>> ImportantRows;
			TArray<TSharedPtr<FNiagaraParamExposerEditorInputRow>> OtherRows;
			for (const TSharedPtr<FNiagaraParamExposerEditorInputRow>& Row : *ModuleRows)
			{
				if (ShouldShowRowInSection(*Row, false))
				{
					ImportantRows.Add(Row);
				}
				if (ShouldShowRowInSection(*Row, true))
				{
					OtherRows.Add(Row);
				}
			}

			if (ImportantRows.Num() == 0 && OtherRows.Num() == 0)
			{
				continue;
			}

			InputScrollBox->AddSlot().Padding(0, 6, 0, 2)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%s — %s"), *StageLabel, *ModuleName)))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			];

			for (const TSharedPtr<FNiagaraParamExposerEditorInputRow>& Row : ImportantRows)
			{
				InputScrollBox->AddSlot().Padding(0, 1)[BuildInputRowWidget(Row)];
			}

			if (OtherRows.Num() > 0)
			{
				const bool bExpanded = ModuleShowAllExpanded.FindRef(ModuleKey);
				TSharedRef<SVerticalBox> OtherBox = SNew(SVerticalBox);
				for (const TSharedPtr<FNiagaraParamExposerEditorInputRow>& Row : OtherRows)
				{
					OtherBox->AddSlot().AutoHeight().Padding(0, 1)[BuildInputRowWidget(Row)];
				}

				InputScrollBox->AddSlot().Padding(0, 2)
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(!bExpanded)
					.AreaTitle(FText::FromString(FString::Printf(TEXT("Show all inputs (%d)"), OtherRows.Num())))
					.OnAreaExpansionChanged_Lambda([this, ModuleKey](bool bExpanded)
					{
						ModuleShowAllExpanded.FindOrAdd(ModuleKey) = bExpanded;
					})
					.BodyContent()[OtherBox]
				];
			}
		}
	}

	TSharedRef<SWidget> OnGenerateStringWidget(TSharedPtr<FString> Item)
	{
		return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : FString()));
	}

	FText GetEmitterText() const
	{
		return FText::FromString(SelectedEmitter.IsValid() ? *SelectedEmitter : TEXT("Select emitter"));
	}

	void OnEmitterChanged(TSharedPtr<FString> Item, ESelectInfo::Type)
	{
		SelectedEmitter = Item;
		RefreshInputRows();
	}

	void OnSetParamStageChanged(TSharedPtr<FString> Item, ESelectInfo::Type)
	{
		SelectedSetParamStage = Item;
	}

	FText GetSetParamStageText() const
	{
		return FText::FromString(SelectedSetParamStage.IsValid() ? *SelectedSetParamStage : TEXT("Emitter Spawn"));
	}

	void AppendLog(const FString& Line)
	{
		if (!LogBox.IsValid())
		{
			return;
		}
		LogBox->SetText(FText::FromString(LogBox->GetText().ToString() + Line + TEXT("\n")));
	}

	FReply OnExposeSelected()
	{
		UNiagaraSystem* System = WeakSystem.Get();
		if (!System || !SelectedEmitter.IsValid())
		{
			return FReply::Handled();
		}

		TArray<FNiagaraParamExposerExposeRequest> Requests;
		for (const TSharedPtr<FNiagaraParamExposerEditorInputRow>& Row : InputRows)
		{
			if (!Row.IsValid() || !Row->bSelected)
			{
				continue;
			}
			FNiagaraParamExposerExposeRequest Request;
			Request.EmitterName = *SelectedEmitter;
			Request.ModuleName = Row->ModuleName;
			Request.InputName = Row->InputName;
			Request.ScriptUsage = Row->ScriptUsage;
			Request.CustomUserParameterName = Row->ProposedUserName;
			Request.bAssignToEmitterCategory = bAssignEmitterCategory;
			Requests.Add(Request);
		}

		const TArray<FNiagaraParamExposerExposeResult> Results = NiagaraParamExposerCore::ExposeModuleInputsBatch(System, Requests);
		TSet<FString> ExposedNames;
		for (const FNiagaraParamExposerExposeResult& Result : Results)
		{
			if (Result.bSuccess)
			{
				ExposedNames.Add(Result.UserParameterName);
			}
			AppendLog(FString::Printf(
				TEXT("[%s] %s.%s -> %s : %s"),
				Result.bSuccess ? TEXT("OK") : TEXT("FAIL"),
				*Result.ModuleName,
				*Result.InputName,
				*Result.UserParameterName,
				*Result.Message));
		}

		for (const FNiagaraParamExposerUserParameterInfo& UserParam : NiagaraParamExposerCore::GetUserParameters(System))
		{
			if (ExposedNames.Contains(UserParam.ShortName))
			{
				AppendLog(FString::Printf(
					TEXT("  readback User.%s (%s) = %s"),
					*UserParam.ShortName,
					*UserParam.TypeName,
					*UserParam.ValueAsString));
			}
		}

		RefreshInputRows();
		return FReply::Handled();
	}

	FReply OnCreateParameter()
	{
		UNiagaraSystem* System = WeakSystem.Get();
		if (!System)
		{
			return FReply::Handled();
		}
        if (bAssignEmitterCategory && !SelectedEmitter.IsValid())
        {
            AppendLog(TEXT("Select an emitter to place the new parameter in its category."));
            return FReply::Handled();
        }
		const auto Result = NiagaraParamExposerCore::CreateCustomUserParameter(
			System,
			CreateNameBox->GetText().ToString(),
			*SelectedCreateType,
			CreateValueBox->GetText().ToString(),
			bAssignEmitterCategory ? *SelectedEmitter : CreateCategoryBox->GetText().ToString(),
			true);
		AppendLog(Result.Message);
		return FReply::Handled();
	}

	FReply OnAddSetParameterModule()
	{
		UNiagaraSystem* System = WeakSystem.Get();
		if (!System || !SelectedEmitter.IsValid())
		{
			return FReply::Handled();
		}
		const auto Result = NiagaraParamExposerCore::AddTypedSetParameterModule(
			System,
			*SelectedEmitter,
			LabelToStage(SelectedSetParamStage.IsValid() ? *SelectedSetParamStage : FString()),
			SetParamNameBox->GetText().ToString(),
			*SelectedSetParamType,
			SetParamValueBox->GetText().ToString(),
			SetParamLinkBox->GetText().ToString(),
			true, bAssignEmitterCategory);
		AppendLog(Result.Message);
		RefreshInputRows();
		return FReply::Handled();
	}

	FReply OnRenameEmitterClicked()
	{
		UNiagaraSystem* System = WeakSystem.Get();
		if (!System || !SelectedEmitter.IsValid())
		{
			return FReply::Handled();
		}
		const FString NewName = RenameEmitterNameBox->GetText().ToString();
		const auto Result = NiagaraParamExposerCore::RenameEmitter(System, *SelectedEmitter, NewName, true);
		AppendLog(Result.Message);
		RefreshEmitters();
		return FReply::Handled();
	}
};

class SHandyToolbox : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SHandyToolbox) {} SLATE_END_ARGS()
    void Construct(const FArguments&)
    {
        ChildSlot
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(6)
            [SNew(SBox).WidthOverride(155)
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(6,8)
                    [SNew(STextBlock).Text(LOCTEXT("ToolboxBrand", "HandyToolbox")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 13)).ColorAndOpacity(FLinearColor(.68f,.79f,.88f))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,4)
                    [MakeNavigation(0, LOCTEXT("ParametersNav", "Niagara Parameters"))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,4)
                    [MakeNavigation(1, LOCTEXT("CleanupNav", "Asset Cleanup"))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,4)
                    [MakeNavigation(2, LOCTEXT("ReferencesNav", "Reference Checker"))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,4)
                    [MakeNavigation(3, LOCTEXT("NamingNav", "Naming Convention"))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,4)
                    [MakeNavigation(4, LOCTEXT("PresentationNav", "Presentation"))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,4)
                    [MakeNavigation(5, LOCTEXT("PackSanityNav", "Pack Sanity Check"))]
                ]]
            + SHorizontalBox::Slot().FillWidth(1)
            [SAssignNew(Pages, SWidgetSwitcher)
                + SWidgetSwitcher::Slot()[SNew(SNiagaraParamExposerEditorTab)]
                + SWidgetSwitcher::Slot()[HandyAssetCleanup::MakePanel()]
                + SWidgetSwitcher::Slot()[HandyReferences::MakePanel()]
                + SWidgetSwitcher::Slot()[HandyNaming::MakePanel()]
                + SWidgetSwitcher::Slot()[HandyPresentation::MakePanel()]
                + SWidgetSwitcher::Slot()[HandySanity::MakePanel([this](int32 Index){Active=Index;Pages->SetActiveWidgetIndex(Index);})]
            ]
        ];
    }
private:
    TSharedPtr<SWidgetSwitcher> Pages;
    int32 Active = 0;
    TSharedRef<SWidget> MakeNavigation(int32 Index, FText Label)
    {
        return SNew(SButton).Text(Label).ContentPadding(FMargin(8,10))
            .ButtonColorAndOpacity_Lambda([this, Index]() { return Active == Index ? FLinearColor(.14f,.23f,.30f) : FLinearColor(.14f,.14f,.14f); })
            .OnClicked_Lambda([this, Index]() { Active = Index; Pages->SetActiveWidgetIndex(Index); return FReply::Handled(); });
    }
};
static TSharedRef<SDockTab> SpawnNiagaraParamExposerEditorTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SHandyToolbox)];
}

void NiagaraParamExposer_RegisterEditorTab()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		NiagaraParamExposerEditorTabId,
		FOnSpawnTab::CreateStatic(&SpawnNiagaraParamExposerEditorTab))
		.SetDisplayName(LOCTEXT("TabTitle", "HandyToolbox"))
		.SetMenuType(ETabSpawnerMenuType::Enabled);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		FToolMenuSection& Section = Menu->FindOrAddSection("NiagaraParamExposer");
		Section.AddMenuEntry(
			"OpenNiagaraParamExposer",
			LOCTEXT("MenuLabel", "HandyToolbox"),
			LOCTEXT("MenuTooltip", "Open HandyToolbox: Niagara parameters and asset cleanup."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				NiagaraParamExposer_OpenEditorTab(GNiagaraParamExposerEditorSystem.Get());
			})));
	}));
}

void NiagaraParamExposer_UnregisterEditorTab()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(NiagaraParamExposerEditorTabId);
}

void NiagaraParamExposer_OpenEditorTab(UNiagaraSystem* NiagaraSystem)
{
	if (NiagaraSystem)
	{
		GNiagaraParamExposerEditorSystem = NiagaraSystem;
	}
	FGlobalTabmanager::Get()->TryInvokeTab(NiagaraParamExposerEditorTabId);
}

#undef LOCTEXT_NAMESPACE
