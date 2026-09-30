#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/EngineTypes.h"
#include "FModelAnimRestoreLibrary.generated.h"

class USkeletalMesh;
class UAnimSequence;
class UAnimBlueprint;
class UEdGraph;
class UEdGraphNode;
class UPoseAsset;

struct FMODELANIMRESTORE_API FFModelRestoreGroup
{
    FString Id;
    FString AssetName;
    FString Bones;
    int32 PoseCount = 0;
    bool bPhysics = false;
    FString PoseJson;
    FString PsaFile;
};

/** A prepared import is bound to one blueprint and one preview mesh. */
UCLASS(Transient)
class FMODELANIMRESTORE_API UFModelAnimRestoreBatch : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TObjectPtr<UAnimBlueprint> TargetBlueprint;
    UPROPERTY(Transient) TObjectPtr<USkeletalMesh> TargetMesh;
    UPROPERTY(Transient) TArray<TObjectPtr<UObject>> CreatedAssets;
    UPROPERTY(Transient) TObjectPtr<UAnimBlueprint> TemplateBlueprint;
    UPROPERTY(Transient) TArray<TObjectPtr<UEdGraphNode>> TemplateNodes;
    UPROPERTY(Transient) TMap<FString,TObjectPtr<UAnimSequence>> PoseAnimations;
    UPROPERTY(Transient) TMap<FString,TObjectPtr<UPoseAsset>> PoseAssets;
    int32 PoseDriverCount = 0;
    int32 PhysicsCount = 0;
    int32 ExpectedNodeCount = 0;
};

UCLASS(BlueprintType)
class FMODELANIMRESTORE_API UFModelAnimRestoreOptions : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="1. Assets", meta=(DisplayName="目标模型 / Target Mesh"))
    TObjectPtr<USkeletalMesh> TargetMesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="1. Assets", meta=(DisplayName="参考动作 / Preview Animation"))
    TObjectPtr<UAnimSequence> ReferenceAnimation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="2. FModel JSON", meta=(DisplayName="补偿蓝图 JSON / abpp", FilePathFilter="json"))
    FFilePath PoseBlueprintJson;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="2. FModel JSON", meta=(DisplayName="物理蓝图 JSON / ABP_Phy (可选)", FilePathFilter="json"))
    FFilePath PhysicsBlueprintJson;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="2. FModel JSON", meta=(DisplayName="POSE 文件夹 / POSE Folder"))
    FDirectoryPath PoseDirectory;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="3. Output", meta=(DisplayName="输出目录 / Content Folder"))
    FString Destination = TEXT("/Game/FModelRestored/Character");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="3. Output", meta=(DisplayName="挂到模型的 Post Process Anim Blueprint", ToolTip="仅在还原、编译和保存成功后设置；现有后处理不为空时拒绝覆盖。"))
    bool bAssignPostProcess = false;
};

UCLASS()
class FMODELANIMRESTORE_API UFModelAnimRestoreLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Creates native editable assets. Never overwrites an existing output asset. */
    UFUNCTION(BlueprintCallable, Category="FModel Animation Restore")
    static bool Restore(UFModelAnimRestoreOptions* Options, FString& Report);

    /** Read all groups without creating assets or changing a blueprint. */
    static bool Inspect(UFModelAnimRestoreOptions* Options, TArray<FFModelRestoreGroup>& Groups, FString& Report);

    static FString DefaultPoseDestination(const UAnimBlueprint* Blueprint);

    /** Create selected assets and a native graph fragment; never create another AnimBP. */
    static bool PrepareForBlueprint(UFModelAnimRestoreOptions* Options, UAnimBlueprint* Blueprint,
        const TArray<FString>& SelectedGroups, UFModelAnimRestoreBatch*& Batch, FString& Report);

    /** Add a disconnected, internally wired fragment in one undoable transaction. */
    static bool AddToBlueprint(UFModelAnimRestoreBatch* Batch, UAnimBlueprint* Blueprint,
        USkeletalMesh* CurrentPreviewMesh, UEdGraph* Graph, TArray<UEdGraphNode*>& AddedNodes, FString& Report);

    /** Compile and assign the current blueprint to the mesh; undoable, left dirty for normal editor saving. */
    static bool SetAsPostProcess(UAnimBlueprint* Blueprint, USkeletalMesh* Mesh, FString& Report);
};
