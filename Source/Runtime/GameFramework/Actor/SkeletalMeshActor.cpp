#include "EnginePCH.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "Asset/AssetManager.h"

ASkeletalMeshActor::ASkeletalMeshActor()
{
    SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
	USkeletalMesh* Mesh = UAssetManager::LoadFbxSkeletalMesh("Assets/Models/tungtungtungsahur/tung tung tung sahur.fbx");
	SkeletalMeshComponent->SetSkeletalMesh(Mesh);
    RootComponent = SkeletalMeshComponent;
}