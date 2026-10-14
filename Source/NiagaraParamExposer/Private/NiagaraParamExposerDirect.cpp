#include "NiagaraParamExposerDirect.h"
#include "NiagaraParamExposerNaming.h"
#include "NiagaraParamExposerInputFilter.h"
#include "NiagaraParamExposerUserHierarchy.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "NiagaraSystemEditorData.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraEditorModule.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Styling/AppStyle.h"
#include "ScopedTransaction.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/SWindow.h"
#include "ToolMenus.h"
#define LOCTEXT_NAMESPACE "NPEDirect"
#include "UObject/UObjectIterator.h"
#include "UObject/StructOnScope.h"

namespace NiagaraParamExposerDirect
{
static FDelegateHandle ExtenderHandle;
class FNPEDirectCommands : public TCommands<FNPEDirectCommands>
{
public:
 FNPEDirectCommands() : TCommands<FNPEDirectCommands>(TEXT("NiagaraParamExposerDirect"), NSLOCTEXT("NPE", "Commands", "Niagara Param Exposer"), NAME_None, FAppStyle::GetAppStyleSetName()) {}
 virtual void RegisterCommands() override
 {
  UI_COMMAND(Expose, "Expose Selected Input", "Expose the selected Niagara value with automatic naming and emitter grouping.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Alt, EKeys::E));
 }
 TSharedPtr<FUICommandInfo> Expose;
};

FNiagaraParamExposerOperationResult ExposeInput(UNiagaraStackFunctionInput& Input)
{
 FNiagaraParamExposerOperationResult Result;
 auto Fail = [&](const FString& Message) { Result.Message = Message; return Result; };
 if (Input.IsFinalized() || !Input.GetIsEnabled() || (Input.GetHasEditCondition() && !Input.GetEditConditionEnabled()) || Input.IsStaticParameter())
  return Fail(TEXT("Select an editable value input, not a mode switch."));
 const auto SystemVM = Input.GetSystemViewModelPtr();
 const auto EmitterVM = Input.GetEmitterViewModel();
 if (!SystemVM.IsValid() || !EmitterVM.IsValid()) return Fail(TEXT("Select an emitter input in a Niagara System."));
 UNiagaraSystem& System = SystemVM->GetSystem();
 FString EmitterName;
 for (const FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
  if (Handle.GetInstance().Emitter == EmitterVM->GetEmitter().Emitter) EmitterName = Handle.GetName().ToString();
 if (EmitterName.IsEmpty()) return Fail(TEXT("Could not identify the selected input's emitter."));
 const FString InputName = Input.GetInputParameterHandle().GetName().ToString();
 if (NiagaraParamExposerInputFilter::IsParticleStateLifetime(Input.GetInputFunctionCallNode(), InputName))
  return Fail(TEXT("Select Initialize Particle > Point Attributes > Lifetime; Particle State reads Particles.Lifetime."));
 const FString ShortName = NiagaraParamExposerNaming::MakeAutoUserParameterName(EmitterName, InputName);
 FNiagaraVariable Variable(Input.GetInputType(), FName(*(TEXT("User.") + ShortName)));
 UNiagaraSystemEditorData* Data = Cast<UNiagaraSystemEditorData>(System.GetEditorData());
 if (!Data) return Fail(TEXT("Niagara system editor data is unavailable."));
 const bool bAlreadyLinked = Input.GetValueMode() == UNiagaraStackFunctionInput::EValueMode::Linked &&
  Input.GetLinkedParameterValue() == FNiagaraVariableBase(Variable);
 if (!bAlreadyLinked)
 {
  if (Input.GetValueMode() != UNiagaraStackFunctionInput::EValueMode::Local)
   return Fail(TEXT("This input is driven by a link or dynamic expression. Select a local value inside it to expose without replacing its behavior."));
  TArray<FNiagaraVariable> Existing;
  System.GetExposedParameters().GetUserParameters(Existing);
  for (const FNiagaraVariable& Parameter : Existing)
   if (Parameter.GetName() == FName(*ShortName)) return Fail(TEXT("User.") + ShortName + TEXT(" already exists. Use the plugin panel to choose a different name or link the existing parameter."));
  const auto Value = Input.GetLocalValueStruct();
  if (!Value.IsValid() || !Value->GetStructMemory() || Value->GetStruct() != Input.GetInputType().GetScriptStruct())
   return Fail(TEXT("The selected input has no supported local value."));
  Variable.AllocateData();
  FMemory::Memcpy(Variable.GetData(), Value->GetStructMemory(), Variable.GetSizeInBytes());
 }
 FScopedTransaction Transaction(NSLOCTEXT("NiagaraParamExposer", "DirectExpose", "Expose Selected Niagara Input"));
 System.Modify();
 Data->Modify();
 NiagaraParamExposerUserHierarchy::PruneStaleUserParameterHierarchy(System);
 if (!bAlreadyLinked)
 {
  if (!System.GetExposedParameters().SetParameterData(Variable.GetData(), Variable, true))
   return Fail(TEXT("Could not create the user parameter."));
  if (UNiagaraScriptVariable* ScriptVariable = Data->FindOrAddUserScriptVariable(Variable, System))
  {
   ScriptVariable->Modify();
   ScriptVariable->Variable = Variable;
  }
  Input.SetLinkedParameterValue(Variable);
  System.GetExposedParameters().SetParameterData(Variable.GetData(), Variable);
 }
 FString Error;
 Result.bSuccess = NiagaraParamExposerUserHierarchy::RepairEmitterCategories(System, Error);
 System.MarkPackageDirty();
 Result.Message = Result.bSuccess ? TEXT("Exposed User.") + ShortName + TEXT(" and grouped it by emitter. Save when ready.") : Error;
 return Result;
}

// UE 5.6's detail stack keeps row selection in its Slate tree, separately from
// the public system selection model. Use public Slate APIs only, within the
// known SNiagaraStack widget; never cast unrelated editor tree views.
void CollectStackSelection(const TSharedRef<SWidget>& Widget, UNiagaraSystem* System, TArray<TWeakObjectPtr<UNiagaraStackFunctionInput>>& Out)
{
 if (!Widget->GetVisibility().IsVisible()) return;
 if (Widget->GetType() == TEXT("STreeView<UNiagaraStackEntry*>"))
 {
  const auto Tree = StaticCastSharedRef<STreeView<UNiagaraStackEntry*>>(Widget);
  for (UNiagaraStackEntry* Entry : Tree->GetSelectedItems())
  {
   auto* Input = Cast<UNiagaraStackFunctionInput>(Entry);
   if (!Input || Input->IsFinalized()) continue;
   const auto VM = Input->GetSystemViewModelPtr();
   if (VM.IsValid() && &VM->GetSystem() == System) Out.AddUnique(Input);
  }
  return;
 }
 FChildren* Children = Widget->GetChildren();
 for (int32 Index = 0; Index < Children->Num(); ++Index) CollectStackSelection(Children->GetChildAt(Index), System, Out);
}

void FindDetailStacks(const TSharedRef<SWidget>& Widget, UNiagaraSystem* System, TArray<TWeakObjectPtr<UNiagaraStackFunctionInput>>& Out)
{
 if (!Widget->GetVisibility().IsVisible()) return;
 if (Widget->GetType() == TEXT("SNiagaraStack")) { CollectStackSelection(Widget, System, Out); return; }
 FChildren* Children = Widget->GetChildren();
 for (int32 Index = 0; Index < Children->Num(); ++Index) FindDetailStacks(Children->GetChildAt(Index), System, Out);
}

void CollectWindowSelection(const TSharedRef<SWindow>& Window, UNiagaraSystem* System, TArray<TWeakObjectPtr<UNiagaraStackFunctionInput>>& Out)
{
 FindDetailStacks(Window, System, Out);
 // Floating asset editors are native child windows, not children in the widget tree.
 for (const auto& Child : Window->GetChildWindows()) CollectWindowSelection(Child, System, Out);
}

static TArray<TWeakObjectPtr<UNiagaraStackFunctionInput>> SelectedInputs(UNiagaraSystem* System)
{
 TArray<TWeakObjectPtr<UNiagaraStackFunctionInput>> Result;
 if (!System) return Result;
 if (FSlateApplication::IsInitialized())
  for (const auto& Window : FSlateApplication::Get().GetTopLevelWindows()) CollectWindowSelection(Window, System, Result);
 // Prefer detail-stack selection; overview selection can still contain an older row.
 if (!Result.IsEmpty()) return Result;
 for (TObjectIterator<UNiagaraSystemSelectionViewModel> It; It; ++It)
 {
  if (It->HasAnyFlags(RF_ClassDefaultObject)) continue;
  TArray<UNiagaraStackEntry*> Entries;
  It->GetSelectedEntries(Entries);
  for (UNiagaraStackEntry* Entry : Entries)
  {
   auto* Input = Cast<UNiagaraStackFunctionInput>(Entry);
   if (!Input || Input->IsFinalized()) continue;
   const auto VM = Input->GetSystemViewModelPtr();
   if (VM.IsValid() && &VM->GetSystem() == System) Result.AddUnique(Input);
  }
 }
 return Result;
}

static void ExposeSelected(TWeakObjectPtr<UNiagaraSystem> System)
{
 const auto Inputs = SelectedInputs(System.Get());
 FString Message;
 if (Inputs.IsEmpty()) Message = TEXT("No value row selected. Click an attribute label in the Selection panel (not a module header), then press Ctrl+Alt+E.");
 for (const auto& Input : Inputs)
 {
  if (Input.IsValid() && !Input->IsFinalized())
  {
   const auto Result = ExposeInput(*Input);
   Message += Result.Message + TEXT("\n");
  }
 }
 FNotificationInfo Info(FText::FromString(Message.TrimEnd()));
 Info.ExpireDuration = 7.f;
 FSlateNotificationManager::Get().AddNotification(Info);
}

void Register()
{
 FNPEDirectCommands::Register();
 // Register explicitly in ToolMenus so the command is visible near the start of
 // Niagara's toolbar and in its Asset menu, independent of legacy extender order.
 UToolMenus* Menus = UToolMenus::Get();
 FToolMenuOwnerScoped Owner(TEXT("NiagaraParamExposerDirect"));
 auto& Toolbar = Menus->ExtendMenu(TEXT("AssetEditor.Niagara.ToolBar"))->AddSection(
  TEXT("NPEExpose"), FText::GetEmpty(), FToolMenuInsert(TEXT("Asset"), EToolMenuInsertType::Before));
 Toolbar.AddEntry(FToolMenuEntry::InitToolBarButton(FNPEDirectCommands::Get().Expose));
 Menus->ExtendMenu(TEXT("AssetEditor.Niagara.MainMenu.Asset"))->AddSection(TEXT("NPEExpose"), LOCTEXT("NPE", "Parameters"))
  .AddMenuEntry(FNPEDirectCommands::Get().Expose);
 auto& Module = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
 auto Delegate = FAssetEditorExtender::CreateLambda([](const TSharedRef<FUICommandList> Commands, const TArray<UObject*> Objects)
 {
  auto Extender = MakeShared<FExtender>();
  for (UObject* Object : Objects)
  {
   if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Object))
   {
    TWeakObjectPtr<UNiagaraSystem> WeakSystem(System);
    Commands->MapAction(FNPEDirectCommands::Get().Expose, FExecuteAction::CreateStatic(&ExposeSelected, WeakSystem));
    break;
   }
  }
  return Extender;
 });
 ExtenderHandle = Delegate.GetHandle();
 Module.GetToolBarExtensibilityManager()->GetExtenderDelegates().Add(Delegate);
}

void Unregister()
{
 if (auto* Module = FModuleManager::GetModulePtr<FNiagaraEditorModule>(TEXT("NiagaraEditor")))
  Module->GetToolBarExtensibilityManager()->GetExtenderDelegates().RemoveAll([](const FAssetEditorExtender& Delegate)
   { return Delegate.GetHandle() == ExtenderHandle; });
 ExtenderHandle.Reset();
 UToolMenus::UnregisterOwner(TEXT("NiagaraParamExposerDirect"));
 FNPEDirectCommands::Unregister();
}
}

#undef LOCTEXT_NAMESPACE
