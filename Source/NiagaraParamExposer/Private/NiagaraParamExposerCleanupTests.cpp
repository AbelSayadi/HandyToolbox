#include "NiagaraParamExposerCleanup.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "NiagaraParamExposerCore.h"
#include "NiagaraParamExposerUserHierarchy.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "Editor.h"
#include "ScopedTransaction.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPECleanupTest, "NiagaraParamExposer.Cleanup.Regression", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNPECleanupTest::RunTest(const FString& Parameters)
{
    auto* System = NewObject<UNiagaraSystem>(GetTransientPackage(), NAME_None, RF_Transactional);
    UNiagaraSystemFactoryNew::InitializeSystem(System, true);
    auto Add = [&](const TCHAR* Name, float Value)
    {
        FNiagaraVariable Var(FNiagaraTypeDefinition::GetFloatDef(), FName(Name));
        Var.SetValue(Value);
        System->GetExposedParameters().AddParameter(Var);
        System->GetExposedParameters().SetParameterValue(Value, Var);
        return Var;
    };
    auto KeepZ = Add(TEXT("User.Shared_Z"), 7.25f);
    auto Orphan = Add(TEXT("User.DeletedEmitter_Color"), 4.5f);
    auto KeepA = Add(TEXT("User.Shared_A"), 2.25f);
    auto* Source = Cast<UNiagaraScriptSource>(System->GetSystemSpawnScript()->GetLatestSource());
    if (!TestNotNull(TEXT("System source"), Source) || !TestNotNull(TEXT("Graph"), Source->NodeGraph.Get())) return false;
    auto* Node = NewObject<UNiagaraNodeFunctionCall>(Source->NodeGraph);
    Source->NodeGraph->AddNode(Node);
    Node->CreatePin(EGPD_Output, TEXT("float"), KeepZ.GetName());
    Node->CreatePin(EGPD_Output, TEXT("float"), KeepA.GetName());
    FString Error;
    using namespace NiagaraParamExposerUserHierarchy;
    TestTrue(TEXT("Category setup"), AssignUserParametersToCategory(*System, TEXT("CustomGroup"), {TEXT("Shared_Z"), TEXT("DeletedEmitter_Color"), TEXT("Shared_A")}, Error));
    auto Before = GetCategoryAssignments(*System);
    auto Expected = Before;
    Expected.RemoveAll([](const FString& Row) { return Row.StartsWith(TEXT("DeletedEmitter_Color|")); });
    auto Preview=NiagaraParamExposerCleanup::Cleanup(System,true);
    TestTrue(TEXT("Preview succeeds without mutation"),Preview.bSuccess && System->GetExposedParameters().IndexOf(Orphan)!=INDEX_NONE && GetCategoryAssignments(*System)==Before);
    auto Result = NiagaraParamExposerCleanup::Cleanup(System);
    TestTrue(*Result.Message, Result.bSuccess);
    TestFalse(TEXT("Orphan removed"), (System->GetExposedParameters().IndexOf(Orphan) != INDEX_NONE));
    TestTrue(TEXT("System graph reference retained"), (System->GetExposedParameters().IndexOf(KeepZ) != INDEX_NONE));
    TestTrue(TEXT("Other reference retained"), (System->GetExposedParameters().IndexOf(KeepA) != INDEX_NONE));
    TestTrue(TEXT("Exact surviving hierarchy order preserved"), GetCategoryAssignments(*System) == Expected);
    GEditor->UndoTransaction();
    TestTrue(TEXT("Undo restores parameter"), (System->GetExposedParameters().IndexOf(Orphan) != INDEX_NONE));
    TestEqual(TEXT("Undo restores value"), System->GetExposedParameters().GetParameterValue<float>(Orphan), 4.5f);
    TestTrue(TEXT("Undo restores exact hierarchy"), GetCategoryAssignments(*System) == Before);
    GEditor->RedoTransaction();
    TestFalse(TEXT("Redo removes orphan again"), (System->GetExposedParameters().IndexOf(Orphan) != INDEX_NONE));
    TestTrue(TEXT("Redo preserves surviving hierarchy"), GetCategoryAssignments(*System) == Expected);
    TestTrue(TEXT("Repeated cleanup succeeds"), NiagaraParamExposerCleanup::Cleanup(System).bSuccess);
    TestTrue(TEXT("Repeated cleanup does not change order"), GetCategoryAssignments(*System) == Expected);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandyBatchCleanupTest,"NiagaraParamExposer.Cleanup.BatchUndo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHandyBatchCleanupTest::RunTest(const FString&){
    TArray<UNiagaraSystem*> Systems;FNiagaraVariable Var(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.Deleted_Orphan"));
    for(int32 I=0;I<2;++I){auto* S=NewObject<UNiagaraSystem>(GetTransientPackage(),NAME_None,RF_Transactional);UNiagaraSystemFactoryNew::InitializeSystem(S,true);S->GetExposedParameters().AddParameter(Var);Systems.Add(S);}
    {FScopedTransaction Transaction(NSLOCTEXT("HandyToolbox","TestBatch","Batch cleanup test"));for(auto* S:Systems)NiagaraParamExposerCleanup::Cleanup(S);}
    for(auto* S:Systems)TestTrue(TEXT("Batch removed orphan"),S->GetExposedParameters().IndexOf(Var)==INDEX_NONE);
    GEditor->UndoTransaction();for(auto* S:Systems)TestTrue(TEXT("One undo restores entire batch"),S->GetExposedParameters().IndexOf(Var)!=INDEX_NONE);
    GEditor->RedoTransaction();for(auto* S:Systems)TestTrue(TEXT("One redo reapplies entire batch"),S->GetExposedParameters().IndexOf(Var)==INDEX_NONE);
    return true;
}
#endif
