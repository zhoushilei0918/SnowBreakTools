#include "SFModelRestorePanel.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/PoseAsset.h"
#include "AnimationGraphSchema.h"
#include "AssetThumbnail.h"
#include "EdGraph/EdGraph.h"
#include "Engine/SkeletalMesh.h"
#include "IAnimationBlueprintEditor.h"
#include "Misc/Paths.h"
#include "PropertyCustomizationHelpers.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "DesktopPlatformModule.h"
#include "HAL/PlatformProcess.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FModelAnimRestore"

namespace
{
TSharedRef<SWidget> AssetSlot(const UClass* Class,const TSharedPtr<FAssetThumbnailPool>& Pool,TFunction<FString()> GetPath)
{
    return SNew(SObjectPropertyEntryBox).AllowedClass(Class)
        .ObjectPath_Lambda([GetPath]{return GetPath();}).ThumbnailPool(Pool).ThumbnailSizeOverride(FIntPoint(40,40))
        .AllowClear(false).AllowCreate(false).DisplayUseSelected(false).EnableContentPicker(false)
        .OnShouldSetAsset_Lambda([](const FAssetData&){return false;});
}
}

void SFModelRestorePanel::BuildPoseContext(const TSharedRef<SVerticalBox>& Form)
{
    ThumbnailPool=MakeShared<FAssetThumbnailPool>(24);
    auto ContextRow=[&](const FText& Label,const UClass* Class,TFunction<FString()> GetPath)
    {
        Form->AddSlot().AutoHeight().Padding(0,3)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(.22f).VAlign(VAlign_Center)[SNew(STextBlock).AutoWrapText(true).Text(Label)]
            +SHorizontalBox::Slot().FillWidth(.78f)[AssetSlot(Class,ThumbnailPool,GetPath)]];
    };
    ContextRow(LOCTEXT("CurrentBlueprint","Current blueprint"),UAnimBlueprint::StaticClass(),[this]{return Blueprint()?Blueprint()->GetPathName():FString();});
    ContextRow(LOCTEXT("PreviewMesh","Preview mesh (read only)"),USkeletalMesh::StaticClass(),[this]{return Mesh()?Mesh()->GetPathName():FString();});
    Form->AddSlot().AutoHeight().Padding(0,3)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(.22f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("TargetGraph","Target graph"))]
        +SHorizontalBox::Slot().FillWidth(.78f)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SComboButton).OnGetMenuContent(this,&SFModelRestorePanel::GraphMenu)
                .ButtonContent()[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth().Padding(4,0,8,0)[SNew(SImage).Image(FAppStyle::GetBrush("GraphEditor.EventGraph_16x"))]
                    +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Graph()?Graph()->GetName():TEXT("-"));})]]]
            +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SNew(SButton).ButtonStyle(FAppStyle::Get(),"HoverHintOnly")
                .ToolTipText(LOCTEXT("OpenTargetGraph","Open this graph"))
                .OnClicked_Lambda([this]{if(auto Pinned=Editor.Pin();Pinned&&Graph())Pinned->JumpToHyperlink(Graph(),false);return FReply::Handled();})
                [SNew(SImage).Image(FAppStyle::GetBrush("Icons.BrowseContent"))]]]];
}

TSharedRef<SWidget> SFModelRestorePanel::GraphMenu()
{
    FMenuBuilder Menu(true,nullptr);
    if(auto* BP=Blueprint())
    {
        TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
        for(auto* Candidate:Graphs)
            if(Candidate && Candidate->bEditable && Candidate->GetSchema()->IsA<UAnimationGraphSchema>())
            {
                TWeakObjectPtr<UEdGraph> Weak(Candidate);
                Menu.AddMenuEntry(FText::FromString(Candidate->GetName()),FText::GetEmpty(),FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this,Weak]{SelectedGraph=Weak;})));
            }
    }
    return Menu.MakeWidget();
}

void SFModelRestorePanel::BuildPoseFileOptions(const TSharedRef<SVerticalBox>& Form)
{
    for(bool Folder : {false,true})
    {
        auto GetPath=[this,Folder]{return Folder?Options->PoseDirectory.Path:Options->PoseBlueprintJson.FilePath;};
        Form->AddSlot().AutoHeight().Padding(0,8,0,0)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(.22f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Folder?
                LOCTEXT("PoseFolder","POSE folder"):LOCTEXT("PoseJson","Pose blueprint JSON (abpp)"))]
            +SHorizontalBox::Slot().FillWidth(.78f)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).HAlign(HAlign_Left)
                .ToolTipText(Folder?LOCTEXT("ChoosePoseFolder","Choose the folder containing the POSE PSA files and PoseAsset JSON files."):
                    LOCTEXT("ChooseAbpp","Choose the character's abpp.json file."))
                .Text_Lambda([this,Folder]{FString Path=Folder?Options->PoseDirectory.Path:Options->PoseBlueprintJson.FilePath;
                    FPaths::NormalizeDirectoryName(Path);
                    return Path.IsEmpty()?(Folder?LOCTEXT("SelectPoseFolder","Choose POSE folder..."):LOCTEXT("SelectAbpp","Choose abpp.json...")):
                        FText::FromString(FPaths::GetCleanFilename(Path));})
                .OnClicked_Lambda([this,Folder]
                {
                    auto* Desktop=FDesktopPlatformModule::Get(); if(!Desktop)return FReply::Handled();
                    const void* Parent=FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
                    FString Selected;
                    if(Folder)
                        Desktop->OpenDirectoryDialog(Parent,LOCTEXT("SelectPoseFolder","Choose POSE folder...").ToString(),Options->PoseDirectory.Path,Selected);
                    else
                    {
                        TArray<FString> Files;
                        if(Desktop->OpenFileDialog(Parent,LOCTEXT("SelectAbpp","Choose abpp.json...").ToString(),
                            FPaths::GetPath(Options->PoseBlueprintJson.FilePath),TEXT(""),TEXT("JSON (*.json)|*.json"),0,Files) && !Files.IsEmpty())Selected=Files[0];
                    }
                    if(!Selected.IsEmpty())
                    {
                        (Folder?Options->PoseDirectory.Path:Options->PoseBlueprintJson.FilePath)=Selected;
                        Invalidate(true);
                        ShowReport(LOCTEXT("PsaReady","Choose abpp.json and its POSE folder, then read the groups. PSA animations will be imported automatically.").ToString());
                    }
                    return FReply::Handled();
                })]
                +SHorizontalBox::Slot().AutoWidth().Padding(4,0,0,0)[SNew(SButton).ButtonStyle(FAppStyle::Get(),"HoverHintOnly")
                    .ToolTipText(Folder?LOCTEXT("OpenPoseFolder","Open the POSE folder in File Explorer"):LOCTEXT("RevealAbpp","Show the selected JSON in File Explorer"))
                    .IsEnabled_Lambda([GetPath,Folder]{const FString Path=GetPath();return !Path.IsEmpty() && (Folder?FPaths::DirectoryExists(Path):FPaths::FileExists(Path));})
                    .OnClicked_Lambda([GetPath]{const FString Path=GetPath();if(!Path.IsEmpty())FPlatformProcess::ExploreFolder(*FPaths::ConvertRelativePathToFull(Path));return FReply::Handled();})
                    [SNew(SImage).Image(FAppStyle::GetBrush("Icons.FolderOpen"))]]]];
        Form->AddSlot().AutoHeight().Padding(0,2,0,0)[SNew(SHorizontalBox)
            .Visibility_Lambda([GetPath]{return GetPath().IsEmpty()?EVisibility::Collapsed:EVisibility::Visible;})
            +SHorizontalBox::Slot().FillWidth(.22f)[SNullWidget::NullWidget]
            +SHorizontalBox::Slot().FillWidth(.78f)[SNew(SEditableTextBox).IsReadOnly(true)
                .Text_Lambda([GetPath]{return FText::FromString(FPaths::ConvertRelativePathToFull(GetPath()));})
                .ToolTipText_Lambda([GetPath]{return FText::FromString(FPaths::ConvertRelativePathToFull(GetPath()));})]];
    }
    Form->AddSlot().AutoHeight().Padding(0,8)[SNew(STextBlock).AutoWrapText(true)
        .Text(LOCTEXT("PsaOutputHelp","Animations and Pose Assets are saved in the POSE folder beside this blueprint. Existing assets are kept; new imports receive unique names."))];
}

void SFModelRestorePanel::BuildPoseRow(const TSharedRef<FRow>& Row)
{
    GroupList->AddSlot().AutoHeight().Padding(0,4)[SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8)
        [SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[SNew(SCheckBox)
                .IsChecked_Lambda([Row]{return Row->bSelected?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this,Row](ECheckBoxState State){Row->bSelected=State==ECheckBoxState::Checked;Invalidate(false);})
                [SNew(STextBlock).Text(FText::FromString(Row->Group.Bones))]]
            +SVerticalBox::Slot().AutoHeight().Padding(22,4,0,4)[SNew(STextBlock).AutoWrapText(true)
                .Text(FText::Format(LOCTEXT("PsaSourceFiles","{0} + {1}  ({2} poses)"),FText::FromString(FPaths::GetCleanFilename(Row->Group.PsaFile)),
                    FText::FromString(FPaths::GetCleanFilename(Row->Group.PoseJson)),FText::AsNumber(Row->Group.PoseCount)))]]];
    GroupList->AddSlot().AutoHeight().Padding(22,0,8,6)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,8,0)[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("ImportedPsa","Imported animation"))]
            +SVerticalBox::Slot().AutoHeight()[AssetSlot(UAnimSequence::StaticClass(),ThumbnailPool,[Row]{return Row->Animation.IsValid()?Row->Animation->GetPathName():FString();})]]
        +SHorizontalBox::Slot().FillWidth(1)[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("CreatedPoseAsset","Created Pose Asset"))]
            +SVerticalBox::Slot().AutoHeight()[AssetSlot(UPoseAsset::StaticClass(),ThumbnailPool,[Row]{return Row->Pose.IsValid()?Row->Pose->GetPathName():FString();})]]];
}

#undef LOCTEXT_NAMESPACE
