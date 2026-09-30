#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "FModelAnimRestoreLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "EditorFramework/AssetImportData.h"
#include "PsaReader.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/PoseAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/AnimBlueprintFactory.h"
#include "AnimGraphNode_PoseDriver.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimNode_KawaiiPhysics.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Editor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "SFModelRestorePanel.h"
#include "Widgets/Input/SFilePathPicker.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Layout/Children.h"

namespace
{
void FindPanelWidgets(const TSharedRef<SWidget>& Widget, const FName Type, TArray<TSharedRef<SWidget>>& Result)
{
    if (Widget->GetType() == Type) Result.Add(Widget);
    FChildren* Children = Widget->GetChildren();
    for (int32 Index = 0; Index < Children->Num(); ++Index)
        FindPanelWidgets(Children->GetChildAt(Index), Type, Result);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFModelPanelConstructionTest,"FModelAnimRestore.Editor.PanelConstruction",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFModelPanelConstructionTest::RunTest(const FString&)
{
    // The import tests do not construct Slate widgets. Exercise the actual panel and
    // its DesktopWidgets file picker, including its live binding and callback.
    auto& I18N = FInternationalization::Get();
    const FString PreviousLanguage = I18N.GetCurrentLanguage()->GetName();
    for (const TCHAR* Language : {TEXT("en"), TEXT("zh-Hans")})
    {
        I18N.SetCurrentLanguage(Language);
        for (bool Physics : {false, true})
            for (int32 Pass = 0; Pass < 16; ++Pass)
            {
                TSharedRef<SFModelRestorePanel> Panel = SNew(SFModelRestorePanel).Physics(Physics);
                Panel->SlatePrepass();
                TArray<TSharedRef<SWidget>> Pickers;
                FindPanelWidgets(Panel, TEXT("SFilePathPicker"), Pickers);
                TestEqual(TEXT("Pose uses name-only buttons; physics retains its picker"), Pickers.Num(), Physics?1:0);
                if (!Physics) {TArray<TSharedRef<SWidget>> Slots;FindPanelWidgets(Panel,TEXT("SObjectPropertyEntryBox"),Slots);TestEqual(TEXT("Readonly context asset slots"),Slots.Num(),2);}
                if (Pickers.IsEmpty()) continue;
                auto Picker = StaticCastSharedRef<SFilePathPicker>(Pickers[0]);
                TArray<TSharedRef<SWidget>> TextBoxes;
                FindPanelWidgets(Picker, TEXT("SEditableTextBox"), TextBoxes);
                TestEqual(TEXT("File picker has an editable path"), TextBoxes.Num(), 1);
                if (TextBoxes.Num() != 1) continue;
                auto TextBox = StaticCastSharedRef<SEditableTextBox>(TextBoxes[0]);
                for (const FString Path : {FString(TEXT("C:/Exports/girl022_abpp.json")), FString(TEXT("C:/Exports/ABP_Girl022_Phy.json")), FString()})
                {
                    Picker->SimulateTextInput(FText::FromString(Path));
                    Panel->SlatePrepass();
                    TestEqual(TEXT("Path commit updates the panel's bound JSON path"), TextBox->GetText().ToString(), Path);
                }
            }
    }
    I18N.SetCurrentLanguage(PreviousLanguage);
    AddInfo(TEXT("Constructed and destroyed 64 panels across both import modes and both languages; file picker callbacks passed."));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFModelPostProcessTest,"FModelAnimRestore.Girl022.PostProcessAssignment",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFModelPostProcessTest::RunTest(const FString&)
{
    auto* Fixture=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/FModelRestoreTests/Girl022/FBX/girl022_FBX.girl022_FBX"));
    if(!TestNotNull(TEXT("Girl022 FBX fixture"),Fixture))return false;
    TStrongObjectPtr<UPackage> Package(CreatePackage(*(TEXT("/Game/FModelRestoreTests/Girl022/PostProcess_")+FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    TStrongObjectPtr<USkeletalMesh> Mesh(DuplicateObject<USkeletalMesh>(Fixture,Package.Get(),TEXT("TestMesh")));
    Mesh->SetFlags(RF_Transactional);Mesh->SetPostProcessAnimBlueprint(nullptr);
    TStrongObjectPtr<UAnimBlueprintFactory> Factory(NewObject<UAnimBlueprintFactory>());
    Factory->TargetSkeleton=Mesh->GetSkeleton();Factory->PreviewSkeletalMesh=Mesh.Get();
    auto MakeBlueprint=[&](const TCHAR* Name){return Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),Package.Get(),Name,RF_Public|RF_Standalone|RF_Transactional,nullptr,GWarn));};
    TStrongObjectPtr<UAnimBlueprint> First(MakeBlueprint(TEXT("FirstPostProcess")));
    TStrongObjectPtr<UAnimBlueprint> Second(MakeBlueprint(TEXT("SecondPostProcess")));
    if(!First || !Second)return false;
    FString Report;
    TestFalse(TEXT("Missing mesh is rejected"),UFModelAnimRestoreLibrary::SetAsPostProcess(First.Get(),nullptr,Report));
    if(!TestTrue(TEXT("Assign current blueprint"),UFModelAnimRestoreLibrary::SetAsPostProcess(First.Get(),Mesh.Get(),Report))){AddError(Report);return false;}
    TestTrue(TEXT("Mesh references compiled current blueprint"),Mesh->GetPostProcessAnimBlueprint()==First->GeneratedClass);
    TestTrue(TEXT("Mesh is marked for saving"),Package->IsDirty());
    TestTrue(TEXT("Undo assignment"),GEditor->UndoTransaction());
    TestNull(TEXT("Undo restores empty assignment"),Mesh->GetPostProcessAnimBlueprint().Get());
    TestTrue(TEXT("Redo assignment"),GEditor->RedoTransaction());
    TestTrue(TEXT("Redo restores blueprint"),Mesh->GetPostProcessAnimBlueprint()==First->GeneratedClass);
    if(!TestTrue(TEXT("Replace an existing post process"),UFModelAnimRestoreLibrary::SetAsPostProcess(Second.Get(),Mesh.Get(),Report)))return false;
    TestTrue(TEXT("Replacement uses second blueprint"),Mesh->GetPostProcessAnimBlueprint()==Second->GeneratedClass);
    TestTrue(TEXT("Undo replacement"),GEditor->UndoTransaction());
    TestTrue(TEXT("Undo restores previous blueprint"),Mesh->GetPostProcessAnimBlueprint()==First->GeneratedClass);
    TestTrue(TEXT("Repeated assignment is harmless"),UFModelAnimRestoreLibrary::SetAsPostProcess(First.Get(),Mesh.Get(),Report));
    TStrongObjectPtr<USkeleton> WrongSkeleton(NewObject<USkeleton>());
    Second->TargetSkeleton=WrongSkeleton.Get();
    TestFalse(TEXT("Incompatible skeleton rejected"),UFModelAnimRestoreLibrary::SetAsPostProcess(Second.Get(),Mesh.Get(),Report));
    TestTrue(TEXT("Rejected assignment preserves previous setting"),Mesh->GetPostProcessAnimBlueprint()==First->GeneratedClass);
    Second->TargetSkeleton=Mesh->GetSkeleton();
    Mesh->SetPostProcessAnimBlueprint(nullptr);
    AddInfo(TEXT("FBX mesh post-process assignment, replacement, Undo/Redo, save state and skeleton guard passed on an isolated copy."));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFModelPanelImportTest,"FModelAnimRestore.Girl022.PanelImports",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFModelPanelImportTest::RunTest(const FString&)
{
    TStrongObjectPtr<UFModelAnimRestoreOptions> Options(NewObject<UFModelAnimRestoreOptions>());
    Options->TargetMesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/FModelRestoreTests/Girl022/Input/girl022_body01_skm.girl022_body01_skm"));
    if(!TestNotNull(TEXT("Girl022 fixture"),Options->TargetMesh.Get()))return false;
    const FString Folder=TEXT("/Game/FModelRestoreTests/Girl022/Panel_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Options->Destination=Folder;
    Options->PoseBlueprintJson.FilePath=TEXT("F:/BreakSnowStudy/FModel/Output/Exports/Game/Content/Characters/Girl/girl022/girl022_abpp.json");
    Options->PoseDirectory.Path=TEXT("F:/BreakSnowStudy/FModel/Output/Exports/Game/Content/Characters/Girl/girl022/POSE");
    TStrongObjectPtr<UAnimBlueprintFactory> Factory(NewObject<UAnimBlueprintFactory>());
    Factory->TargetSkeleton=Options->TargetMesh->GetSkeleton(); Factory->PreviewSkeletalMesh=Options->TargetMesh;
    TStrongObjectPtr<UPackage> Package(CreatePackage(*(Folder/TEXT("girl022_PanelDemo"))));
    TStrongObjectPtr<UAnimBlueprint> BP(Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),Package.Get(),TEXT("girl022_PanelDemo"),RF_Public|RF_Standalone|RF_Transactional,nullptr,GWarn)));
    if(!TestNotNull(TEXT("Test blueprint"),BP.Get()))return false;
    UEdGraph* Graph=nullptr;
    for(UEdGraph* G:BP->FunctionGraphs)if(G->GetFName()==TEXT("AnimGraph"))Graph=G;
    if(!TestNotNull(TEXT("AnimGraph"),Graph))return false;
    UAnimGraphNode_Root* Root=nullptr;
    for(UEdGraphNode* N:Graph->Nodes)if(auto* R=Cast<UAnimGraphNode_Root>(N))Root=R;
    if(!TestNotNull(TEXT("Output pose"),Root))return false;
    FGraphNodeCreator<UAnimGraphNode_SequencePlayer> Creator(*Graph);
    auto* Sequence=Creator.CreateNode();
    Sequence->Node.SetSequence(LoadObject<UAnimSequence>(nullptr,TEXT("/Game/FModelRestoreTests/Girl022/Input/girl022_wp01a_base_run_loop.girl022_wp01a_base_run_loop")));
    Sequence->NodePosX=-360; Creator.Finalize(); Sequence->ReconstructNode();
    auto PosePin=[](UEdGraphNode* Node,EEdGraphPinDirection Dir)->UEdGraphPin*
    {
        for(auto* Pin:Node->Pins)if(Pin->Direction==Dir && Pin->PinType.PinCategory==TEXT("struct"))return Pin;
        return nullptr;
    };
    Graph->GetSchema()->TryCreateConnection(PosePin(Sequence,EGPD_Output),PosePin(Root,EGPD_Input));
    const int32 Before=Graph->Nodes.Num();
    TArray<FFModelRestoreGroup> Groups; FString Report;
    if(!TestTrue(TEXT("Inspect all limb groups"),UFModelAnimRestoreLibrary::Inspect(Options.Get(),Groups,Report))) {AddError(Report);return false;}
    if(!TestEqual(TEXT("Four independent limb groups"),Groups.Num(),4))return false;
    TestEqual(TEXT("Inspection leaves graph unchanged"),Graph->Nodes.Num(),Before);
    TArray<UEdGraphNode*> PoseNodes;
    for(int32 Round=0;Round<2;++Round)
    {
        const TArray<FString> Selection={Groups[Round].Id,Groups[Round+2].Id};
        UFModelAnimRestoreBatch* Raw=nullptr;
        if(!TestTrue(TEXT("Create two selected Pose Assets"),UFModelAnimRestoreLibrary::PrepareForBlueprint(Options.Get(),BP.Get(),Selection,Raw,Report))) {AddError(Report);return false;}
        TStrongObjectPtr<UFModelAnimRestoreBatch> Batch(Raw);
        TestEqual(TEXT("Only two selected drivers prepared"),Batch->PoseDriverCount,2);
        TestEqual(TEXT("Only samples and Pose Assets created"),Batch->CreatedAssets.Num(),4);
        TestEqual(TEXT("Preparing does not add nodes"),Graph->Nodes.Num(),Before+Round*2);
        TArray<UEdGraphNode*> Added;
        if(!TestTrue(TEXT("Add selected nodes to current graph"),UFModelAnimRestoreLibrary::AddToBlueprint(Batch.Get(),BP.Get(),Options->TargetMesh,Graph,Added,Report))) {AddError(Report);return false;}
        TestEqual(TEXT("Two added nodes"),Added.Num(),2);
        for(auto* N:Added)
        {
            auto* Driver=Cast<UAnimGraphNode_PoseDriver>(N);
            if(!TestNotNull(TEXT("Native Pose Driver"),Driver))return false;
            TestNotNull(TEXT("Pose Asset reference survived graph import"),Driver->Node.PoseAsset.Get());
            TestEqual(TEXT("Ten RBF targets retained"),Driver->Node.PoseTargets.Num(),10);
        }
        if(Round==0)
        {
            TestTrue(TEXT("Undo insertion"),GEditor->UndoTransaction());
            TestEqual(TEXT("Undo restored graph"),Graph->Nodes.Num(),Before);
            TestTrue(TEXT("Redo insertion"),GEditor->RedoTransaction());
            TestEqual(TEXT("Redo restored two drivers"),Graph->Nodes.Num(),Before+2);
        }
        // Resolve nodes again after Undo/Redo; transaction restoration can recreate objects.
        PoseNodes.Reset();
        for(UEdGraphNode* N:Graph->Nodes)if(N->IsA<UAnimGraphNode_PoseDriver>())PoseNodes.Add(N);
        TestTrue(TEXT("Original output connection retained"),PosePin(Root,EGPD_Input)->LinkedTo.Contains(PosePin(Sequence,EGPD_Output)));
        TestFalse(TEXT("Reject changed preview mesh"),UFModelAnimRestoreLibrary::AddToBlueprint(Batch.Get(),BP.Get(),nullptr,Graph,Added,Report));
        auto* Other=LoadObject<UAnimBlueprint>(nullptr,TEXT("/Game/FModelRestored/Girl022/girl022_Preview.girl022_Preview"));
        TestFalse(TEXT("Reject another blueprint"),UFModelAnimRestoreLibrary::AddToBlueprint(Batch.Get(),Other,Options->TargetMesh,Graph,Added,Report));
    }
    TestEqual(TEXT("Four groups added across two imports"),PoseNodes.Num(),4);
    Options->PoseBlueprintJson.FilePath.Empty();
    Options->PhysicsBlueprintJson.FilePath=TEXT("F:/BreakSnowStudy/FModel/Output/Exports/Game/Content/Blueprints/Character/Hero/girl022/ABP_Girl022_Phy.json");
    if(!TestTrue(TEXT("Inspect physics separately"),UFModelAnimRestoreLibrary::Inspect(Options.Get(),Groups,Report))){AddError(Report);return false;}
    TestEqual(TEXT("68 physical groups"),Groups.Num(),68);
    TArray<FString> PhysicsSelection; for(const auto& Group:Groups)PhysicsSelection.Add(Group.Id);
    UFModelAnimRestoreBatch* Raw=nullptr;
    if(!TestTrue(TEXT("Prepare physics separately"),UFModelAnimRestoreLibrary::PrepareForBlueprint(Options.Get(),BP.Get(),PhysicsSelection,Raw,Report))){AddError(Report);return false;}
    TStrongObjectPtr<UFModelAnimRestoreBatch> PhysicsBatch(Raw);
    TestEqual(TEXT("Physics preparation creates no Pose Assets or AnimBPs"),PhysicsBatch->CreatedAssets.Num(),0);
    TArray<UEdGraphNode*> PhysicsNodes;
    if(!TestTrue(TEXT("Add physics nodes"),UFModelAnimRestoreLibrary::AddToBlueprint(PhysicsBatch.Get(),BP.Get(),Options->TargetMesh,Graph,PhysicsNodes,Report))){AddError(Report);return false;}
    TestEqual(TEXT("68 nodes plus two space conversions"),PhysicsNodes.Num(),70);
    TestTrue(TEXT("Existing output is still connected"),PosePin(Root,EGPD_Input)->LinkedTo.Contains(PosePin(Sequence,EGPD_Output)));
    int32 Count=0;
    for(auto* N:PhysicsNodes)
        if(auto* Property=FindFProperty<FStructProperty>(N->GetClass(),TEXT("Node"));Property && Property->Struct==FAnimNode_KawaiiPhysics::StaticStruct())
        {
            ++Count;
            TestFalse(TEXT("Physics root retained"),Property->ContainerPtrToValuePtr<FAnimNode_KawaiiPhysics>(N)->RootBone.BoneName.IsNone());
        }
    TestEqual(TEXT("68 native physics nodes"),Count,68);
    // Connect only the dedicated demo after verifying the import preserves existing links.
    for(auto* N:PoseNodes)for(auto* Pin:N->Pins)Pin->BreakAllPinLinks();
    auto Link=[&](UEdGraphNode* A,UEdGraphNode* B){return Graph->GetSchema()->TryCreateConnection(PosePin(A,EGPD_Output),PosePin(B,EGPD_Input));};
    TestTrue(TEXT("Demo input"),Link(Sequence,PhysicsNodes[0]));
    UEdGraphNode* Last=PhysicsNodes.Last();
    for(auto* Pose:PoseNodes){TestTrue(TEXT("Demo pose chain"),Link(Last,Pose));Last=Pose;}
    TestTrue(TEXT("Demo output"),Link(Last,Root));
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP.Get());
    FCompilerResultsLog Log; FKismetEditorUtilities::CompileBlueprint(BP.Get(),EBlueprintCompileOptions::None,&Log);
    TestEqual(TEXT("Demo compile errors"),Log.NumErrors,0);
    FSavePackageArgs SaveArgs; SaveArgs.TopLevelFlags=RF_Public|RF_Standalone;
    const FString Filename=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
    TestTrue(TEXT("Save demo blueprint"),UPackage::SavePackage(Package.Get(),BP.Get(),*Filename,SaveArgs));
    FFileHelper::SaveStringToFile(BP->GetPathName(),*(FPaths::ProjectSavedDir()/TEXT("FModelAnimRestore/Girl022PanelDemo.txt")));
    AddInfo(TEXT("4 Pose Driver groups imported in two selections; 68 physics nodes imported separately; Undo/Redo and context isolation passed. Demo: ")+BP->GetPathName());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFModelLocalizationTest,"FModelAnimRestore.Girl022.Localization",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFModelLocalizationTest::RunTest(const FString&)
{
    auto& I18N=FInternationalization::Get();
    const FString Previous=I18N.GetCurrentLanguage()->GetName();
    I18N.SetCurrentLanguage(TEXT("zh-Hans"));
    TestEqual(TEXT("Chinese menu title"),NSLOCTEXT("FModelAnimRestore","PoseTitle","Import Pose Drivers / Pose Assets").ToString(),FString(TEXT("导入 Pose Driver / Pose Asset")));
    FString Report; UFModelAnimRestoreLibrary::Restore(nullptr,Report);
    TestEqual(TEXT("Chinese error"),Report,FString(TEXT("缺少导入设置。")));
    I18N.SetCurrentLanguage(TEXT("en"));
    TestEqual(TEXT("English menu title"),NSLOCTEXT("FModelAnimRestore","PhysicsTitle","Import Kawaii Physics").ToString(),FString(TEXT("Import Kawaii Physics")));
    UFModelAnimRestoreLibrary::Restore(nullptr,Report);
    TestEqual(TEXT("English error"),Report,FString(TEXT("Import options are missing.")));
    I18N.SetCurrentLanguage(Previous);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFModelPsaFolderTest,"FModelAnimRestore.Girl022.PsaFolder",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFModelPsaFolderTest::RunTest(const FString&)
{
    const FString Id=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Folder=TEXT("/Game/FModelRestoreTests/Girl022/Psa_")+Id;
    const FString Files=FPaths::ProjectSavedDir()/TEXT("FModelAnimRestore/PsaFolderTests")/Id;
    const FString Source=TEXT("F:/BreakSnowStudy/FModel/Output/Exports/Game/Content/Characters/Girl/girl022");
    IFileManager::Get().MakeDirectory(*Files,true);
    TestEqual(TEXT("Copy standalone abpp"),IFileManager::Get().Copy(*(Files/TEXT("girl022_abpp.json")),*(Source/TEXT("girl022_abpp.json"))),COPY_OK);
    TArray<FString> PoseFiles;IFileManager::Get().FindFiles(PoseFiles,*(Source/TEXT("POSE/*_PoseAsset.json")),true,false);
    for(const auto& Json:PoseFiles)
    {
        FString Psa=FPaths::GetBaseFilename(Json);Psa.RemoveFromEnd(TEXT("_PoseAsset"));Psa+=TEXT(".psa");
        TestEqual(TEXT("Copy PoseAsset JSON"),IFileManager::Get().Copy(*(Files/Json),*(Source/TEXT("POSE")/Json)),COPY_OK);
        TestEqual(TEXT("Copy PSA"),IFileManager::Get().Copy(*(Files/Psa),*(Source/TEXT("POSE")/Psa)),COPY_OK);
    }
    TStrongObjectPtr<UFModelAnimRestoreOptions> Options(NewObject<UFModelAnimRestoreOptions>());
    Options->TargetMesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/FModelRestoreTests/Girl022/FBX/girl022_FBX.girl022_FBX"));
    if(!TestNotNull(TEXT("FBX-imported Girl022 fixture"),Options->TargetMesh.Get()))return false;
    TestTrue(TEXT("Fixture actually imported from FBX"),Options->TargetMesh->GetAssetImportData()->GetFirstFilename().EndsWith(TEXT(".fbx")));
    Options->PoseBlueprintJson.FilePath=Files/TEXT("girl022_abpp.json");
    Options->PoseDirectory.Path=Files;
    Options->ExportContentDirectory.Path=Files/TEXT("Nonexistent_Content_Is_Not_Required");
    TStrongObjectPtr<UAnimBlueprintFactory> Factory(NewObject<UAnimBlueprintFactory>());
    Factory->TargetSkeleton=Options->TargetMesh->GetSkeleton();Factory->PreviewSkeletalMesh=Options->TargetMesh;
    TStrongObjectPtr<UPackage> Package(CreatePackage(*(Folder/TEXT("ABP_Psa"))));
    TStrongObjectPtr<UAnimBlueprint> BP(Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),Package.Get(),TEXT("ABP_Psa"),RF_Public|RF_Standalone|RF_Transactional,nullptr,GWarn)));
    Options->Destination=UFModelAnimRestoreLibrary::DefaultPoseDestination(BP.Get());
    TestEqual(TEXT("Output beside the blueprint"),Options->Destination,Folder/TEXT("POSE"));
    TArray<FFModelRestoreGroup> Groups;FString Report;
    if(!TestTrue(TEXT("Read loose files without Content tree or Skeleton JSON"),UFModelAnimRestoreLibrary::Inspect(Options.Get(),Groups,Report))){AddError(Report);return false;}
    if(!TestEqual(TEXT("Four groups"),Groups.Num(),4))return false;
    TArray<FString> Selection;for(const auto& Group:Groups){Selection.Add(Group.Id);TestEqual(TEXT("Ten poses"),Group.PoseCount,10);}
    UFModelAnimRestoreBatch* Raw=nullptr;
    if(!TestTrue(TEXT("Import four PSA animations and create poses"),UFModelAnimRestoreLibrary::PrepareForBlueprint(Options.Get(),BP.Get(),Selection,Raw,Report))){AddError(Report);return false;}
    TStrongObjectPtr<UFModelAnimRestoreBatch> First(Raw);
    TestEqual(TEXT("Eight persistent assets"),First->CreatedAssets.Num(),8);
    TestFalse(TEXT("Panel success text contains no paths"),Report.Contains(TEXT("/Game/"))||Report.Contains(Files));
    for(const auto& Group:Groups)
    {
        auto* Animation=First->PoseAnimations.FindChecked(Group.Id).Get();auto* Pose=First->PoseAssets.FindChecked(Group.Id).Get();
        TestEqual(TEXT("Pose linked to imported PSA animation"),Pose->SourceAnimation.Get(),Animation);
        TestTrue(TEXT("Source file matches selected folder"),FPaths::IsSamePath(Animation->AssetImportData->GetFirstFilename(),Group.PsaFile));
        TestEqual(TEXT("Ten imported samples"),Animation->GetDataModel()->GetNumberOfKeys(),10);
        TestEqual(TEXT("Ten named poses"),Pose->GetPoseFNames().Num(),10);
        TestTrue(TEXT("Saved in the default POSE folder"),Animation->GetOutermost()->GetName().StartsWith(Options->Destination+TEXT("/")));
        // Verify actual data came from PSA, with its coordinate conversion, on the driving bone.
        FPsaReader Reader(Group.PsaFile);const FName Bone(*Group.Bones);
        const int32 Index=Reader.Bones.IndexOfByPredicate([&](const auto& B){return FName(UTF8_TO_TCHAR(B.Name))==Bone;});
        if(!TestTrue(TEXT("Source bone exists in PSA"),Index>0))return false;
        for(int32 Frame=0;Frame<10;++Frame)
        {
            const auto& Key=Reader.Keys[Frame*Reader.Bones.Num()+Index];
            const FTransform Actual=Animation->GetDataModel()->EvaluateBoneTrackTransform(Bone,FFrameTime(Frame),EAnimInterpolationType::Step);
            TestTrue(TEXT("PSA rotation preserved"),Actual.GetRotation().Equals(FQuat(Key.Rotation.X,-Key.Rotation.Y,Key.Rotation.Z,Key.Rotation.W).GetNormalized(),1.e-4));
            TestTrue(TEXT("PSA translation preserved"),Actual.GetTranslation().Equals(FVector(Key.Position.X,-Key.Position.Y,Key.Position.Z),1.e-4));
        }
    }
    if(!TestTrue(TEXT("Reimport creates unique assets"),UFModelAnimRestoreLibrary::PrepareForBlueprint(Options.Get(),BP.Get(),{Selection[0]},Raw,Report))){AddError(Report);return false;}
    TStrongObjectPtr<UFModelAnimRestoreBatch> Second(Raw);
    TestNotEqual(TEXT("Previous animation kept"),First->PoseAnimations.FindChecked(Selection[0])->GetPathName(),Second->PoseAnimations.FindChecked(Selection[0])->GetPathName());
    TestNotEqual(TEXT("Previous pose kept"),First->PoseAssets.FindChecked(Selection[0])->GetPathName(),Second->PoseAssets.FindChecked(Selection[0])->GetPathName());
    // Corrupt only an isolated test copy: mismatched target names must fail before creation.
    FString Json;FFileHelper::LoadFileToString(Json,*Groups[0].PoseJson);
    TArray<TSharedPtr<FJsonValue>> Exports;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Exports);
    auto Container=Exports[0]->AsObject()->GetObjectField(TEXT("Properties"))->GetObjectField(TEXT("PoseContainer"));
    auto Names=Container->GetArrayField(TEXT("PoseNames"));
    const FString OriginalName=Names[0]->AsObject()->GetStringField(TEXT("DisplayName"));
    Names[0]->AsObject()->SetStringField(TEXT("DisplayName"),TEXT("Invalid_Test_Pose_Name"));
    FString Invalid;FJsonSerializer::Serialize(Exports,TJsonWriterFactory<>::Create(&Invalid));
    FFileHelper::SaveStringToFile(Invalid,*Groups[0].PoseJson);
    TestFalse(TEXT("Pose names must match abpp targets"),UFModelAnimRestoreLibrary::PrepareForBlueprint(Options.Get(),BP.Get(),Selection,Raw,Report));
    TestNull(TEXT("No incomplete batch"),Raw);
    Names[0]->AsObject()->SetStringField(TEXT("DisplayName"),OriginalName);
    Names.Add(MakeShared<FJsonValueString>(TEXT("Extra_Test_Pose")));
    Container->SetArrayField(TEXT("PoseNames"),Names);
    Invalid.Empty();FJsonSerializer::Serialize(Exports,TJsonWriterFactory<>::Create(&Invalid));
    FFileHelper::SaveStringToFile(Invalid,*Groups[0].PoseJson);
    TestFalse(TEXT("PSA frame count must match JSON pose count"),UFModelAnimRestoreLibrary::PrepareForBlueprint(Options.Get(),BP.Get(),Selection,Raw,Report));
    TestNull(TEXT("Frame mismatch creates no batch"),Raw);
    FFileHelper::SaveStringToFile(Json,*Groups[0].PoseJson);
    Options->PoseDirectory.Path=Files/TEXT("Missing");
    TestFalse(TEXT("Missing source folder rejected"),UFModelAnimRestoreLibrary::Inspect(Options.Get(),Groups,Report));
    AddInfo(TEXT("Girl022 FBX mesh: loose PSA + PoseAsset JSON + abpp only; four groups, actual PSA samples, asset references, unique reimport, and validation passed."));
    return true;
}
#endif
