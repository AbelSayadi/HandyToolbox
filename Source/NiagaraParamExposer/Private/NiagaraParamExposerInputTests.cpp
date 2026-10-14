#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "NiagaraParamExposerTypedValue.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "ToolMenus.h"
#include "UObject/UObjectIterator.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "NiagaraNodeAssignment.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NiagaraParamExposerInputFilter.h"
#include "NiagaraParamExposerCore.h"
#include "NiagaraParamExposerDirect.h"
#include "NiagaraParamExposerUserHierarchy.h"
#include "NiagaraParamExposerNiagaraHelpers.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "EdGraph/EdGraphPin.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Text/STextBlock.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPESearchFilterTest, "NiagaraParamExposer.Inputs.SearchAndLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNPESearchFilterTest::RunTest(const FString& Parameters)
{
 using namespace NiagaraParamExposerInputFilter;
 TestTrue(TEXT("Emitter search ignores case and surrounding spaces"), MatchesEmitterSearch(TEXT("FireAuraFeet"), TEXT(" aura ")));
 TestTrue(TEXT("Empty emitter search shows all"), MatchesEmitterSearch(TEXT("FireAuraFeet"), TEXT("")));
 TestFalse(TEXT("No-match emitter search"), MatchesEmitterSearch(TEXT("FireAuraFeet"), TEXT("arms")));
 auto* Function = NewObject<UNiagaraNodeFunctionCall>();
 UEdGraphPin* Mode = Function->CreatePin(EGPD_Input, TEXT("int"), TEXT("Lifetime Mode"));
 TArray<FString> Names{TEXT("Lifetime"), TEXT("Lifetime Min"), TEXT("Lifetime Max")};
 Mode->DefaultValue = TEXT("Direct Set");
 TestFalse(TEXT("Direct Lifetime stays visible even with Min/Max metadata"), ShouldHideModuleInputForDisplay(*Function, TEXT("Lifetime"), TEXT("NiagaraFloat"), Names));
 TestTrue(TEXT("Direct mode hides inactive Min"), ShouldHideModuleInputForDisplay(*Function, TEXT("Lifetime Min"), TEXT("NiagaraFloat"), Names));
 Mode->DefaultValue = TEXT("Random Range");
 TestTrue(TEXT("Random mode hides direct input"), ShouldHideModuleInputForDisplay(*Function, TEXT("Lifetime"), TEXT("NiagaraFloat"), Names));
 TestFalse(TEXT("Random mode shows Min"), ShouldHideModuleInputForDisplay(*Function, TEXT("Lifetime Min"), TEXT("NiagaraFloat"), Names));
 Mode->DefaultValue = TEXT("Unknown");
 TestFalse(TEXT("Unknown mode does not guess input is inactive"), ShouldHideModuleInputForDisplay(*Function, TEXT("Lifetime"), TEXT("NiagaraFloat"), Names));
 auto* Pin = Function->CreatePin(EGPD_Input, TEXT("float"), TEXT("Literal"));
 Pin->DefaultValue = TEXT("1.0");
 FString Linked;
 ENiagaraParamExposerInputBindingMode Binding = ENiagaraParamExposerInputBindingMode::Default;
 TestFalse(TEXT("Decimal literal is not a parameter link"), NiagaraParamExposerNiagaraHelpers::TryReadLinkedParameterFromOverridePin(*Pin, Linked, Binding));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPEActualInputTest, "NiagaraParamExposer.Inputs.AssetIntegration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNPEActualInputTest::RunTest(const FString& Parameters)
{
 FString Content, Asset;
 if (!FParse::Value(FCommandLine::Get(), TEXT("NPETestContent="), Content) || !FParse::Value(FCommandLine::Get(), TEXT("NPETestAsset="), Asset))
 { AddInfo(TEXT("Supply NPETestContent and NPETestAsset to run integration against a transient asset copy.")); return true; }
 FPackageName::RegisterMountPoint(TEXT("/Game/"), Content);
 UNiagaraSystem* Original = LoadObject<UNiagaraSystem>(nullptr, *Asset);
 if (!TestNotNull(TEXT("Source system loaded read-only"), Original)) return false;
 UNiagaraSystem* System = DuplicateObject<UNiagaraSystem>(Original, GetTransientPackage());
 ENiagaraParamExposerScriptUsage Usage = ENiagaraParamExposerScriptUsage::ParticleSpawn;
 auto* Init = NiagaraParamExposerCore::FindModuleFunctionCall(System, TEXT("FireAuraFeet"), TEXT("Initialize Particle"), Usage, Usage);
 if (!TestNotNull(TEXT("Point Attributes module found"), Init)) return false;
 for (const UEdGraphPin* Pin : Init->Pins)
  if (Pin && Pin->PinName.ToString().Contains(TEXT("Lifetime")))
   AddInfo(FString::Printf(TEXT("MODE PIN: %s = %s (type %s)"), *Pin->PinName.ToString(), *Pin->DefaultValue, *GetNameSafe(Pin->PinType.PinSubCategoryObject.Get())));
 TestFalse(TEXT("Actual Point Attributes lifetime visible"), NiagaraParamExposerInputFilter::ShouldHideModuleInputForDisplay(*Init, TEXT("Lifetime"), TEXT("NiagaraFloat"), {TEXT("Lifetime"), TEXT("Lifetime Min"), TEXT("Lifetime Max")}));
 FNiagaraParamExposerExposeRequest Request;
 Request.EmitterName = TEXT("FireAuraFeet"); Request.ModuleName = TEXT("Particle State"); Request.InputName = TEXT("Lifetime"); Request.ScriptUsage = ENiagaraParamExposerScriptUsage::ParticleUpdate;
 TestFalse(TEXT("Wrong Lifetime source rejected"), NiagaraParamExposerCore::ExposeModuleInput(System, Request, false).bSuccess);
 Request.ModuleName = TEXT("Initialize Particle"); Request.ScriptUsage = ENiagaraParamExposerScriptUsage::ParticleSpawn;
 auto Exposed = NiagaraParamExposerCore::ExposeModuleInput(System, Request, false);
 TestTrue(*Exposed.Message, Exposed.bSuccess);
 const auto Restored = NiagaraParamExposerCore::LinkModuleInputToParameter(System, TEXT("FireAuraFeet"), TEXT("Particle State"), TEXT("Lifetime"), ENiagaraParamExposerScriptUsage::ParticleUpdate, TEXT("Particles.Lifetime"), false);
 TestTrue(*Restored.Message, Restored.bSuccess);
 auto Verify = [&](const FString& Module, ENiagaraParamExposerScriptUsage Stage, const FString& Expected)
 {
  bool bFound = false;
  for (const auto& Info : NiagaraParamExposerCore::ListModuleInputs(System, TEXT("FireAuraFeet"), Module, Stage))
   if (Info.InputName == TEXT("Lifetime")) { bFound = true; TestEqual(TEXT("Correct Lifetime graph link"), Info.LinkedParameterName, Expected); }
  TestTrue(TEXT("Lifetime input found"), bFound);
 };
 Verify(TEXT("Initialize Particle"), ENiagaraParamExposerScriptUsage::ParticleSpawn, TEXT("User.FireAuraFeet_Lifetime"));
 Verify(TEXT("Particle State"), ENiagaraParamExposerScriptUsage::ParticleUpdate, TEXT("Particles.Lifetime"));
 FNiagaraVariable Lifetime(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.FireAuraFeet_Lifetime"));
 TestEqual(TEXT("Point Attributes value copied, not Particle State zero"), System->GetExposedParameters().GetParameterValue<float>(Lifetime), 1.f);
 System->WaitForCompilationComplete();
 auto VM = MakeShared<FNiagaraSystemViewModel>();
 FNiagaraSystemViewModelOptions Options; Options.bCanAutoCompile = false; Options.bCanSimulate = false; Options.bIsForDataProcessingOnly = true; Options.MessageLogGuid = FGuid::NewGuid();
 VM->Initialize(*System, Options);
 UNiagaraStackFunctionInput* LocalInput = nullptr;
 UNiagaraStackFunctionInput* LifetimeInput = nullptr;
 for (const auto& Emitter : VM->GetEmitterHandleViewModels())
 {
  if (Emitter->GetEmitterHandle()->GetName() != TEXT("FireAuraFeet")) continue;
  TArray<UNiagaraStackFunctionInput*> Inputs;
  Emitter->GetEmitterStackViewModel()->GetRootEntry()->GetFilteredChildrenOfType<UNiagaraStackFunctionInput>(Inputs, true);
  for (auto* Input : Inputs)
  {
   const FString Name = Input->GetInputParameterHandle().GetName().ToString();
   if (Name == TEXT("Lifetime") && !NiagaraParamExposerInputFilter::IsParticleStateLifetime(Input->GetInputFunctionCallNode(), Name)) LifetimeInput = Input;
   if (!LocalInput && Input->GetInputType() == FNiagaraTypeDefinition::GetFloatDef() && !Input->IsStaticParameter() && Input->GetIsEnabled() && (!Input->GetHasEditCondition() || Input->GetEditConditionEnabled()) && Input->GetValueMode() == UNiagaraStackFunctionInput::EValueMode::Local && Name != TEXT("Lifetime"))
    LocalInput = Input;
  }
 }
 if (TestNotNull(TEXT("Native Lifetime row found"), LifetimeInput))
 {
  const auto Again = NiagaraParamExposerDirect::ExposeInput(*LifetimeInput);
  TestTrue(*Again.Message, Again.bSuccess);
 }
 if (TestNotNull(TEXT("Native local input found"), LocalInput))
 {
  TArray<UNiagaraStackEntry*> TreeItems{LocalInput};
  auto Tree = SNew(STreeView<UNiagaraStackEntry*>)
   .TreeItemsSource(&TreeItems)
   .OnGenerateRow_Lambda([](UNiagaraStackEntry* Entry, const TSharedRef<STableViewBase>& Owner)
    { return SNew(STableRow<UNiagaraStackEntry*>, Owner)[SNew(STextBlock).Text(Entry->GetDisplayName())]; })
   .OnGetChildren_Lambda([](UNiagaraStackEntry*, TArray<UNiagaraStackEntry*>&) {});
  Tree->SetItemSelection(LocalInput, true);
  TArray<TWeakObjectPtr<UNiagaraStackFunctionInput>> Selected;
  NiagaraParamExposerDirect::CollectStackSelection(Tree, System, Selected);
  TestEqual(TEXT("Native detail row selection reaches direct exposure"), Selected.Num(), 1);
  Selected.Reset();
  NiagaraParamExposerDirect::CollectStackSelection(Tree, Original, Selected);
  TestEqual(TEXT("Other system selections excluded"), Selected.Num(), 0);
  const auto Direct = NiagaraParamExposerDirect::ExposeInput(*LocalInput);
  TestTrue(*Direct.Message, Direct.bSuccess);
  if (Direct.bSuccess) TestTrue(TEXT("Native input now linked"), LocalInput->GetValueMode() == UNiagaraStackFunctionInput::EValueMode::Linked);
 }

 // Exercise typed module creation on the transient asset, including actual exposed bytes.
 for (const auto& Pair : TArray<TPair<FString,FString>>{{TEXT("Float"),TEXT("2.5")},{TEXT("Vector2D"),TEXT("2,3")},{TEXT("Vector"),TEXT("2,3,4")},{TEXT("Color"),TEXT("0.2,0.4,0.6,0.8")},{TEXT("Int"),TEXT("7")},{TEXT("Bool"),TEXT("true")}})
 {
  const FString Name = TEXT("NPETest") + Pair.Key;
  FNiagaraVariable Expected; FString Default;
  TestTrue(TEXT("Typed parser"), NiagaraParamExposerTypedValue::Parse(Pair.Key, Pair.Value, Expected, Default));
  auto Added = NiagaraParamExposerCore::AddTypedSetParameterModule(System, TEXT("FireAuraFeet"), ENiagaraParamExposerScriptUsage::EmitterSpawn, TEXT("Emitter.")+Name, Pair.Key, Pair.Value, TEXT("auto"), false);
  TestTrue(*Added.Message, Added.bSuccess);
  FNiagaraVariable Lookup(Expected.GetType(), FName(*(TEXT("User.FireAuraFeet_")+Name)));
  const uint8* Data = System->GetExposedParameters().GetParameterData(Lookup);
  if (TestNotNull(TEXT("Typed user parameter exists"), Data)) TestTrue(TEXT("All typed default components preserved"), FMemory::Memcmp(Data, Expected.GetData(), Expected.GetSizeInBytes())==0);
  auto Collision = NiagaraParamExposerCore::AddTypedSetParameterModule(System, TEXT("FireAuraFeet"), ENiagaraParamExposerScriptUsage::EmitterSpawn, TEXT("Emitter.")+Name, TEXT("Float"), TEXT("99"), TEXT("auto"), false);
  TestFalse(TEXT("Existing user parameter is not overwritten"), Collision.bSuccess);
 }
 TestFalse(TEXT("Malformed vector rejected"), NiagaraParamExposerCore::AddTypedSetParameterModule(System, TEXT("FireAuraFeet"), ENiagaraParamExposerScriptUsage::EmitterSpawn, TEXT("Emitter.BadVector"), TEXT("Vector"), TEXT("1,2"), TEXT("auto"), false).bSuccess);
 System->WaitForCompilationComplete();
 return true;
}

// This creates the real asset editor, including its native child window and stack.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPENativeWindowTest, "NiagaraParamExposer.Inputs.NativeEditor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNPENativeWindowTest::RunTest(const FString& Parameters)
{
 FString Content, Asset;
 if (!FParse::Value(FCommandLine::Get(), TEXT("NPETestContent="), Content) || !FParse::Value(FCommandLine::Get(), TEXT("NPETestAsset="), Asset)) return true;
 FPackageName::RegisterMountPoint(TEXT("/Game/"), Content);
 auto* Original = LoadObject<UNiagaraSystem>(nullptr, *Asset);
 if (!TestNotNull(TEXT("Asset loaded"), Original)) return false;
 auto* System = DuplicateObject<UNiagaraSystem>(Original, GetTransientPackage(), TEXT("NPENativeUITest"));
 auto* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
 if (!TestTrue(TEXT("Real Niagara editor opened"), Editors->OpenEditorForAsset(System))) return false;
 // Select the module in the actual overview first, as a user does before its attributes appear.
 for (TObjectIterator<UNiagaraStackModuleItem> It; It; ++It)
 {
  if (It->IsFinalized()) continue;
  auto VM = It->GetSystemViewModelPtr();
  if (VM.IsValid() && &VM->GetSystem()==System && It->GetDisplayName().ToString().Contains(TEXT("Initialize Particle")))
  { VM->GetSelectionViewModel()->UpdateSelectedEntries({*It},{},true); break; }
 }
 FSlateApplication::Get().Tick();
 int32 StackCount = 0;
 UNiagaraStackFunctionInput* Selected = nullptr;
 TFunction<void(TSharedRef<SWidget>,bool)> Visit = [&](TSharedRef<SWidget> Widget, bool InStack)
 {
  InStack |= Widget->GetType()==TEXT("SNiagaraStack");
  if (InStack && Widget->GetType()==TEXT("STreeView<UNiagaraStackEntry*>"))
  {
   ++StackCount;
   auto Tree = StaticCastSharedRef<STreeView<UNiagaraStackEntry*>>(Widget);
   for (auto* Root : Tree->GetRootItems())
   {
    TArray<UNiagaraStackFunctionInput*> Inputs;
    Root->GetFilteredChildrenOfType<UNiagaraStackFunctionInput>(Inputs, true);
    for (auto* Input : Inputs)
    {
     if (Input->GetSystemViewModelPtr().IsValid() && &Input->GetSystemViewModelPtr()->GetSystem()==System && !Input->IsStaticParameter())
     { Tree->SetItemSelection(Input,true); Selected=Input; return; }
    }
   }
  }
  auto* Children=Widget->GetChildren();
  for(int32 I=0;I<Children->Num();++I) Visit(Children->GetChildAt(I),InStack);
 };
 TFunction<void(TSharedRef<SWindow>)> VisitWindow = [&](TSharedRef<SWindow> Window)
 { Visit(Window,false); for (auto Child : Window->GetChildWindows()) VisitWindow(Child); };
 for (auto Window : FSlateApplication::Get().GetTopLevelWindows()) VisitWindow(Window);
 TestTrue(TEXT("Native Niagara stack exists"), StackCount>0);
 TestNotNull(TEXT("Actual native input selected"), Selected);
 TArray<TWeakObjectPtr<UNiagaraStackFunctionInput>> Found;
 for (auto Window : FSlateApplication::Get().GetTopLevelWindows()) NiagaraParamExposerDirect::CollectWindowSelection(Window,System,Found);
 TestTrue(TEXT("Selection reachable through actual editor window hierarchy"), Selected && Found.Contains(Selected));
 auto* Toolbar = UToolMenus::Get()->FindMenu(TEXT("AssetEditor.Niagara.ToolBar"));
 TestTrue(TEXT("Expose command registered in actual Niagara toolbar"), Toolbar && Toolbar->FindSection(TEXT("NPEExpose")));
 Editors->CloseAllEditorsForAsset(System);
 System->WaitForCompilationComplete();
 return true;
}
#endif
