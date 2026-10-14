// Copyright NiagaraParamExposer. All Rights Reserved.

#include "NiagaraParamExposerSlate.h"

#include "NiagaraParamExposerCore.h"
#include "NiagaraParamExposerTypes.h"
#include "NiagaraSystem.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

struct FNiagaraParamExposerInputRow
{
	bool bSelected = true;
	FString ModuleName;
	FString InputName;
	FString DefaultUserName;
	FString CustomUserName;
	ENiagaraParamExposerScriptUsage ScriptUsage = ENiagaraParamExposerScriptUsage::Any;
};

void SNiagaraParamExposerPanel::Construct(const FArguments& InArgs)
{
	WeakSystem = InArgs._NiagaraSystem;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("Niagara Param Exposer")))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SAssignNew(EmitterCombo, SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&EmitterOptions)
			.OnGenerateWidget(this, &SNiagaraParamExposerPanel::OnGenerateEmitterWidget)
			.OnSelectionChanged(this, &SNiagaraParamExposerPanel::OnEmitterSelectionChanged)
			[
				SNew(STextBlock).Text(this, &SNiagaraParamExposerPanel::GetEmitterText)
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(4)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(InputListView, SListView<TSharedPtr<FNiagaraParamExposerInputRow>>)
				.ListItemsSource(&InputRows)
				.OnGenerateRow_Lambda([](TSharedPtr<FNiagaraParamExposerInputRow> Item, const TSharedRef<STableViewBase>& OwnerTable)
				{
					return SNew(STableRow<TSharedPtr<FNiagaraParamExposerInputRow>>, OwnerTable)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(2)
						[
							SNew(SCheckBox)
							.IsChecked_Lambda([Item]() { return Item->bSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
							.OnCheckStateChanged_Lambda([Item](ECheckBoxState State)
							{
								Item->bSelected = State == ECheckBoxState::Checked;
							})
						]
						+ SHorizontalBox::Slot().FillWidth(0.35f).Padding(2)
						[
							SNew(STextBlock).Text(FText::FromString(Item->ModuleName))
						]
						+ SHorizontalBox::Slot().FillWidth(0.25f).Padding(2)
						[
							SNew(STextBlock).Text(FText::FromString(Item->InputName))
						]
						+ SHorizontalBox::Slot().FillWidth(0.4f).Padding(2)
						[
							SNew(SEditableTextBox)
							.Text(FText::FromString(Item->CustomUserName.IsEmpty() ? Item->DefaultUserName : Item->CustomUserName))
							.OnTextCommitted_Lambda([Item](const FText& Text, ETextCommit::Type)
							{
								Item->CustomUserName = Text.ToString();
							})
						]
					];
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Expose Selected")))
			.OnClicked(this, &SNiagaraParamExposerPanel::OnExposeClicked)
		]
	];

	RefreshEmitters();
}

void SNiagaraParamExposerPanel::RefreshEmitters()
{
	EmitterOptions.Reset();
	SelectedEmitter.Reset();

	if (UNiagaraSystem* System = WeakSystem.Get())
	{
		for (const FNiagaraParamExposerEmitterInfo& Emitter : NiagaraParamExposerCore::ListEmitters(System))
		{
			EmitterOptions.Add(MakeShared<FString>(Emitter.EmitterName));
		}
	}

	if (EmitterOptions.Num() > 0)
	{
		SelectedEmitter = EmitterOptions[0];
		EmitterCombo->SetSelectedItem(SelectedEmitter);
	}

	RefreshInputs();
}

void SNiagaraParamExposerPanel::RefreshInputs()
{
	InputRows.Reset();

	UNiagaraSystem* System = WeakSystem.Get();
	if (!System || !SelectedEmitter.IsValid())
	{
		if (InputListView.IsValid())
		{
			InputListView->RequestListRefresh();
		}
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

		for (const FNiagaraParamExposerModuleInputInfo& Input : Inputs)
		{
			TSharedPtr<FNiagaraParamExposerInputRow> Row = MakeShared<FNiagaraParamExposerInputRow>();
			Row->ModuleName = Module.ModuleName;
			Row->InputName = Input.InputName;
			Row->ScriptUsage = Module.ScriptUsage;
			Row->DefaultUserName = FString::Printf(TEXT("%s_%s"), *(*SelectedEmitter), *Input.InputName);
			Row->CustomUserName = Row->DefaultUserName;
			InputRows.Add(Row);
		}
	}

	if (InputListView.IsValid())
	{
		InputListView->RequestListRefresh();
	}
}

FText SNiagaraParamExposerPanel::GetEmitterText() const
{
	return FText::FromString(SelectedEmitter.IsValid() ? *SelectedEmitter : TEXT("Select emitter"));
}

TSharedRef<SWidget> SNiagaraParamExposerPanel::OnGenerateEmitterWidget(TSharedPtr<FString> InItem)
{
	return SNew(STextBlock).Text(FText::FromString(InItem.IsValid() ? *InItem : FString()));
}

void SNiagaraParamExposerPanel::OnEmitterSelectionChanged(TSharedPtr<FString> InItem, ESelectInfo::Type SelectInfo)
{
	SelectedEmitter = InItem;
	RefreshInputs();
}

FReply SNiagaraParamExposerPanel::OnExposeClicked()
{
	UNiagaraSystem* System = WeakSystem.Get();
	if (!System || !SelectedEmitter.IsValid())
	{
		return FReply::Handled();
	}

	TArray<FNiagaraParamExposerExposeRequest> Requests;
	for (const TSharedPtr<FNiagaraParamExposerInputRow>& Row : InputRows)
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
		Request.CustomUserParameterName = Row->CustomUserName;
		Requests.Add(Request);
	}

	NiagaraParamExposerCore::ExposeModuleInputsBatch(System, Requests);
	RefreshInputs();
	return FReply::Handled();
}
