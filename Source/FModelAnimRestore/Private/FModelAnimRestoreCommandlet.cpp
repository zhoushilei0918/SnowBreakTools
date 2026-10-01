#include "FModelAnimRestoreCommandlet.h"
#include "FModelAnimRestoreLibrary.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"

UFModelAnimRestoreCommandlet::UFModelAnimRestoreCommandlet()
{
    IsClient=false; IsServer=false; IsEditor=true; LogToConsole=true;
}
int32 UFModelAnimRestoreCommandlet::Main(const FString& Params)
{
    auto* O=NewObject<UFModelAnimRestoreOptions>();
    FString Mesh,Anim,Output;
    FParse::Value(*Params,TEXT("Mesh="),Mesh);
    FParse::Value(*Params,TEXT("Animation="),Anim);
    FParse::Value(*Params,TEXT("Abpp="),O->PoseBlueprintJson.FilePath);
    FParse::Value(*Params,TEXT("PoseFolder="),O->PoseDirectory.Path);
    FParse::Value(*Params,TEXT("Physics="),O->PhysicsBlueprintJson.FilePath);
    FParse::Value(*Params,TEXT("Destination="),O->Destination);
    FParse::Value(*Params,TEXT("Report="),Output);
    O->TargetMesh=LoadObject<USkeletalMesh>(nullptr,*Mesh);
    if (!Anim.IsEmpty()) O->ReferenceAnimation=LoadObject<UAnimSequence>(nullptr,*Anim);
    if (!Anim.IsEmpty() && !O->ReferenceAnimation) { UE_LOG(LogTemp,Error,TEXT("Animation not found: %s"),*Anim); return 1; }
    O->bAssignPostProcess=FParse::Param(*Params,TEXT("AssignPostProcess"));
    O->bSkipNonPoseNodes=FParse::Param(*Params,TEXT("SkipNonPoseNodes"));
    FString Report;
    bool Result=UFModelAnimRestoreLibrary::Restore(O,Report);
    if (!Output.IsEmpty()) FFileHelper::SaveStringToFile(Report,*Output,FFileHelper::EEncodingOptions::ForceUTF8);
    return Result?0:1;
}
