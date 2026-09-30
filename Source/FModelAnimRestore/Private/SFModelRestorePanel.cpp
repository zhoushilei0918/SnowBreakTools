#include "SFModelRestorePanel.h"
#include "IAnimationBlueprintEditor.h"
#include "IPersonaToolkit.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/PoseAsset.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "AnimationGraphSchema.h"
#include "Engine/SkeletalMesh.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/PackageName.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "FModelAnimRestore"

UAnimBlueprint* SFModelRestorePanel::Blueprint() const
{
    const auto Pinned=Editor.Pin();
    return Pinned ? Cast<UAnimBlueprint>(Pinned->GetBlueprintObj()) : nullptr;
}
USkeletalMesh* SFModelRestorePanel::Mesh() const
{
    const auto Pinned=Editor.Pin();
    if (!Pinned) return nullptr;
    const auto Toolkit=Pinned->GetPersonaToolkit();
    if (const auto* Component=Toolkit->GetPreviewMeshComponent()) return Component->GetSkeletalMeshAsset();
    return Toolkit->GetPreviewMesh();
}
UEdGraph* SFModelRestorePanel::Graph() const
{
    const auto Pinned=Editor.Pin(); auto* BP=Blueprint();
    if (!Pinned || !BP) return nullptr;
    if (SelectedGraph.IsValid() && FBlueprintEditorUtils::FindBlueprintForGraph(SelectedGraph.Get())==BP) return SelectedGraph.Get();
    if (auto* Focused=Pinned->GetFocusedGraph(); Focused && Focused->GetSchema()->IsA<UAnimationGraphSchema>() && FBlueprintEditorUtils::FindBlueprintForGraph(Focused)==BP) return Focused;
    for (UEdGraph* Candidate : BP->FunctionGraphs) if (Candidate->GetFName()==TEXT("AnimGraph")) return Candidate;
    return nullptr;
}
void SFModelRestorePanel::Construct(const FArguments& Args)
{
    Editor=Args._Editor; bPhysics=Args._Physics;
    Options.Reset(NewObject<UFModelAnimRestoreOptions>()); ObservedMesh=Mesh();
    if (auto* BP=Blueprint())
    {
        Options->Destination=bPhysics ? TEXT("/Game/FModelRestored/")+BP->GetName() : UFModelAnimRestoreLibrary::DefaultPoseDestination(BP);
    }
    else if (!bPhysics) Options->Destination=TEXT("/Game/POSE");
    const auto HeadingFont=FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle");
    TSharedPtr<SVerticalBox> Form;
    ChildSlot[SNew(SBox).MinDesiredWidth(510)[SNew(SVerticalBox)
        +SVerticalBox::Slot().FillHeight(1).Padding(12)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(Form,SVerticalBox)]]
        +SVerticalBox::Slot().AutoHeight().Padding(12,0,12,12)[SNew(SBox).HeightOverride(125)[SAssignNew(Output,SMultiLineEditableTextBox).IsReadOnly(true).AutoWrapText(true)]]]];
    Form->AddSlot().AutoHeight().Padding(0,0,0,12)[SNew(STextBlock).AutoWrapText(true).Text(bPhysics ?
        LOCTEXT("PhysicsHelp","Read a physics blueprint JSON, prepare the selected Kawaii Physics groups, then add them to this Animation Blueprint.") :
        LOCTEXT("PsaPoseHelp","Choose abpp.json and the POSE folder. Import the selected PSA animations, create Pose Assets, then add their Pose Drivers to this blueprint."))];
    BuildContext(Form.ToSharedRef());
    BuildFileOptions(Form.ToSharedRef());
    Form->AddSlot().AutoHeight().Padding(0,12)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("ReadGroups","Read groups")).IsEnabled_Lambda([this]{return Blueprint()&&Mesh()&&(bPhysics?!Options->PhysicsBlueprintJson.FilePath.IsEmpty():(!Options->PoseBlueprintJson.FilePath.IsEmpty()&&!Options->PoseDirectory.Path.IsEmpty()));}).OnClicked(this,&SFModelRestorePanel::ReadGroups)]
        +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(LOCTEXT("SelectAll","Select all")).OnClicked(this,&SFModelRestorePanel::SelectAll,true)]
        +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("SelectNone","Clear selection")).OnClicked(this,&SFModelRestorePanel::SelectAll,false)]];
    const TSharedRef<SWidget> PrepareButton=SNew(SButton).HAlign(HAlign_Center)
        .Text(bPhysics?LOCTEXT("PreparePhysics","1. Prepare selected physics groups"):LOCTEXT("ImportPsaPoses","1. Import animations and create Pose Assets"))
        .IsEnabled_Lambda([this]{return Blueprint()&&Mesh()&&SelectedCount()>0&&!bNeedsRead;}).OnClicked(this,&SFModelRestorePanel::Prepare);
    Form->AddSlot().AutoHeight()[SNew(SSeparator)];
    Form->AddSlot().AutoHeight().Padding(0,12)[PrepareButton];
    Form->AddSlot().AutoHeight().Padding(0,0,0,6)[SNew(STextBlock).Font(HeadingFont).Text_Lambda([this]{return FText::Format(LOCTEXT("GroupCount","Groups: {0} selected / {1} total"),FText::AsNumber(SelectedCount()),FText::AsNumber(Rows.Num()));})];
    Form->AddSlot().AutoHeight()[SAssignNew(GroupList,SVerticalBox)];
    TSharedRef<SHorizontalBox> Actions=SNew(SHorizontalBox);
    Form->AddSlot().AutoHeight().Padding(0,12)[Actions];
    Actions->AddSlot().FillWidth(1)[SNew(SButton).HAlign(HAlign_Center).Text(LOCTEXT("AddNodes","2. Add nodes to this blueprint"))
        .IsEnabled_Lambda([this]{return Batch.IsValid()&&Editor.IsValid()&&Mesh()==Batch->TargetMesh&&Graph();}).OnClicked(this,&SFModelRestorePanel::AddNodes)];
    if (!bPhysics) Actions->AddSlot().FillWidth(1).Padding(8,0,0,0)[SNew(SButton).HAlign(HAlign_Center)
        .Text(LOCTEXT("SetPostProcessButton","(Optional) 3. Set as mesh Post Process Anim Blueprint"))
        .ToolTipText(LOCTEXT("SetPostProcessHelp","Compile this blueprint and assign it as the preview mesh's Post Process Anim Blueprint. Supports Undo; save the mesh to keep the change."))
        .IsEnabled_Lambda([this]{return Blueprint()&&Mesh()&&Mesh()->GetSkeleton()&&Blueprint()->TargetSkeleton==Mesh()->GetSkeleton();})
        .OnClicked(this,&SFModelRestorePanel::SetPostProcess)];
    Form->AddSlot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("WiringHelp","New groups keep their internal connections and are placed below the existing graph. Connect their input and output to your pose flow. Graph insertion supports Undo; created assets remain in the Content Browser."))];
    ShowReport((bPhysics?LOCTEXT("Ready","Choose a JSON export and read its groups. The target follows this editor's preview mesh."):
        LOCTEXT("PsaReady","Choose abpp.json and its POSE folder, then read the groups. PSA animations will be imported automatically.")).ToString());
}
void SFModelRestorePanel::Tick(const FGeometry& Geometry,double Time,float Delta)
{
    SCompoundWidget::Tick(Geometry,Time,Delta);
    if (ObservedMesh.Get()!=Mesh())
    {
        ObservedMesh=Mesh(); Invalidate(true);
        ShowReport(LOCTEXT("MeshChanged","The preview mesh changed. Read the groups again to validate them against the new mesh.").ToString());
    }
}
void SFModelRestorePanel::Invalidate(bool bClearGroups)
{
    Batch.Reset();
    if (bClearGroups) {Rows.Reset(); if(GroupList) GroupList->ClearChildren();}
}
void SFModelRestorePanel::ShowReport(const FString& Text) {if(Output) Output->SetText(FText::FromString(Text));}
int32 SFModelRestorePanel::SelectedCount() const {int32 Count=0; for(const auto& Row:Rows) Count+=Row->bSelected?1:0; return Count;}
FReply SFModelRestorePanel::SelectAll(bool bSelected) {for(auto& Row:Rows)Row->bSelected=bSelected; Invalidate(false); return FReply::Handled();}
FReply SFModelRestorePanel::ReadGroups()
{
    Batch.Reset(); Options->TargetMesh=Mesh();
    if (!bPhysics) Options->Destination=UFModelAnimRestoreLibrary::DefaultPoseDestination(Blueprint());
    TArray<FFModelRestoreGroup> Groups; FString Report;
    if (UFModelAnimRestoreLibrary::Inspect(Options.Get(),Groups,Report))
    {
        TSet<FString> PreviousSelection;
        const bool bHadRows=!Rows.IsEmpty();
        for (const auto& Row:Rows) if (Row->bSelected) PreviousSelection.Add(Row->Group.Id);
        Invalidate(true); bNeedsRead=false;
        for (const auto& Group:Groups)
        {
            auto Row=MakeShared<FRow>(); Row->Group=Group; Rows.Add(Row);
            Row->bSelected=!bHadRows || PreviousSelection.Contains(Group.Id);
            if (!bPhysics)
            {
                BuildPoseRow(Row); continue;
            }
            BuildPhysicsRow(Row);
        }
    }
    else bNeedsRead=true;
    ShowReport(Report); return FReply::Handled();
}
FReply SFModelRestorePanel::Prepare()
{
    Batch.Reset(); Options->TargetMesh=Mesh();
    if (!bPhysics) Options->Destination=UFModelAnimRestoreLibrary::DefaultPoseDestination(Blueprint());
    TArray<FString> Selection; for (auto& Row:Rows) if(Row->bSelected)Selection.Add(Row->Group.Id);
    FScopedSlowTask Progress(1,LOCTEXT("Preparing","Preparing FModel animation groups...")); Progress.MakeDialog();
    FString Report; UFModelAnimRestoreBatch* Result=nullptr;
    if (UFModelAnimRestoreLibrary::PrepareForBlueprint(Options.Get(),Blueprint(),Selection,Result,Report))
    {
        Batch.Reset(Result);
        if (!bPhysics)
            for (const auto& Row:Rows)
            {
                if (const auto* Animation=Batch->PoseAnimations.Find(Row->Group.Id)) Row->Animation=Animation->Get();
                if (const auto* Pose=Batch->PoseAssets.Find(Row->Group.Id)) Row->Pose=Pose->Get();
            }
        Report+=TEXT("\n")+LOCTEXT("Prepared","Ready. Use 'Add nodes to this blueprint' to insert the selected groups.").ToString();
    }
    ShowReport(Report); return FReply::Handled();
}
FReply SFModelRestorePanel::AddNodes()
{
    FString Report; TArray<UEdGraphNode*> Nodes;
    if (UFModelAnimRestoreLibrary::AddToBlueprint(Batch.Get(),Blueprint(),Mesh(),Graph(),Nodes,Report))
        if (auto Pinned=Editor.Pin();Pinned && !Nodes.IsEmpty())Pinned->JumpToNode(Nodes[0]);
    ShowReport(Report); return FReply::Handled();
}
FReply SFModelRestorePanel::SetPostProcess()
{
    FString Report;
    UFModelAnimRestoreLibrary::SetAsPostProcess(Blueprint(),Mesh(),Report);
    ShowReport(Report); return FReply::Handled();
}
#undef LOCTEXT_NAMESPACE
