#pragma once
#include "Widgets/SCompoundWidget.h"
#include "FModelAnimRestoreLibrary.h"
#include "UObject/StrongObjectPtr.h"

class IAnimationBlueprintEditor;
class SVerticalBox;
class SMultiLineEditableTextBox;
class FAssetThumbnailPool;

/** One panel belongs to one asset editor; its target cannot be picked manually. */
class SFModelRestorePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SFModelRestorePanel) : _Physics(false) {}
        SLATE_ARGUMENT(TWeakPtr<IAnimationBlueprintEditor>, Editor)
        SLATE_ARGUMENT(bool, Physics)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual void Tick(const FGeometry&, double, float) override;
private:
    struct FRow { FFModelRestoreGroup Group; bool bSelected=true; TWeakObjectPtr<UAnimSequence> Animation; TWeakObjectPtr<UPoseAsset> Pose; };
    TWeakPtr<IAnimationBlueprintEditor> Editor;
    bool bPhysics=false;
    TWeakObjectPtr<USkeletalMesh> ObservedMesh;
    TStrongObjectPtr<UFModelAnimRestoreOptions> Options;
    TStrongObjectPtr<UFModelAnimRestoreBatch> Batch;
    TArray<TSharedPtr<FRow>> Rows;
    TSharedPtr<SVerticalBox> GroupList;
    TSharedPtr<SMultiLineEditableTextBox> Output;
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
    TWeakObjectPtr<UEdGraph> SelectedGraph;
    bool bNeedsRead = false;
    void BuildContext(const TSharedRef<SVerticalBox>& Form);
    void BuildFileOptions(const TSharedRef<SVerticalBox>& Form);
    void BuildPoseRow(const TSharedRef<FRow>& Row);
    void BuildPhysicsRow(const TSharedRef<FRow>& Row);
    TSharedRef<SWidget> GraphMenu();
    UAnimBlueprint* Blueprint() const;
    USkeletalMesh* Mesh() const;
    UEdGraph* Graph() const;
    void Invalidate(bool bClearGroups);
    void ShowReport(const FString& Text);
    FReply ReadGroups();
    FReply Prepare();
    FReply AddNodes();
    FReply SetPostProcess();
    FReply SelectAll(bool bSelected);
    int32 SelectedCount() const;
};
