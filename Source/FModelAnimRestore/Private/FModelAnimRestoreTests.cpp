#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "FModelAnimRestoreLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "PsaReader.h"
#include "PsaImporter.h"
#include "AnimNode_KawaiiPhysics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PreviewScene.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFModelGirl022RuntimeTest,"FModelAnimRestore.Girl022.Runtime",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFModelGirl022RuntimeTest::RunTest(const FString&)
{
    auto* Mesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/FModelRestoreTests/Girl022/Input/girl022_body01_skm.girl022_body01_skm"));
    auto* BP=LoadObject<UAnimBlueprint>(nullptr,TEXT("/Game/FModelRestored/Girl022/girl022_Preview.girl022_Preview"));
    if (!TestNotNull(TEXT("Saved Girl022 mesh"),Mesh) || !TestNotNull(TEXT("Saved preview blueprint"),BP)) return false;
    FPreviewScene Scene(FPreviewScene::ConstructionValues().ShouldSimulatePhysics(false));
    TStrongObjectPtr<USkeletalMeshComponent> Enabled(NewObject<USkeletalMeshComponent>());
    TStrongObjectPtr<USkeletalMeshComponent> Disabled(NewObject<USkeletalMeshComponent>());
    for (auto* C : {Enabled.Get(),Disabled.Get()})
    {
        C->SetSkeletalMeshAsset(Mesh);
        C->SetAnimInstanceClass(BP->GeneratedClass);
        Scene.AddComponent(C,FTransform::Identity);
        C->InitAnim(true);
    }
    int32 NodeCount=0;
    auto* Class=CastChecked<UAnimBlueprintGeneratedClass>(BP->GeneratedClass);
    for (auto* Property : Class->GetAnimNodeProperties())
        if (Property->Struct==FAnimNode_KawaiiPhysics::StaticStruct())
        {
            auto* N=Property->ContainerPtrToValuePtr<FAnimNode_KawaiiPhysics>(Disabled->GetAnimInstance());
            N->Alpha=0; ++NodeCount;
        }
    TestEqual(TEXT("68 runtime Kawaii nodes"),NodeCount,68);
    double MaxDifference=0;
    TSet<int32> Changed;
    for (int32 Frame=0; Frame<180; ++Frame)
    {
        const FVector Location(20*FMath::Sin(Frame*0.075),12*FMath::Cos(Frame*0.05),0);
        for (auto* C : {Enabled.Get(),Disabled.Get()})
        {
            C->SetWorldLocation(Location);
            C->TickAnimation(1.f/60.f,false);
            C->RefreshBoneTransforms();
        }
        auto A=Enabled->GetBoneSpaceTransforms(); auto B=Disabled->GetBoneSpaceTransforms();
        if (!TestEqual(TEXT("Pose bone count"),A.Num(),B.Num())) return false;
        for (int32 I=0; I<A.Num(); ++I)
        {
            if (A[I].ContainsNaN() || !A[I].GetRotation().IsNormalized() || A[I].GetTranslation().Size()>10000)
            { AddError(FString::Printf(TEXT("Non-finite/unstable pose at frame %d bone %d"),Frame,I)); return false; }
            const double Delta=FMath::Max((A[I].GetTranslation()-B[I].GetTranslation()).Length(),A[I].GetRotation().AngularDistance(B[I].GetRotation()));
            MaxDifference=FMath::Max(MaxDifference,Delta);
            if (Delta>0.001) Changed.Add(I);
        }
    }
    TestTrue(TEXT("Physics affects the evaluated animation"),MaxDifference>0.01 && Changed.Num()>5);
    AddInfo(FString::Printf(TEXT("180 frames; %d physics-affected bones; max difference %.6f"),Changed.Num(),MaxDifference));
    Scene.RemoveComponent(Enabled.Get()); Scene.RemoveComponent(Disabled.Get());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFModelGuardTest,"FModelAnimRestore.Girl022.Guards",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFModelGuardTest::RunTest(const FString&)
{
    TStrongObjectPtr<UFModelAnimRestoreOptions> O(NewObject<UFModelAnimRestoreOptions>());
    FString Report;
    TestFalse(TEXT("Reject empty input"),UFModelAnimRestoreLibrary::Restore(O.Get(),Report));
    O->TargetMesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/FModelRestoreTests/Girl022/Input/girl022_body01_skm.girl022_body01_skm"));
    if (!TestNotNull(TEXT("Fixture"),O->TargetMesh.Get())) return false;
    O->PoseBlueprintJson.FilePath=TEXT("F:/BreakSnowStudy/FModel/Output/Exports/Game/Content/Characters/Girl/girl022/girl022_abpp.json");
    O->PoseDirectory.Path=TEXT("F:/BreakSnowStudy/FModel/Output/Exports/Game/Content/Characters/Girl/girl022/POSE");
    O->Destination=TEXT("/Game/FModelRestored/Girl022");
    TestFalse(TEXT("Reject overwrite"),UFModelAnimRestoreLibrary::Restore(O.Get(),Report));
    TestTrue(TEXT("Overwrite reason is reported"),Report.Contains(TEXT("/Game/FModelRestored/Girl022/")));
    O->Destination=TEXT("/Game/../Invalid");
    TestFalse(TEXT("Reject invalid package path"),UFModelAnimRestoreLibrary::Restore(O.Get(),Report));
    FPsaReader Reader(TEXT("F:/BreakSnowStudy/FModel/Output/Exports/Game/Content/Characters/Girl/girl022/POSE/girl022_Forearm_L_POSE.psa"));
    TArray<FPsaBoneMapping> Mapping;FString Error;
    if(!TestTrue(TEXT("Unmodified PSA hierarchy matches"),FPsaImporter::MatchBones(Reader,O->TargetMesh,Mapping,Error)))return false;
    const int32 Index=Reader.Bones.IndexOfByPredicate([](const auto& Bone){return FString(UTF8_TO_TCHAR(Bone.Name))==TEXT("Bip001-L-Forearm");});
    if(!TestTrue(TEXT("Forearm exists"),Index>0))return false;
    Reader.Bones[Index].ParentIndex=0;
    TestFalse(TEXT("Reject mismatched PSA bone parent"),FPsaImporter::MatchBones(Reader,O->TargetMesh,Mapping,Error));
    TestTrue(TEXT("Hierarchy error names the bone"),Error.Contains(TEXT("Bip001-L-Forearm")));
    return true;
}
#endif
