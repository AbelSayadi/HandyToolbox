// Copyright NiagaraParamExposer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UNiagaraSystem;

class SNiagaraParamExposerPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraParamExposerPanel) {}
		SLATE_ARGUMENT(UNiagaraSystem*, NiagaraSystem)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TWeakObjectPtr<UNiagaraSystem> WeakSystem;

	TSharedPtr<class SComboBox<TSharedPtr<FString>>> EmitterCombo;
	TArray<TSharedPtr<FString>> EmitterOptions;
	TSharedPtr<FString> SelectedEmitter;

	TSharedPtr<class SListView<TSharedPtr<struct FNiagaraParamExposerInputRow>>> InputListView;
	TArray<TSharedPtr<struct FNiagaraParamExposerInputRow>> InputRows;

	FReply OnExposeClicked();
	void RefreshEmitters();
	void RefreshInputs();
	FText GetEmitterText() const;
	TSharedRef<SWidget> OnGenerateEmitterWidget(TSharedPtr<FString> InItem);
	void OnEmitterSelectionChanged(TSharedPtr<FString> InItem, ESelectInfo::Type SelectInfo);
};
