#include "NiagaraParamExposerUserHierarchy.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraSystemEditorData.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "ViewModels/HierarchyEditor/NiagaraUserParametersHierarchyViewModel.h"
#include "UObject/UnrealType.h"
#include "ScopedTransaction.h"
#include "Editor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPECategoryRegression,
    "NiagaraParamExposer.Categories.Regression",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPECategoryRegression::RunTest(const FString& Parameters)
{
    using namespace NiagaraParamExposerUserHierarchy;
    UNiagaraSystem* System = NewObject<UNiagaraSystem>(GetTransientPackage(), NAME_None, RF_Transactional);
    UNiagaraSystemFactoryNew::InitializeSystem(System, true);
    UNiagaraSystemEditorData* Data = Cast<UNiagaraSystemEditorData>(System->GetEditorData());
    if (!Data)
    {
        FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(UNiagaraSystem::StaticClass(), TEXT("EditorData"));
        if (!TestNotNull(TEXT("Editor data property"), Property)) return false;
        Data = NewObject<UNiagaraSystemEditorData>(System, NAME_None, RF_Transactional);
        Property->SetObjectPropertyValue_InContainer(System, Data);
    }
    auto Add = [&](const FString& Name, float Value)
    {
        FNiagaraVariable Var(FNiagaraTypeDefinition::GetFloatDef(), FName(*(TEXT("User.") + Name)));
        Var.SetValue(Value);
        System->GetExposedParameters().AddParameter(Var);
        System->GetExposedParameters().SetParameterValue(Value, Var);
        return Var;
    };
    const FNiagaraVariable Spawn = Add(TEXT("Fire_A_Spawnrate"), 17.25f);
    Add(TEXT("Fire_A_LifetimeMax"), 4.f);
    Add(TEXT("Fire_A_LifetimeMin"), 2.f);
    FNiagaraVariable Color(FNiagaraTypeDefinition::GetColorDef(), TEXT("User.Fire_A_Color"));
    const FLinearColor ColorValue(10.f, 1.5f, .25f, .7f);
    Color.SetValue(ColorValue);
    System->GetExposedParameters().AddParameter(Color);
    System->GetExposedParameters().SetParameterValue(ColorValue, Color);
    Add(TEXT("Fire_A_Opacity"), .7f);
    Add(TEXT("Fire_A_Noise"), 8.f);
    Add(TEXT("Other_Size"), 9.f);
    Add(TEXT("Unprefixed"), 10.f);
    FString Error;
    TestTrue(TEXT("Initial repair"), RepairEmitterCategories(*System, Error));
    auto Rows = GetCategoryAssignments(*System);
    TestEqual(TEXT("Only prefixed parameters grouped"), Rows.Num(), 7);
    TestTrue(TEXT("Underscores preserved in emitter prefix"), Rows.Contains(TEXT("Fire_A_Spawnrate|Fire_A|valid")));
    TestTrue(TEXT("Min before Max"), Rows.IndexOfByKey(TEXT("Fire_A_LifetimeMin|Fire_A|valid")) < Rows.IndexOfByKey(TEXT("Fire_A_LifetimeMax|Fire_A|valid")));
    TestTrue(TEXT("Other settings before color"), Rows.IndexOfByKey(TEXT("Fire_A_Noise|Fire_A|valid")) < Rows.IndexOfByKey(TEXT("Fire_A_Color|Fire_A|valid")));
    TestTrue(TEXT("Color before opacity"), Rows.IndexOfByKey(TEXT("Fire_A_Color|Fire_A|valid")) < Rows.IndexOfByKey(TEXT("Fire_A_Opacity|Fire_A|valid")));
    TestEqual(TEXT("Default value preserved"), System->GetExposedParameters().GetParameterValue<float>(Spawn), 17.25f);
    TestEqual(TEXT("LinearColor type and value preserved"), System->GetExposedParameters().GetParameterValue<FLinearColor>(Color), ColorValue);
    // Simulate a legacy entry with a stale GUID, then a new batch and engine refresh.
    TArray<UNiagaraHierarchyUserParameter*> Items;
    Data->UserParameterHierarchy->GetChildrenOfType<UNiagaraHierarchyUserParameter>(Items, true);
    if (!Items.IsEmpty()) Items[0]->SetIdentity(FHierarchyElementIdentity({FGuid::NewGuid()}, {}));
    PruneStaleUserParameterHierarchy(*System);
    Add(TEXT("Other_Lifetime"), 6.f);
    TestTrue(TEXT("Additive assignment"), AssignUserParametersToCategory(*System, TEXT("Other"), {TEXT("User.Other_Lifetime")}, Error));
    Data->SyncUserScriptVariables(System);
    Rows = GetCategoryAssignments(*System);
    TestEqual(TEXT("Existing categories survive cleanup and refresh"), Rows.Num(), 8);
    for (const FString& Row : Rows) TestTrue(TEXT("Engine-recognized GUID and correct ownership"), Row.EndsWith(TEXT("|valid")));
    TestTrue(TEXT("Reparent to a different category"), AssignUserParametersToCategory(*System, TEXT("Temporary"), {TEXT("Other_Size")}, Error));
    TestTrue(TEXT("Correct reparent ownership"), GetCategoryAssignments(*System).Contains(TEXT("Other_Size|Temporary|valid")));
    TestTrue(TEXT("Repair restores prefix grouping"), RepairEmitterCategories(*System, Error));
    const auto Before = GetCategoryAssignments(*System);
    TestTrue(TEXT("Idempotent repair"), RepairEmitterCategories(*System, Error));
    TestTrue(TEXT("No duplicate rows on repeat repair"), Before == GetCategoryAssignments(*System));
    System->GetExposedParameters().RemoveParameter(Spawn);
    PruneStaleUserParameterHierarchy(*System);
    TestFalse(TEXT("Deleted parameter pruned"), GetCategoryAssignments(*System).Contains(TEXT("Fire_A_Spawnrate|Fire_A|valid")));
    Add(TEXT("Fire_A_Spawnrate"), 23.f);
    TestTrue(TEXT("Delete and recreate"), RepairEmitterCategories(*System, Error));
    TestTrue(TEXT("Recreated entry valid"), GetCategoryAssignments(*System).Contains(TEXT("Fire_A_Spawnrate|Fire_A|valid")));
    Add(TEXT("Pair_LifetimeMaxExtra"), 1.f);
    Add(TEXT("Pair_LifetimeMax"), 2.f);
    Add(TEXT("Pair_LifetimeMin"), 3.f);
    TestTrue(TEXT("Mixed pair names repair"), RepairEmitterCategories(*System, Error));
    Rows = GetCategoryAssignments(*System);
    TestTrue(TEXT("Pair order stays consistent with other names"),
        Rows.IndexOfByKey(TEXT("Pair_LifetimeMin|Pair|valid")) < Rows.IndexOfByKey(TEXT("Pair_LifetimeMax|Pair|valid")) &&
        Rows.IndexOfByKey(TEXT("Pair_LifetimeMax|Pair|valid")) < Rows.IndexOfByKey(TEXT("Pair_LifetimeMaxExtra|Pair|valid")));
    // Previously silently stopped at 512 entries.
    for (int32 Index = 0; Index < 520; ++Index) Add(FString::Printf(TEXT("Large_Input%d"), Index), Index);
    TestTrue(TEXT("Large category repair"), RepairEmitterCategories(*System, Error));
    TestEqual(TEXT("All entries beyond 512 assigned"), GetCategoryAssignments(*System).Num(), 531);
    UNiagaraSystem* Copy = DuplicateObject<UNiagaraSystem>(System, GetTransientPackage());
    TestTrue(TEXT("Serialized duplicate retains category ownership and identities"), GetCategoryAssignments(*Copy) == GetCategoryAssignments(*System));
    if (GEditor && GEditor->Trans)
    {
        const auto BeforeUndo = GetCategoryAssignments(*System);
        {
            FScopedTransaction Transaction(NSLOCTEXT("NPE", "TestMove", "Test parameter category move"));
            TestTrue(TEXT("Transactional move"), AssignUserParametersToCategory(*System, TEXT("UndoTest"), {TEXT("Other_Size")}, Error));
        }
        GEditor->UndoTransaction();
        TestTrue(TEXT("Undo restores exact memberships and order"), BeforeUndo == GetCategoryAssignments(*System));
        GEditor->RedoTransaction();
        TestTrue(TEXT("Redo reapplies category"), GetCategoryAssignments(*System).Contains(TEXT("Other_Size|UndoTest|valid")));
    }
    return true;
}
#endif
