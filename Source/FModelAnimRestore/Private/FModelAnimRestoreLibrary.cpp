#include "FModelAnimRestoreLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/PoseAsset.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_PoseDriver.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimNode_KawaiiPhysics.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetCompilingManager.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/AnimBlueprintFactory.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "UObject/GCObject.h"
#include "UObject/StrongObjectPtr.h"
#include "AnimationGraphSchema.h"
#include "ScopedTransaction.h"
#include "Misc/Guid.h"
#include "PsaImporter.h"
#include "PsaReader.h"
#include "AssetToolsModule.h"
#include "ObjectTools.h"

namespace FModelRestore
{
using FObject = TSharedPtr<FJsonObject>;
using FValues = TArray<TSharedPtr<FJsonValue>>;

static FObject Object(const FObject& J, const FString& Key)
{
    const FObject* V = nullptr;
    return J && J->TryGetObjectField(Key, V) ? *V : nullptr;
}
static const FValues& Array(const FObject& J, const FString& Key)
{
    static FValues Empty;
    const FValues* V = nullptr;
    return J && J->TryGetArrayField(Key, V) ? *V : Empty;
}
static FString String(const FObject& J, const FString& Key, const FString& Default = TEXT(""))
{
    FString V;
    return J && J->TryGetStringField(Key, V) ? V : Default;
}
static double Number(const FObject& J, const FString& Key, double Default = 0)
{
    double V;
    return J && J->TryGetNumberField(Key, V) ? V : Default;
}
static FString JsonText(const FObject& J)
{
    FString S;
    FJsonSerializer::Serialize(J.ToSharedRef(), TJsonWriterFactory<>::Create(&S));
    return S;
}
static FString ObjectName(const FObject& Ref)
{
    FString Name = String(Ref, TEXT("ObjectName"));
    int32 Quote;
    if (Name.FindChar('\'', Quote)) Name = Name.Mid(Quote + 1).Replace(TEXT("'"), TEXT(""));
    return Name;
}

struct FNode
{
    FString Name;
    FObject Settings;
};
struct FGraph
{
    FObject Class;
    TArray<FNode> Ordered;
};
struct FPosePlan
{
    FNode Source;
    FAnimNode_PoseDriver Driver;
    FString AssetName;
    FString PoseJson;
    FString PosePackage;
    FString SamplesPackage;
    TArray<FName> Names;
    FString PsaFile;
    TObjectPtr<UAnimSequence> Animation = nullptr;
    TObjectPtr<UPoseAsset> Asset = nullptr;
};
struct FPhysicsPlan
{
    FNode Source;
    FAnimNode_KawaiiPhysics Node;
};

struct FImporter : public FGCObject
{
    UFModelAnimRestoreOptions& Options;
    FString Error;
    TArray<FString> Notes;
    FValues Unsupported;
    TArray<TObjectPtr<UObject>> Assets;
    TArray<FPosePlan> Poses;
    TArray<FPhysicsPlan> Physics;
    FString Character;
    FString ReportFile;
    FGraph PoseGraph;
    FGraph PhysicsGraph;
    bool bFragmentMode = false;

    explicit FImporter(UFModelAnimRestoreOptions& InOptions) : Options(InOptions) {}
    virtual FString GetReferencerName() const override { return TEXT("FModelAnimRestoreImporter"); }
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override
    {
        Collector.AddReferencedObjects(Assets);
        for (auto& P : Poses) { Collector.AddReferencedObject(P.Asset); Collector.AddReferencedObject(P.Animation); }
    }
    bool Fail(const FString& Why) { if (Error.IsEmpty()) Error = Why; return false; }
    bool Load(const FString& Filename, FValues& Exports)
    {
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *Filename)) return Fail(NSLOCTEXT("FModelAnimRestore","CannotReadJson","Cannot read JSON: ").ToString() + Filename);
        auto Reader = TJsonReaderFactory<>::Create(Text);
        if (!FJsonSerializer::Deserialize(Reader, Exports)) return Fail(NSLOCTEXT("FModelAnimRestore","ExpectedArray","Expected an exported FModel JSON array: ").ToString() + Filename);
        return true;
    }
    FObject Export(const FValues& Items, const FString& Type)
    {
        for (auto& V : Items) if (V->Type == EJson::Object && String(V->AsObject(), TEXT("Type")) == Type) return V->AsObject();
        return nullptr;
    }
    bool ReadGraph(const FString& Filename, FGraph& Out)
    {
        FValues Exports;
        if (!Load(Filename, Exports)) return false;
        Out.Class = Export(Exports, TEXT("AnimBlueprintGeneratedClass"));
        FObject Defaults;
        for (auto& V : Exports)
            if (V->Type == EJson::Object && String(V->AsObject(), TEXT("Name")).StartsWith(TEXT("Default__")))
                Defaults = Object(V->AsObject(), TEXT("Properties"));
        if (!Out.Class || !Defaults) return Fail(NSLOCTEXT("FModelAnimRestore","NoDefaults","JSON is missing its generated class or Default__ settings: ").ToString() + Filename);
        // LinkID indexes follow the serialized ChildProperties, not the numeric suffix of a node name.
        TArray<FNode> Nodes;
        for (auto& V : Array(Out.Class, TEXT("ChildProperties")))
        {
            auto P = V->AsObject();
            FString Name = String(P, TEXT("Name"));
            if (!ObjectName(Object(P, TEXT("Struct"))).StartsWith(TEXT("AnimNode_"))) continue;
            auto Settings = Object(Defaults, Name);
            if (!Settings) return Fail(NSLOCTEXT("FModelAnimRestore","NoNodeDefaults","Node default data is missing: ").ToString() + Name);
            Nodes.Add({Name, Settings});
        }
        int32 Current = Nodes.IndexOfByPredicate([](const FNode& N){ return N.Name == TEXT("AnimGraphNode_Root"); });
        TSet<int32> Seen;
        bool Boundary = false;
        while (Nodes.IsValidIndex(Current))
        {
            if (Seen.Contains(Current)) return Fail(NSLOCTEXT("FModelAnimRestore","LinkCycle","A cycle was found in blueprint LinkIDs: ").ToString() + Filename);
            Seen.Add(Current);
            const FNode& N = Nodes[Current];
            if (N.Name.Contains(TEXT("LinkedInputPose")) || N.Name.Contains(TEXT("LinkedAnimGraph")) || N.Name.Contains(TEXT("CopyPoseFromMesh")))
            { Boundary = true; break; }
            if (N.Name.Contains(TEXT("PoseDriver")) || N.Name.Contains(TEXT("KawaiiPhysics"))) Out.Ordered.Insert(N, 0);
            else if (!(N.Name.Contains(TEXT("Root")) || N.Name.Contains(TEXT("LocalToComponentSpace")) || N.Name.Contains(TEXT("ComponentToLocalSpace")) || N.Name.Contains(TEXT("Inertialization"))))
                return Fail(NSLOCTEXT("FModelAnimRestore","UnsupportedChain","The pose chain contains an unsupported node: ").ToString() + N.Name);
            if (N.Name.Contains(TEXT("Inertialization"))) Notes.AddUnique(NSLOCTEXT("FModelAnimRestore","InertializationNote","Inertialization and state-transition logic are outside static physics import and were not copied.").ToString());
            FObject Link;
            for (const FString Key : {TEXT("Result"),TEXT("Source"),TEXT("SourcePose"),TEXT("ComponentPose"),TEXT("LocalPose")})
                if (Object(N.Settings, Key)) { Link = Object(N.Settings, Key); break; }
            Current = int32(Number(Link, TEXT("LinkID"), -1));
        }
        if (!Boundary || Out.Ordered.IsEmpty()) return Fail(NSLOCTEXT("FModelAnimRestore","InputNotFound","Cannot follow Root LinkIDs to the input pose: ").ToString() + Filename);
        return true;
    }
    bool Bone(FName Name, const FString& Field, bool AllowNone = false)
    {
        if (Name.IsNone() && AllowNone) return true;
        if (Options.TargetMesh->GetRefSkeleton().FindBoneIndex(Name) == INDEX_NONE)
            return Fail(Field + NSLOCTEXT("FModelAnimRestore","MissingBone"," references a missing bone: ").ToString() + Name.ToString());
        return true;
    }
    bool Convert(const FObject& J, const UStruct* Type, void* Data, const FString& Context)
    {
        if (!J) return Fail(NSLOCTEXT("FModelAnimRestore","MissingJsonObject","Missing JSON object: ").ToString() + Context);
        FText Why;
        if (!FJsonObjectConverter::JsonObjectToUStruct(J.ToSharedRef(),Type,Data,0,0,false,&Why))
            return Fail(Context + TEXT(": ") + Why.ToString());
        return true;
    }
    bool PlanPose(const FNode& N)
    {
        FPosePlan P; P.Source=N;
        auto Clean=MakeShared<FJsonObject>();
        const TSet<FString> Fields={TEXT("SourceBones"),TEXT("EvalSpaceBone"),TEXT("bEvalFromRefPose"),TEXT("OnlyDriveBones"),TEXT("PoseTargets"),TEXT("RBFParams"),TEXT("DriveSource"),TEXT("DriveOutput"),TEXT("LODThreshold")};
        for (auto& Pair : N.Settings->Values) if (Fields.Contains(FString(Pair.Key))) Clean->SetField(Pair.Key,Pair.Value);
        if (!Convert(Clean,FAnimNode_PoseDriver::StaticStruct(),&P.Driver,N.Name)) return false;
        if (P.Driver.DriveOutput != EPoseDriverOutput::DrivePoses) return Fail(NSLOCTEXT("FModelAnimRestore","DriveCurvesUnsupported","DrivePoses is supported; DriveCurves is not: ").ToString()+N.Name);
        if (P.Driver.SourceBones.IsEmpty() || P.Driver.PoseTargets.IsEmpty()) return Fail(NSLOCTEXT("FModelAnimRestore","NoPoseTargets","Pose Driver is missing SourceBones or Targets.").ToString());
        TArray<FName> RequiredBones;
        for (auto& B : P.Driver.SourceBones) RequiredBones.AddUnique(B.BoneName);
        for (auto& B : P.Driver.OnlyDriveBones) RequiredBones.AddUnique(B.BoneName);
        if (!P.Driver.EvalSpaceBone.BoneName.IsNone()) RequiredBones.AddUnique(P.Driver.EvalSpaceBone.BoneName);
        for (FName B : RequiredBones) if (!Bone(B,N.Name)) return false;

        P.AssetName=ObjectName(Object(N.Settings,TEXT("PoseAsset")));
        if (P.AssetName.IsEmpty() || ObjectTools::SanitizeObjectName(P.AssetName)!=P.AssetName)
            return Fail(NSLOCTEXT("FModelAnimRestore","InvalidPoseReference","The abpp contains an invalid Pose Asset name.").ToString());
        P.PoseJson=Options.PoseDirectory.Path/(P.AssetName+TEXT(".json"));
        FValues Exports;
        if (!Load(P.PoseJson,Exports)) return false;
        auto PoseExport=Export(Exports,TEXT("PoseAsset"));
        auto Props=Object(PoseExport,TEXT("Properties"));
        auto Container=Object(Props,TEXT("PoseContainer"));
        if (!Container || String(PoseExport,TEXT("Name"))!=P.AssetName)
            return Fail(NSLOCTEXT("FModelAnimRestore","WrongPoseFile","Missing or mismatched Pose Asset data: ").ToString()+FPaths::GetCleanFilename(P.PoseJson));
        bool Additive=false; Props->TryGetBoolField(TEXT("bAdditivePose"),Additive);
        if (Additive) return Fail(NSLOCTEXT("FModelAnimRestore","PsaAdditiveUnsupported","Additive Pose Assets are not supported by this PSA import: ").ToString()+P.AssetName);
        TSet<FName> UniqueNames;
        for (auto& V : Array(Container,TEXT("PoseNames")))
        {
            FName Name(*(V->Type==EJson::String ? V->AsString() : String(V->AsObject(),TEXT("DisplayName"))));
            if (Name.IsNone() || UniqueNames.Contains(Name)) return Fail(NSLOCTEXT("FModelAnimRestore","DuplicatePoseName","A pose name is empty or duplicated: ").ToString()+P.AssetName);
            UniqueNames.Add(Name); P.Names.Add(Name);
        }
        if (P.Names.IsEmpty()) return Fail(NSLOCTEXT("FModelAnimRestore","NoPoseNames","The Pose Asset JSON has no pose names: ").ToString()+P.AssetName);
        for (const auto& Target : P.Driver.PoseTargets)
            if (!P.Names.Contains(Target.DrivenName) || Target.BoneTransforms.Num()!=P.Driver.SourceBones.Num())
                return Fail(NSLOCTEXT("FModelAnimRestore","TargetMismatch","PoseTarget does not match the pose name or source-bone count: ").ToString()+Target.DrivenName.ToString());

        FString AnimationName=ObjectName(Object(Props,TEXT("SourceAnimation")));
        if (AnimationName.IsEmpty()) { AnimationName=P.AssetName; AnimationName.RemoveFromEnd(TEXT("_PoseAsset")); }
        if (ObjectTools::SanitizeObjectName(AnimationName)!=AnimationName)
            return Fail(NSLOCTEXT("FModelAnimRestore","InvalidPoseReference","The abpp contains an invalid Pose Asset name.").ToString());
        P.PsaFile=Options.PoseDirectory.Path/(AnimationName+TEXT(".psa"));
        // UnrealPSKPSA validates PSA bone names/parents directly against the FBX mesh.
        // No source Skeleton JSON or FModel directory layout is involved.
        FPsaReader Reader(P.PsaFile);
        TArray<FPsaBoneMapping> Mapping; FString PsaError;
        if (!FPsaImporter::MatchBones(Reader,Options.TargetMesh,Mapping,PsaError))
            return Fail(FPaths::GetCleanFilename(P.PsaFile)+TEXT(": ")+PsaError);
        if (Reader.Sequences.Num()!=1 || Reader.Sequences[0].NumRawFrames!=P.Names.Num())
            return Fail(FText::Format(NSLOCTEXT("FModelAnimRestore","PsaPoseCount","{0} must contain one animation with {1} sampled poses matching the Pose Asset JSON."),FText::FromString(FPaths::GetCleanFilename(P.PsaFile)),FText::AsNumber(P.Names.Num())).ToString());
        for (FName B : RequiredBones)
            if (!Mapping.ContainsByPredicate([&](const FPsaBoneMapping& M){return Options.TargetMesh->GetRefSkeleton().GetBoneName(M.TargetIndex)==B;}))
                return Fail(NSLOCTEXT("FModelAnimRestore","PsaRequiredBone","The PSA is missing a required compensation bone: ").ToString()+B.ToString());
        Poses.Add(MoveTemp(P)); return true;
    }
    void RecordUnsupported(const FString& Path, const TSharedPtr<FJsonValue>& Value)
    {
        auto Entry=MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("field"),Path); Entry->SetField(TEXT("source_value"),Value);
        Unsupported.Add(MakeShared<FJsonValueObject>(Entry));
    }
    FObject PhysicsFields(const FObject& Input,const UStruct* Type,const FString& Path)
    {
        auto Clean=MakeShared<FJsonObject>();
        const TSet<FString> Runtime={TEXT("ComponentPose"),TEXT("ActualAlpha"),TEXT("WorkingPhysicsSettings"),TEXT("WorkingState"),TEXT("ModifyBones"),TEXT("TotalBoneLength"),TEXT("PreSkelCompTransform"),TEXT("bInitPhysicsSettings"),TEXT("bInitExternalPhysicsSettings"),TEXT("BoneMovementScale_Working"),TEXT("bBoneWaveEnableWave_Working"),TEXT("WaveDuration_Working"),TEXT("WaveScale_Working"),TEXT("BaseWaveScaleRange_Working"),TEXT("AlphaBlend"),TEXT("Location"),TEXT("Rotation"),TEXT("Plane"),TEXT("IsNormalized"),TEXT("Size"),TEXT("SizeSquared"),TEXT("bInitialized")};
        for (auto& Pair : Input->Values)
        {
            if (Runtime.Contains(FString(Pair.Key))) continue;
            FProperty* Prop=FindFProperty<FProperty>(Type,*Pair.Key);
            if (!Prop || Prop->HasAnyPropertyFlags(CPF_Transient))
            {
                // Null external resources do not need migration; retain custom scalar settings in the report.
                if (Pair.Value->Type!=EJson::Null) RecordUnsupported(Path+TEXT(".")+Pair.Key,Pair.Value);
                continue;
            }
            if (CastField<FObjectPropertyBase>(Prop))
            {
                if (Pair.Value->Type!=EJson::Null) RecordUnsupported(Path+TEXT(".")+Pair.Key,Pair.Value);
                continue;
            }
            if (auto Struct=CastField<FStructProperty>(Prop); Struct && Pair.Value->Type==EJson::Object)
                Clean->SetObjectField(Pair.Key,PhysicsFields(Pair.Value->AsObject(),Struct->Struct,Path+TEXT(".")+Pair.Key));
            else if (auto Arr=CastField<FArrayProperty>(Prop); Arr && Pair.Value->Type==EJson::Array && CastField<FStructProperty>(Arr->Inner))
            {
                auto Inner=CastFieldChecked<FStructProperty>(Arr->Inner);
                FValues Values;
                int32 I=0;
                for (auto& V : Pair.Value->AsArray())
                {
                    if (V->Type!=EJson::Object) { Fail(NSLOCTEXT("FModelAnimRestore","InvalidPhysicsArray","Invalid physics array format: ").ToString()+Path); break; }
                    Values.Add(MakeShared<FJsonValueObject>(PhysicsFields(V->AsObject(),Inner->Struct,Path+FString::Printf(TEXT(".%s[%d]"),*Pair.Key,I++))));
                }
                Clean->SetArrayField(Pair.Key,Values);
            }
            else Clean->SetField(Pair.Key,Pair.Value);
        }
        return Clean;
    }
    bool PlanPhysics(const FNode& N)
    {
        FPhysicsPlan P; P.Source=N;
        auto Clean=PhysicsFields(N.Settings,FAnimNode_KawaiiPhysics::StaticStruct(),N.Name);
        if (!Error.IsEmpty() || !Convert(Clean,FAnimNode_KawaiiPhysics::StaticStruct(),&P.Node,N.Name)) return false;
        // Old UE4 versions applied acceleration through half * gravity * dt^2.
        if (!N.Settings->HasField(TEXT("bUseLegacyGravity"))) P.Node.bUseLegacyGravity=true;
        if (!Bone(P.Node.RootBone.BoneName,N.Name)) return false;
        for (auto& B : P.Node.ExcludeBones) if (!Bone(B.BoneName,N.Name)) return false;
        for (auto& L : P.Node.SphericalLimits) if (!Bone(L.DrivingBone.BoneName,N.Name,true)) return false;
        for (auto& L : P.Node.CapsuleLimits) if (!Bone(L.DrivingBone.BoneName,N.Name,true)) return false;
        for (auto& L : P.Node.PlanarLimits) if (!Bone(L.DrivingBone.BoneName,N.Name,true)) return false;
        Physics.Add(MoveTemp(P));
        return true;
    }
    bool Plan(bool bCheckOutputs = true)
    {
        if (!Options.TargetMesh || !Options.TargetMesh->GetSkeleton()) return Fail(NSLOCTEXT("FModelAnimRestore","NeedMesh","A mesh with a Skeleton is required.").ToString());
        if (Options.ReferenceAnimation && Options.ReferenceAnimation->GetSkeleton()!=Options.TargetMesh->GetSkeleton()) return Fail(NSLOCTEXT("FModelAnimRestore","AnimationSkeletonMismatch","The preview animation must use the same Skeleton as the target mesh.").ToString());
        if (Options.PoseBlueprintJson.FilePath.IsEmpty() && Options.PhysicsBlueprintJson.FilePath.IsEmpty()) return Fail(NSLOCTEXT("FModelAnimRestore","NeedJson","Choose a pose or physics blueprint JSON.").ToString());
        if (!Options.Destination.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Options.Destination)) return Fail(NSLOCTEXT("FModelAnimRestore","InvalidDestination","The output folder must be a valid /Game/... path.").ToString());
        if (!Options.PoseBlueprintJson.FilePath.IsEmpty() && (Options.PoseDirectory.Path.IsEmpty() || !IFileManager::Get().DirectoryExists(*Options.PoseDirectory.Path)))
            return Fail(NSLOCTEXT("FModelAnimRestore","NeedPoseFolder","Choose the folder containing the POSE PSA files and PoseAsset JSON files.").ToString());
        Character=FPaths::GetBaseFilename(Options.PoseBlueprintJson.FilePath.IsEmpty()?Options.PhysicsBlueprintJson.FilePath:Options.PoseBlueprintJson.FilePath);
        Character.RemoveFromEnd(TEXT("_abpp"));
        if (Options.bAssignPostProcess && Options.TargetMesh->GetPostProcessAnimBlueprint()) return Fail(NSLOCTEXT("FModelAnimRestore","ExistingPostProcess","The mesh already has a Post Process Anim Blueprint. Disable assignment and merge the result manually.").ToString());
        if (!Options.PoseBlueprintJson.FilePath.IsEmpty())
        {
            if (!ReadGraph(Options.PoseBlueprintJson.FilePath,PoseGraph)) return false;
            for (auto& N : PoseGraph.Ordered)
                if (!N.Name.Contains(TEXT("PoseDriver")) || !PlanPose(N)) return Fail(NSLOCTEXT("FModelAnimRestore","UnexpectedPoseNode","The pose JSON contains an unsupported non-PoseDriver node.").ToString());
        }
        if (!Options.PhysicsBlueprintJson.FilePath.IsEmpty())
        {
            // Phy.json contains the node settings. Bone references are checked by
            // PlanPhysics against the current mesh; no external Skeleton JSON is needed.
            if (!ReadGraph(Options.PhysicsBlueprintJson.FilePath,PhysicsGraph)) return false;
            for (auto& N : PhysicsGraph.Ordered)
                if (!N.Name.Contains(TEXT("KawaiiPhysics")) || !PlanPhysics(N)) return Fail(NSLOCTEXT("FModelAnimRestore","UnexpectedPhysicsNode","The physics JSON contains an unsupported non-KawaiiPhysics node.").ToString());
        }
        return !bCheckOutputs || CheckOutputNames();
    }
    bool CheckOutputNames()
    {
        TArray<FString> Packages;
        if (!bFragmentMode)
        {
            Packages.Add(Options.Destination / (Character+TEXT("_Restored")));
            if (Options.ReferenceAnimation) Packages.Add(Options.Destination / (Character+TEXT("_Preview")));
        }
        TSet<FString> Seen;
        for (const auto& Package : Packages)
        {
            if (!Package.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Package))
                return Fail(NSLOCTEXT("FModelAnimRestore","InvalidAssetPackage","Enter a full asset package path without an extension, for example /Game/Character/POSE/MyPose: ").ToString()+Package);
            if (Seen.Contains(Package.ToLower()))
                return Fail(NSLOCTEXT("FModelAnimRestore","DuplicateOutputPath","Two outputs use the same asset path: ").ToString()+Package);
            Seen.Add(Package.ToLower());
            if (FPackageName::DoesPackageExist(Package) || StaticFindObject(UObject::StaticClass(),nullptr,*(Package+TEXT(".")+FPackageName::GetLongPackageAssetName(Package))))
                return Fail(NSLOCTEXT("FModelAnimRestore","OutputExists","An output asset already exists. Choose a new output folder: ").ToString()+Package);
        }
        return true;
    }
    template<typename T> T* NewAsset(const FString& PackagePath)
    {
        UPackage* Package=CreatePackage(*PackagePath);
        T* Asset=NewObject<T>(Package,*FPackageName::GetLongPackageAssetName(PackagePath),RF_Public|RF_Standalone|RF_Transactional);
        Assets.Add(Asset);
        return Asset;
    }
    bool BuildPoses()
    {
        for (auto& P : Poses)
        {
            FPsaImportOptions ImportOptions;
            ImportOptions.bSaveAssets=false;
            TArray<UAnimSequence*> Imported; TArray<FString> Warnings; FString Summary,PsaError;
            const bool ImportedOK=FPsaImporter::ImportFile(P.PsaFile,Options.TargetMesh,Options.Destination,ImportOptions,Imported,Summary,PsaError,&Warnings);
            for (auto* Animation:Imported) Assets.Add(Animation);
            if (!ImportedOK) return Fail(PsaError);
            if (Imported.Num()!=1) return Fail(NSLOCTEXT("FModelAnimRestore","PsaImportCount","Expected one imported animation for each compensation group.").ToString());
            P.Animation=Imported[0]; P.SamplesPackage=P.Animation->GetOutermost()->GetName();
            for (const auto& Warning:Warnings) Notes.AddUnique(Warning);
            FString PoseName;
            FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateUniqueAssetName(Options.Destination/P.AssetName,TEXT(""),P.PosePackage,PoseName);
            UPoseAsset* Pose=NewAsset<UPoseAsset>(P.PosePackage);
            Pose->SetSkeleton(Options.TargetMesh->GetSkeleton()); Pose->SetPreviewMesh(Options.TargetMesh);
            Pose->CreatePoseFromAnimation(P.Animation,&P.Names);
            TArray<FName> Result=Pose->GetPoseFNames();
            if (P.Names.Num()==1 && Result.Num()>1) Pose->DeletePoses({Result[1]});
            if (Pose->GetPoseFNames()!=P.Names) return Fail(NSLOCTEXT("FModelAnimRestore","PoseNamesChanged","Generated pose names differ from the source: ").ToString()+P.AssetName);
            P.Asset=Pose; P.Driver.PoseAsset=Pose;
        }
        return true;
    }
    static UEdGraphPin* PosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
    {
        for (auto* Pin : Node->Pins)
            if (Pin->Direction==Direction && Pin->PinType.PinCategory==TEXT("struct") && Pin->PinType.PinSubCategoryObject.IsValid()
                && (Pin->PinType.PinSubCategoryObject->GetName()==TEXT("PoseLink") || Pin->PinType.PinSubCategoryObject->GetName()==TEXT("ComponentSpacePoseLink"))) return Pin;
        return nullptr;
    }
    bool Link(UEdGraphNode* A, UEdGraphNode* B)
    {
        auto* From=PosePin(A,EGPD_Output); auto* To=PosePin(B,EGPD_Input);
        if (!From || !To || !A->GetGraph()->GetSchema()->TryCreateConnection(From,To)) return Fail(NSLOCTEXT("FModelAnimRestore","LinkFailed","Failed to connect pose pins: ").ToString()+A->GetName()+TEXT(" -> ")+B->GetName());
        return true;
    }
    template<typename T> T* Node(UEdGraph* G, int32 X)
    {
        FGraphNodeCreator<T> Creator(*G); T* N=Creator.CreateNode(); N->NodePosX=X; N->NodePosY=0; Creator.Finalize(); return N;
    }
    UAnimBlueprint* BuildGraph(bool Preview)
    {
        FString Name=Character+(Preview?TEXT("_Preview"):TEXT("_Restored"));
        TStrongObjectPtr<UAnimBlueprintFactory> Factory(NewObject<UAnimBlueprintFactory>());
        Factory->TargetSkeleton=Options.TargetMesh->GetSkeleton(); Factory->PreviewSkeletalMesh=Options.TargetMesh; Factory->ParentClass=UAnimInstance::StaticClass();
        TStrongObjectPtr<UPackage> Package(bFragmentMode ? GetTransientPackage() : CreatePackage(*(Options.Destination/Name)));
        const FName ObjectName = bFragmentMode ? MakeUniqueObjectName(Package.Get(),UAnimBlueprint::StaticClass(),TEXT("FModelImportFragment")) : FName(*Name);
        const EObjectFlags Flags = bFragmentMode ? RF_Transient|RF_Transactional : RF_Public|RF_Standalone|RF_Transactional;
        UAnimBlueprint* BP=Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),Package.Get(),ObjectName,Flags,nullptr,GWarn));
        if (!BP) { Fail(NSLOCTEXT("FModelAnimRestore","BlueprintCreateFailed","Failed to create an Animation Blueprint.").ToString()); return nullptr; }
        Assets.Add(BP);
        UEdGraph* G=nullptr;
        for (UEdGraph* Graph : BP->FunctionGraphs) if (Graph->GetFName()==TEXT("AnimGraph")) G=Graph;
        if (!G) { Fail(NSLOCTEXT("FModelAnimRestore","NoAnimGraph","The new blueprint has no AnimGraph.").ToString()); return nullptr; }
        UAnimGraphNode_Root* RootNode=nullptr;
        for (UEdGraphNode* N : G->Nodes) if (auto* R=Cast<UAnimGraphNode_Root>(N)) RootNode=R;
        if (!RootNode) { Fail(NSLOCTEXT("FModelAnimRestore","NoOutputPose","The new blueprint has no Output Pose.").ToString()); return nullptr; }
        int32 X=0;
        UEdGraphNode* Last=nullptr;
        if (Preview)
        {
            auto* N=Node<UAnimGraphNode_SequencePlayer>(G,X);
            N->Node.SetSequence(Options.ReferenceAnimation); N->Node.SetLoopAnimation(true); Last=N; N->ReconstructNode();
        }
        else
        {
            auto* N=Node<UAnimGraphNode_LinkedInputPose>(G,X); N->Node.Name=TEXT("InPose"); N->Node.Graph=TEXT("AnimGraph"); N->ReconstructNode(); Last=N;
        }
        if (!Physics.IsEmpty())
        {
            auto* ToComponent=Node<UAnimGraphNode_LocalToComponentSpace>(G,X+=260);
            if (!Link(Last,ToComponent)) return nullptr; Last=ToComponent;
            UClass* KawaiiClass=LoadObject<UClass>(nullptr,TEXT("/Script/KawaiiPhysicsEd.AnimGraphNode_KawaiiPhysics"));
            auto* Settings=KawaiiClass?FindFProperty<FStructProperty>(KawaiiClass,TEXT("Node")):nullptr;
            if (!Settings || Settings->Struct!=FAnimNode_KawaiiPhysics::StaticStruct()) { Fail(NSLOCTEXT("FModelAnimRestore","KawaiiClassMissing","Cannot load the KawaiiPhysicsEd node class.").ToString()); return nullptr; }
            for (auto& P : Physics)
            {
                // The upstream editor class has no DLL export macro; construct it through reflection.
                auto* N=NewObject<UEdGraphNode>(G,KawaiiClass,NAME_None,RF_Transactional);
                G->AddNode(N,false,false); N->CreateNewGuid(); N->NodePosX=(X+=260);
                Settings->CopyCompleteValue(Settings->ContainerPtrToValuePtr<void>(N),&P.Node);
                N->PostPlacedNewNode(); N->AllocateDefaultPins(); N->NodeComment=P.Source.Name; N->bCommentBubbleVisible=true;
                if (!Link(Last,N)) return nullptr; Last=N;
            }
            auto* ToLocal=Node<UAnimGraphNode_ComponentToLocalSpace>(G,X+=260);
            if (!Link(Last,ToLocal)) return nullptr; Last=ToLocal;
        }
        for (auto& P : Poses)
        {
            auto* N=Node<UAnimGraphNode_PoseDriver>(G,X+=320); N->Node=P.Driver; N->ReconstructNode();
            N->NodeComment=P.Source.Name; N->bCommentBubbleVisible=true;
            if (!Link(Last,N)) return nullptr; Last=N;
        }
        RootNode->NodePosX=X+320;
        if (!Link(Last,RootNode)) return nullptr;
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
        FCompilerResultsLog Log;
        FKismetEditorUtilities::CompileBlueprint(BP,EBlueprintCompileOptions::None,&Log);
        if (Log.NumErrors>0 || BP->Status==BS_Error) { Fail(NSLOCTEXT("FModelAnimRestore","CompileFailed","The generated graph failed to compile. Check the Output Log.").ToString()); return nullptr; }
        if (Log.NumWarnings>0) Notes.Add(FText::Format(NSLOCTEXT("FModelAnimRestore","CompileWarnings","{0} compiled with {1} warnings. Check the Output Log."),FText::FromString(Name),FText::AsNumber(Log.NumWarnings)).ToString());
        return BP;
    }
    bool Save(UObject* Asset)
    {
        FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
        FString Filename=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
        if (!UPackage::SavePackage(Asset->GetOutermost(),Asset,*Filename,Args)) return Fail(NSLOCTEXT("FModelAnimRestore","SaveFailed","Failed to save: ").ToString()+Filename);
        if (Assets.Contains(Asset) && !Asset->IsA<UAnimSequence>()) FAssetRegistryModule::AssetCreated(Asset);
        return true;
    }
    bool Run()
    {
        if (!Plan() || !BuildPoses()) return false;
        UAnimBlueprint* Restored=BuildGraph(false);
        if (!Restored || (Options.ReferenceAnimation && !BuildGraph(true))) return false;
        FAssetCompilingManager::Get().FinishAllCompilation();
        for (const auto& Asset : Assets) if (!Save(Asset.Get())) return false;
        if (Options.bAssignPostProcess)
        {
            Options.TargetMesh->Modify();
            Options.TargetMesh->SetPostProcessAnimBlueprint(Cast<UAnimBlueprintGeneratedClass>(Restored->GeneratedClass));
            Options.TargetMesh->MarkPackageDirty();
            if (!Save(Options.TargetMesh)) return false;
        }
        return true;
    }
    FString Report(bool Success)
    {
        auto J=MakeShared<FJsonObject>();
        J->SetBoolField(TEXT("success"),Success); J->SetStringField(TEXT("error"),Error);
        J->SetStringField(TEXT("target_mesh"),Options.TargetMesh?Options.TargetMesh->GetPathName():TEXT(""));
        J->SetStringField(TEXT("abpp_json"),Options.PoseBlueprintJson.FilePath);
        J->SetStringField(TEXT("physics_json"),Options.PhysicsBlueprintJson.FilePath);
        J->SetStringField(TEXT("physics_bone_validation"),TEXT("current_mesh_bone_names"));
        J->SetStringField(TEXT("pose_folder"),Options.PoseDirectory.Path);
        FValues PoseInputs;
        for (const auto& P : Poses)
        {
            auto Input=MakeShared<FJsonObject>();
            Input->SetStringField(TEXT("node"),P.Source.Name);
            Input->SetStringField(TEXT("pose_json"),P.PoseJson);
            Input->SetStringField(TEXT("pose_package"),P.PosePackage);
            Input->SetStringField(TEXT("samples_package"),P.SamplesPackage);
            Input->SetStringField(TEXT("psa_file"),P.PsaFile);
            PoseInputs.Add(MakeShared<FJsonValueObject>(Input));
        }
        J->SetArrayField(TEXT("pose_inputs"),PoseInputs);
        J->SetNumberField(TEXT("pose_driver_count"),Poses.Num()); J->SetNumberField(TEXT("kawaii_node_count"),Physics.Num());
        J->SetArrayField(TEXT("unsupported_fields"),Unsupported);
        FValues Paths;
        for (const auto& A : Assets) Paths.Add(MakeShared<FJsonValueString>(A->GetPathName()));
        J->SetArrayField(TEXT("assets"),Paths);
        FValues Warnings;
        for (auto& S : Notes) Warnings.Add(MakeShared<FJsonValueString>(S));
        J->SetArrayField(TEXT("notes"),Warnings);
        ReportFile=FPaths::ProjectSavedDir()/TEXT("FModelAnimRestore")/(FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT("_Report.json"));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportFile),true);
        FFileHelper::SaveStringToFile(JsonText(J),*ReportFile,FFileHelper::EEncodingOptions::ForceUTF8);
        if (bFragmentMode && Options.PhysicsBlueprintJson.FilePath.IsEmpty())
        {
            if (Success)
            {
                FString Status=FText::Format(NSLOCTEXT("FModelAnimRestore","PsaPrepared","Imported {0} PSA animations and created {0} Pose Assets. Ready to add Pose Drivers."),FText::AsNumber(Poses.Num())).ToString();
                for (const auto& Note:Notes) Status+=TEXT("\n")+Note;
                return Status;
            }
            FString Status=Error;
            if (!Options.PoseBlueprintJson.FilePath.IsEmpty()) Status.ReplaceInline(*Options.PoseBlueprintJson.FilePath,*FPaths::GetCleanFilename(Options.PoseBlueprintJson.FilePath));
            FString Folder=Options.PoseDirectory.Path; FPaths::NormalizeDirectoryName(Folder);
            if (!Folder.IsEmpty()) Status.ReplaceInline(*(Folder+TEXT("/")),TEXT(""));
            return Status;
        }
        FString Text=Success?FText::Format(NSLOCTEXT("FModelAnimRestore","PreparedCounts","Prepared {0} Pose Drivers and {1} Kawaii Physics nodes.\nFolder: {2}\n"),FText::AsNumber(Poses.Num()),FText::AsNumber(Physics.Num()),FText::FromString(Options.Destination)).ToString():Error+TEXT("\n");
        if (!Unsupported.IsEmpty()) Text+=FText::Format(NSLOCTEXT("FModelAnimRestore","UnsupportedFields","{0} custom fields have no direct equivalent. Their original values are in the report; the original game physics behavior is not fully reproduced.\n"),FText::AsNumber(Unsupported.Num())).ToString();
        for (auto& S : Notes) Text+=S+TEXT("\n");
        for (const auto& A : Assets) Text+=A->GetPathName()+TEXT("\n");
        return Text+NSLOCTEXT("FModelAnimRestore","ReportPath","Report: ").ToString()+FPaths::ConvertRelativePathToFull(ReportFile);
    }
};
}

bool UFModelAnimRestoreLibrary::Restore(UFModelAnimRestoreOptions* Options,FString& Report)
{
    if (!Options) { Report=NSLOCTEXT("FModelAnimRestore","MissingOptions","Import options are missing.").ToString(); return false; }
    TStrongObjectPtr<UFModelAnimRestoreOptions> KeepOptions(Options);
    FModelRestore::FImporter Importer(*Options);
    bool Success=Importer.Run();
    Report=Importer.Report(Success);
    if (!Success)
    {
        // Failed, unsaved objects must not reserve their output names for the next attempt.
        for (const auto& Asset : Importer.Assets)
            if (!FPackageName::DoesPackageExist(Asset->GetOutermost()->GetName()))
            { FAssetRegistryModule::AssetDeleted(Asset); Asset->ClearFlags(RF_Public|RF_Standalone); Asset->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional); }
    }
    UE_LOG(LogTemp,Display,TEXT("FModelAnimRestore: %s"),*Report);
    return Success;
}

FString UFModelAnimRestoreLibrary::DefaultPoseDestination(const UAnimBlueprint* Blueprint)
{
    return Blueprint ? FPackageName::GetLongPackagePath(Blueprint->GetOutermost()->GetName()) / TEXT("POSE") : TEXT("/Game/POSE");
}

bool UFModelAnimRestoreLibrary::SetAsPostProcess(UAnimBlueprint* Blueprint,USkeletalMesh* Mesh,FString& Report)
{
    if (!Blueprint || !Mesh || !Mesh->GetSkeleton() || Blueprint->TargetSkeleton!=Mesh->GetSkeleton())
    { Report=NSLOCTEXT("FModelAnimRestore","BlueprintSkeletonMismatch","The current Animation Blueprint and preview mesh must use the same Skeleton.").ToString(); return false; }
    TStrongObjectPtr<UAnimBlueprint> KeepBlueprint(Blueprint);
    TStrongObjectPtr<USkeletalMesh> KeepMesh(Mesh);
    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(Blueprint,EBlueprintCompileOptions::None,&Log);
    auto* Class=Cast<UAnimBlueprintGeneratedClass>(Blueprint->GeneratedClass);
    if (Log.NumErrors || Blueprint->Status==BS_Error || !Class)
    { Report=NSLOCTEXT("FModelAnimRestore","PostProcessCompileFailed","The blueprint could not be compiled. Fix its errors before assigning it as a Post Process Anim Blueprint.").ToString(); return false; }
    if (Mesh->GetPostProcessAnimBlueprint()==Class)
    { Report=NSLOCTEXT("FModelAnimRestore","PostProcessAlreadySet","This blueprint is already the mesh's Post Process Anim Blueprint.").ToString(); return true; }
    FScopedTransaction Transaction(NSLOCTEXT("FModelAnimRestore","SetPostProcessTransaction","Set mesh Post Process Anim Blueprint"));
    FProperty* Property=FindFProperty<FProperty>(USkeletalMesh::StaticClass(),USkeletalMesh::GetPostProcessAnimBlueprintMemberName());
    Mesh->Modify();
    Mesh->PreEditChange(Property);
    Mesh->SetPostProcessAnimBlueprint(Class);
    FPropertyChangedEvent Changed(Property,EPropertyChangeType::ValueSet);
    Mesh->PostEditChangeProperty(Changed);
    Mesh->MarkPackageDirty();
    Report=FText::Format(NSLOCTEXT("FModelAnimRestore","PostProcessSet","Set {0} as the Post Process Anim Blueprint for {1}. Save the mesh to keep this change. Ctrl+Z restores the previous assignment."),FText::FromString(Blueprint->GetName()),FText::FromString(Mesh->GetName())).ToString();
    return true;
}

bool UFModelAnimRestoreLibrary::Inspect(UFModelAnimRestoreOptions* Options,TArray<FFModelRestoreGroup>& Groups,FString& Report)
{
    Groups.Reset();
    if (!Options) { Report=NSLOCTEXT("FModelAnimRestore","MissingOptions","Import options are missing.").ToString(); return false; }
    TStrongObjectPtr<UFModelAnimRestoreOptions> Keep(Options);
    FModelRestore::FImporter Importer(*Options);
    Importer.bFragmentMode=true;
    if (!Importer.Plan(false)) { Report=Importer.Report(false); return false; }
    for (const auto& Pose : Importer.Poses)
    {
        FFModelRestoreGroup Group;
        Group.Id=Pose.Source.Name; Group.AssetName=Pose.AssetName; Group.PoseCount=Pose.Names.Num();
        Group.PoseJson=Pose.PoseJson; Group.PsaFile=Pose.PsaFile;
        TArray<FString> Bones;
        for (const auto& Bone : Pose.Driver.SourceBones) Bones.Add(Bone.BoneName.ToString());
        Group.Bones=FString::Join(Bones,TEXT(", "));
        Groups.Add(MoveTemp(Group));
    }
    for (const auto& Physics : Importer.Physics)
    {
        FFModelRestoreGroup Group;
        Group.Id=Physics.Source.Name; Group.Bones=Physics.Node.RootBone.BoneName.ToString(); Group.bPhysics=true;
        Groups.Add(MoveTemp(Group));
    }
    Report=FText::Format(NSLOCTEXT("FModelAnimRestore","GroupsRead","Found {0} groups. Select one or more groups to import."),FText::AsNumber(Groups.Num())).ToString();
    return true;
}

bool UFModelAnimRestoreLibrary::PrepareForBlueprint(UFModelAnimRestoreOptions* Options,UAnimBlueprint* Blueprint,
    const TArray<FString>& SelectedGroups,UFModelAnimRestoreBatch*& Batch,FString& Report)
{
    Batch=nullptr;
    if (!Options || !Blueprint || !Options->TargetMesh || Blueprint->TargetSkeleton != Options->TargetMesh->GetSkeleton())
    { Report=NSLOCTEXT("FModelAnimRestore","BlueprintSkeletonMismatch","The current Animation Blueprint and preview mesh must use the same Skeleton.").ToString(); return false; }
    if (SelectedGroups.IsEmpty())
    { Report=NSLOCTEXT("FModelAnimRestore","SelectGroups","Select at least one group.").ToString(); return false; }
    if (Options->bAssignPostProcess || Options->ReferenceAnimation ||
        (Options->PoseBlueprintJson.FilePath.IsEmpty() == Options->PhysicsBlueprintJson.FilePath.IsEmpty()))
    { Report=NSLOCTEXT("FModelAnimRestore","SeparateImports","Choose exactly one import type: Pose Drivers or Kawaii Physics.").ToString(); return false; }
    TStrongObjectPtr<UFModelAnimRestoreOptions> Keep(Options);
    TStrongObjectPtr<UAnimBlueprint> KeepBlueprint(Blueprint);
    FModelRestore::FImporter Importer(*Options);
    Importer.bFragmentMode=true;
    bool Success=Importer.Plan(false);
    if (Success)
    {
        for (const auto& Id : SelectedGroups)
            if (!Importer.Poses.ContainsByPredicate([&](const auto& P){return P.Source.Name==Id;}) &&
                !Importer.Physics.ContainsByPredicate([&](const auto& P){return P.Source.Name==Id;}))
            { Success=Importer.Fail(NSLOCTEXT("FModelAnimRestore","StaleSelection","The selected groups no longer exist. Read the JSON again.").ToString()); break; }
    }
    if (Success)
    {
        Importer.Poses.RemoveAll([&](const auto& P){return !SelectedGroups.Contains(P.Source.Name);});
        Importer.Physics.RemoveAll([&](const auto& P){return !SelectedGroups.Contains(P.Source.Name);});
        Success=Importer.CheckOutputNames() && Importer.BuildPoses();
    }
    TStrongObjectPtr<UFModelAnimRestoreBatch> Prepared(NewObject<UFModelAnimRestoreBatch>());
    if (Success)
    {
        UAnimBlueprint* Template=Importer.BuildGraph(false);
        Success=Template!=nullptr;
        if (Template)
        {
            for (UEdGraph* Graph : Template->FunctionGraphs)
                if (Graph->GetFName()==TEXT("AnimGraph"))
                    for (UEdGraphNode* Node : Graph->Nodes)
                        if (!Node->IsA<UAnimGraphNode_Root>() && !Node->IsA<UAnimGraphNode_LinkedInputPose>()) Prepared->TemplateNodes.Add(Node);
            Prepared->TemplateNodes.Sort([](const UEdGraphNode& A,const UEdGraphNode& B){return A.NodePosX<B.NodePosX;});
            Prepared->TemplateBlueprint=Template;
            Prepared->ExpectedNodeCount=Prepared->TemplateNodes.Num();
            Importer.Assets.Remove(Template);
            if (Prepared->TemplateNodes.IsEmpty())
                Success=Importer.Fail(NSLOCTEXT("FModelAnimRestore","EmptyFragment","No nodes could be prepared for import.").ToString());
        }
    }
    if (Success)
    {
        FAssetCompilingManager::Get().FinishAllCompilation();
        for (const auto& Asset : Importer.Assets)
            if (!Importer.Save(Asset.Get())) { Success=false; break; }
    }
    Report=Importer.Report(Success);
    if (Success)
    {
        Prepared->TargetBlueprint=Blueprint; Prepared->TargetMesh=Options->TargetMesh;
        Prepared->CreatedAssets=Importer.Assets;
        for (const auto& Pose:Importer.Poses) {Prepared->PoseAnimations.Add(Pose.Source.Name,Pose.Animation);Prepared->PoseAssets.Add(Pose.Source.Name,Pose.Asset);}
        Prepared->PoseDriverCount=Importer.Poses.Num(); Prepared->PhysicsCount=Importer.Physics.Num();
        Batch=Prepared.Get();
    }
    else
    {
        for (const auto& Asset : Importer.Assets)
            if (!Asset->HasAnyFlags(RF_Transient) && !FPackageName::DoesPackageExist(Asset->GetOutermost()->GetName()))
            { FAssetRegistryModule::AssetDeleted(Asset); Asset->ClearFlags(RF_Public|RF_Standalone); Asset->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional); }
    }
    return Success;
}

bool UFModelAnimRestoreLibrary::AddToBlueprint(UFModelAnimRestoreBatch* Batch,UAnimBlueprint* Blueprint,
    USkeletalMesh* CurrentPreviewMesh,UEdGraph* Graph,TArray<UEdGraphNode*>& AddedNodes,FString& Report)
{
    AddedNodes.Reset();
    if (!Batch || !Blueprint || Batch->TargetBlueprint!=Blueprint || Batch->TargetMesh!=CurrentPreviewMesh ||
        !CurrentPreviewMesh || Blueprint->TargetSkeleton!=CurrentPreviewMesh->GetSkeleton())
    { Report=NSLOCTEXT("FModelAnimRestore","ContextChanged","The blueprint or preview mesh has changed. Prepare the import again in this blueprint's panel.").ToString(); return false; }
    if (!Graph || FBlueprintEditorUtils::FindBlueprintForGraph(Graph)!=Blueprint || !Graph->GetSchema()->IsA<UAnimationGraphSchema>())
    { Report=NSLOCTEXT("FModelAnimRestore","NeedAnimGraph","Open an editable animation graph in the current Animation Blueprint.").ToString(); return false; }
    if (!Graph->bEditable || Batch->TemplateNodes.IsEmpty())
    { Report=NSLOCTEXT("FModelAnimRestore","CannotPaste","The prepared nodes cannot be added to this graph.").ToString(); return false; }
    TStrongObjectPtr<UFModelAnimRestoreBatch> Keep(Batch);
    const bool WasDirty=Blueprint->GetOutermost()->IsDirty();
    FScopedTransaction Transaction(NSLOCTEXT("FModelAnimRestore","AddTransaction","Add FModel animation nodes"));
    Blueprint->Modify(); Graph->Modify();
    int32 Bottom=0;
    for (UEdGraphNode* Node : Graph->Nodes) Bottom=FMath::Max(Bottom,Node->NodePosY+FMath::Max(Node->NodeHeight,200));
    bool Complete=true;
    for (const auto& Template : Batch->TemplateNodes)
    {
        UEdGraphNode* Created=nullptr;
        if (const auto* Driver=Cast<UAnimGraphNode_PoseDriver>(Template))
        {
            FGraphNodeCreator<UAnimGraphNode_PoseDriver> Creator(*Graph);
            auto* Node=Creator.CreateNode(); Node->Node=Driver->Node; Node->Node.SourcePose=FPoseLink(); Creator.Finalize(); Created=Node;
        }
        else if (Template->IsA<UAnimGraphNode_LocalToComponentSpace>())
        {FGraphNodeCreator<UAnimGraphNode_LocalToComponentSpace> Creator(*Graph);Created=Creator.CreateNode();Creator.Finalize();}
        else if (Template->IsA<UAnimGraphNode_ComponentToLocalSpace>())
        {FGraphNodeCreator<UAnimGraphNode_ComponentToLocalSpace> Creator(*Graph);Created=Creator.CreateNode();Creator.Finalize();}
        else if (const auto* Property=FindFProperty<FStructProperty>(Template->GetClass(),TEXT("Node"));Property && Property->Struct==FAnimNode_KawaiiPhysics::StaticStruct())
        {
            Created=NewObject<UEdGraphNode>(Graph,Template->GetClass(),NAME_None,RF_Transactional);
            Graph->AddNode(Created,false,false); Created->CreateNewGuid();
            Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Created),Property->ContainerPtrToValuePtr<void>(Template.Get()));
            Property->ContainerPtrToValuePtr<FAnimNode_KawaiiPhysics>(Created)->ComponentPose=FComponentSpacePoseLink();
            Created->PostPlacedNewNode(); Created->AllocateDefaultPins();
        }
        if (!Created) {Complete=false;break;}
        Created->NodeComment=Template->NodeComment; Created->bCommentBubbleVisible=true;
        AddedNodes.Add(Created);
        if (AddedNodes.Num()>1)
        {
            auto* Out=FModelRestore::FImporter::PosePin(AddedNodes[AddedNodes.Num()-2],EGPD_Output);
            auto* In=FModelRestore::FImporter::PosePin(Created,EGPD_Input);
            if (!Out || !In || !Graph->GetSchema()->TryCreateConnection(Out,In)) {Complete=false;break;}
        }
    }
    if (!Complete || AddedNodes.Num()!=Batch->ExpectedNodeCount)
    {
        for (auto* Node : AddedNodes) {Node->BreakAllNodeLinks(); Graph->RemoveNode(Node);}
        AddedNodes.Reset();
        Transaction.Cancel(); Blueprint->GetOutermost()->SetDirtyFlag(WasDirty);
        Report=NSLOCTEXT("FModelAnimRestore","IncompletePaste","Node import was incomplete. The added nodes were removed.").ToString();
        return false;
    }
    for (int32 Index=0; Index<AddedNodes.Num(); ++Index)
    {
        auto* Node=AddedNodes[Index];
        Node->SetFlags(RF_Transactional); Node->CreateNewGuid();
        Node->NodePosX=(Index%8)*360; Node->NodePosY=Bottom+240+(Index/8)*340;
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Graph->NotifyGraphChanged();
    Report=FText::Format(NSLOCTEXT("FModelAnimRestore","NodesAdded","Added {0} nodes to {1}. Connect the new chain to your pose flow, then compile and save. Ctrl+Z undoes this insertion."),
        FText::AsNumber(AddedNodes.Num()),FText::FromString(Blueprint->GetName()+TEXT(" / ")+Graph->GetName())).ToString();
    return true;
}
