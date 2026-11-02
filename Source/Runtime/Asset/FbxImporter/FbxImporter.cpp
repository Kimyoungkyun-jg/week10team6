#include "EnginePCH.h"
#include "FbxImporter.h"
#include <fbxsdk.h>
#include "Render/SkeletalMeshData.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Math/Box.h"
#include <filesystem>

using namespace fbxsdk;

namespace
{
    FMatrix ConvertFbxMatrix(const fbxsdk::FbxAMatrix& FbxMat, bool bFlipHandedness)
    {
        FMatrix Result;
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                float Val = (float)FbxMat.Get(row, col);
                if (bFlipHandedness && ((row == 1) ^ (col == 1)))
                {
                    Val = -Val;
                }
                Result.M[row][col] = Val;
            }
        }
        return Result;
    }

    void ProcessSkeletonRecursive(fbxsdk::FbxNode* Node, int32 ParentIndex, FFbxInfo& Out, bool bFlipHandedness)
    {
        int32 CurrentBoneIndex = ParentIndex;

        // 노드의 속성이 eSkeleton 또는 eNull(더미/루트 노드)인지 확인
        if (Node->GetNodeAttribute())
        {
            auto AttrType = Node->GetNodeAttribute()->GetAttributeType();
            if (AttrType == fbxsdk::FbxNodeAttribute::eSkeleton || AttrType == fbxsdk::FbxNodeAttribute::eNull)
            {
                FBoneInfo Bone;
                Bone.Name = Node->GetName();
                Bone.ParentIndex = ParentIndex;
                fbxsdk::FbxAMatrix LocalMatrix = Node->EvaluateLocalTransform();
                Bone.LocalBindPose = ConvertFbxMatrix(LocalMatrix, bFlipHandedness);
                Bone.GlobalBindPose = ConvertFbxMatrix(Node->EvaluateGlobalTransform(), bFlipHandedness);
                Bone.InverseBindPose = Bone.GlobalBindPose.Inverse();
                Out.Bones.Add(Bone);
                CurrentBoneIndex = Out.Bones.Num() - 1;
            }
        }

        // 자식 노드들도 순회
        for (int i = 0; i < Node->GetChildCount(); ++i)
        {
            ProcessSkeletonRecursive(Node->GetChild(i), CurrentBoneIndex, Out, bFlipHandedness);
        }
    }

    void ProcessSkeleton(fbxsdk::FbxNode* RootNode, FFbxInfo& Out, bool bFlipHandedness)
    {
        // 루트 밑의 자식들부터 탐색 시작
        for (int i = 0; i < RootNode->GetChildCount(); ++i)
        {
            ProcessSkeletonRecursive(RootNode->GetChild(i), -1, Out, bFlipHandedness);
        }
    }

    void ProcessPolygons(fbxsdk::FbxMesh* Mesh, FFbxInfo& Out, bool bFlipHandedness, const fbxsdk::FbxAMatrix& MeshMatrix)
    {
        int PolygonCount = Mesh->GetPolygonCount();
        Out.Polygons.Reserve(PolygonCount);

        int PolygonVertexCounter = 0; // 전체 꼭짓점 순차 인덱스

        fbxsdk::FbxGeometryElementMaterial* MaterialElement = Mesh->GetElementMaterialCount() > 0
            ? Mesh->GetElementMaterial(0) : nullptr;

        for (int polyIdx = 0; polyIdx < PolygonCount; ++polyIdx)
        {
            FFbxPolygon Polygon;
            Polygon.MaterialIndex = 0; // 기본 머티리얼 0번

            if (MaterialElement)
            {
                int localMtlIdx = -1;
                // eByPolygon 방식인 경우
                if (MaterialElement->GetMappingMode() == fbxsdk::FbxGeometryElement::eByPolygon)
                {
                    if (MaterialElement->GetReferenceMode() == fbxsdk::FbxGeometryElement::eIndex ||
                        MaterialElement->GetReferenceMode() == fbxsdk::FbxGeometryElement::eIndexToDirect)
                    {
                        localMtlIdx = MaterialElement->GetIndexArray().GetAt(polyIdx);
                    }
                }
                // eAllSame 방식인 경우 (전체 메쉬가 단일 머티리얼 사용)
                else if (MaterialElement->GetMappingMode() == fbxsdk::FbxGeometryElement::eAllSame)
                {
                    localMtlIdx = MaterialElement->GetIndexArray().GetAt(0);
                }

                if (localMtlIdx >= 0)
                {
                    fbxsdk::FbxNode* Node = Mesh->GetNode();
                    if (Node && localMtlIdx < Node->GetMaterialCount())
                    {
                        fbxsdk::FbxSurfaceMaterial* FbxMat = Node->GetMaterial(localMtlIdx);
                        if (FbxMat)
                        {
                            FString MatName = FbxMat->GetName();
                            for (int m = 0; m < Out.MaterialInfos.Num(); ++m)
                            {
                                if (Out.MaterialInfos[m].Name == MatName)
                                {
                                    Polygon.MaterialIndex = m; // 전역 인덱스로 정확히 매핑!
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            for (int corner = 0; corner < 3; ++corner)
            {
                FFbxCorner& Corner = Polygon.Corners[corner];

                // ControlPoint 인덱스
                int cpIndex = Mesh->GetPolygonVertex(polyIdx, corner);
                Corner.ControlPointIndex = cpIndex;
                Corner.PolygonVertexIndex = PolygonVertexCounter;

                // Normal 추출
                fbxsdk::FbxVector4 FbxNorm;
                if (Mesh->GetPolygonVertexNormal(polyIdx, corner, FbxNorm))
                {
                    fbxsdk::FbxVector4 TransformedNorm = MeshMatrix.MultR(FbxNorm);
                    float Ny = (float)TransformedNorm[1];
                    if (bFlipHandedness) Ny = -Ny;
                    Corner.Normal = FVector((float)TransformedNorm[0], Ny, (float)TransformedNorm[2]);
                }

                // UV 추출
                if (Mesh->GetElementUVCount() > 0)
                {
                    fbxsdk::FbxGeometryElementUV* ElementUV = Mesh->GetElementUV(0);
                    // 매핑 방식에 따라 UV 읽기
                    fbxsdk::FbxVector2 FbxUV;
                    bool bUnmapped = false;
                    if (Mesh->GetPolygonVertexUV(polyIdx, corner, ElementUV->GetName(), FbxUV, bUnmapped))
                    {
                        // DirectX는 V축이 위에서 아래이므로 (1.0f - V) 뒤집어줍니다.
                        Corner.UVs = FVector2((float)FbxUV[0], 1.0f - (float)FbxUV[1]);
                    }
                }

                PolygonVertexCounter++;
            }

            Out.Polygons.Add(Polygon);
        }
    }

    void ProcessMesh(fbxsdk::FbxNode* Node, FFbxInfo& Out, bool bFlipHandedness)
    {
        // 재귀적으로 씬에서 eMesh 노드를 찾습니다.
        if (Node->GetNodeAttribute() &&
            Node->GetNodeAttribute()->GetAttributeType() == fbxsdk::FbxNodeAttribute::eMesh)
        {
            fbxsdk::FbxMesh* Mesh = Node->GetMesh();
            if (!Mesh) return;

            fbxsdk::FbxAMatrix MeshMatrix;
            MeshMatrix.SetIdentity();
            int SkinCount = Mesh->GetDeformerCount(fbxsdk::FbxDeformer::eSkin);
            if (SkinCount > 0)
            {
                fbxsdk::FbxSkin* Skin = (fbxsdk::FbxSkin*)Mesh->GetDeformer(0, fbxsdk::FbxDeformer::eSkin);
                if (Skin && Skin->GetClusterCount() > 0)
                {
                    Skin->GetCluster(0)->GetTransformMatrix(MeshMatrix);
                }
            }
            else
            {
                MeshMatrix = Node->EvaluateGlobalTransform();
            }

            // 원본 정점 위치 읽기
            int ControlPointCount = Mesh->GetControlPointsCount();
            fbxsdk::FbxVector4* ControlPoints = Mesh->GetControlPoints();

            Out.ControlPoints.SetNum(ControlPointCount);
            Out.ControlPointWeights.SetNum(ControlPointCount);

            for (int i = 0; i < ControlPointCount; ++i)
            {
                fbxsdk::FbxVector4 WorldPos = MeshMatrix.MultT(ControlPoints[i]);
                float Y = (float)WorldPos[1];
                if (bFlipHandedness) Y = -Y;
                Out.ControlPoints[i] = FVector(
                    (float)WorldPos[0],
                    Y,
                    (float)WorldPos[2]
                );
            }

            // 스킨 디포머 파싱
            //int SkinCount = Mesh->GetDeformerCount(fbxsdk::FbxDeformer::eSkin);
            for (int i = 0; i < SkinCount; ++i)
            {
                fbxsdk::FbxSkin* Skin = (fbxsdk::FbxSkin*)Mesh->GetDeformer(i, fbxsdk::FbxDeformer::eSkin);
                int ClusterCount = Skin->GetClusterCount();

                for (int clusterIdx = 0; clusterIdx < ClusterCount; ++clusterIdx)
                {
                    fbxsdk::FbxCluster* Cluster = Skin->GetCluster(clusterIdx);
                    fbxsdk::FbxNode* LinkNode = Cluster->GetLink(); // 이 클러스터에 연결된 본 노드
                    if (!LinkNode) continue;

                    // Bones에서 이 본의 인덱스 찾기
                    FString BoneName = LinkNode->GetName();
                    int32 BoneIndex = -1;
                    for (int b = 0; b < Out.Bones.Num(); ++b)
                    {
                        if (Out.Bones[b].Name == BoneName)
                        {
                            BoneIndex = b;
                            break;
                        }
                    }
                    if (BoneIndex == -1) continue;

                    // 바인드 포즈 행렬 및 역행렬 추출
                    fbxsdk::FbxAMatrix TransformLinkMatrix;
                    Cluster->GetTransformLinkMatrix(TransformLinkMatrix); // 본의 초기 월드 행렬
                    FMatrix GlobalBindPose = ConvertFbxMatrix(TransformLinkMatrix, bFlipHandedness);
                    Out.Bones[BoneIndex].GlobalBindPose = GlobalBindPose;
                    Out.Bones[BoneIndex].InverseBindPose = GlobalBindPose.Inverse();

                    // 이 본이 영향을 주는 정점 인덱스와 가중치 읽기
                    int IndexCount = Cluster->GetControlPointIndicesCount();
                    int* Indices = Cluster->GetControlPointIndices();
                    double* Weights = Cluster->GetControlPointWeights();

                    for (int k = 0; k < IndexCount; ++k)
                    {
                        int cpIndex = Indices[k];
                        float weight = static_cast<float>(Weights[k]);
                        Out.ControlPointWeights[cpIndex].AddBoneWeight(BoneIndex, weight);
                    }
                }
            }

            // 모든 정점의 가중치 합을 1.0으로 정규화
            for (int i = 0; i < ControlPointCount; ++i)
            {
                Out.ControlPointWeights[i].NormalizeWeights();
            }

            // 클러스터가 없는 루트 eNull 노드가 있을 경우 자식의 바인드 포즈로부터 역산
            for (int32 b = 0; b < Out.Bones.Num(); ++b)
            {
                if (Out.Bones[b].ParentIndex != -1)
                {
                    int32 p = Out.Bones[b].ParentIndex;
                    if (Out.Bones[p].GlobalBindPose == FMatrix::Identity && Out.Bones[b].GlobalBindPose != FMatrix::Identity)
                    {
                        Out.Bones[p].GlobalBindPose = Out.Bones[b].GlobalBindPose * Out.Bones[b].LocalBindPose.Inverse();
                        Out.Bones[p].InverseBindPose = Out.Bones[p].GlobalBindPose.Inverse();
                    }
                }
            }

            // 부모-자식 계층에 맞게 LocalBindPose를 GlobalBindPose 기준으로 일치화
            for (int32 b = 0; b < Out.Bones.Num(); ++b)
            {
                if (Out.Bones[b].ParentIndex == -1)
                {
                    Out.Bones[b].LocalBindPose = Out.Bones[b].GlobalBindPose;
                }
                else
                {
                    int32 p = Out.Bones[b].ParentIndex;
                    Out.Bones[b].LocalBindPose = Out.Bones[b].GlobalBindPose * Out.Bones[p].GlobalBindPose.Inverse();
                }
            }

            // 폴리곤 데이터 읽기
            ProcessPolygons(Mesh, Out, bFlipHandedness, MeshMatrix);
            return; // 일단 첫 번째 메쉬 처리 후 종료
        }

        for (int i = 0; i < Node->GetChildCount(); ++i)
        {
            ProcessMesh(Node->GetChild(i), Out, bFlipHandedness);
        }
    }

    FString GetTexturePathFromProperty(fbxsdk::FbxProperty Property, const FString& FbxDirectory)
    {
        if (!Property.IsValid()) return "";

        int TextureCount = Property.GetSrcObjectCount<fbxsdk::FbxFileTexture>();
        if (TextureCount > 0)
        {
            fbxsdk::FbxFileTexture* FileTexture = Property.GetSrcObject<fbxsdk::FbxFileTexture>(0);
            if (FileTexture)
            {
                // 상대 경로로 합쳐서 실제 파일이 있는지 확인
                FString RelPath = FileTexture->GetRelativeFileName();
                if (!RelPath.empty())
                {
                    std::filesystem::path Combined = std::filesystem::path(FbxDirectory.c_str()) / RelPath.c_str();
                    if (std::filesystem::exists(Combined))
                        return Combined.lexically_normal().generic_string().c_str();
                }

                // 같은 폴더에 파일명으로 존재하는지 확인 (예: Assets/Models/.../shaded.png)
                std::filesystem::path FullPath = FileTexture->GetFileName();
                std::filesystem::path FileName = FullPath.filename();
                std::filesystem::path SameDir = std::filesystem::path(FbxDirectory.c_str()) / FileName;
                if (std::filesystem::exists(SameDir))
                    return SameDir.lexically_normal().generic_string().c_str();

                // FBX 자동 언팩 폴더(.fbm) 내에 존재하는지 확인
                std::filesystem::path FbxPath(FbxDirectory.c_str());
                for (const auto& Entry : std::filesystem::directory_iterator(FbxDirectory.c_str()))
                {
                    if (Entry.is_directory())
                    {
                        std::filesystem::path InSubDir = Entry.path() / FileName;
                        if (std::filesystem::exists(InSubDir))
                            return InSubDir.lexically_normal().generic_string().c_str();
                    }
                }
            }
        }
        return "";
    }

    void ProcessMaterials(fbxsdk::FbxNode* Node, FFbxInfo& Out)
    {
        // FBX 파일이 위치한 부모 폴더 경로 (텍스처 상대 경로 조합용)
        std::filesystem::path FbxFilePath(Out.Path.c_str());
        FString FbxDir = FbxFilePath.parent_path().string().c_str();

        int MaterialCount = Node->GetMaterialCount();
        for (int i = 0; i < MaterialCount; ++i)
        {
            fbxsdk::FbxSurfaceMaterial* FbxMat = Node->GetMaterial(i);
            if (!FbxMat) continue;

            FString MatName = FbxMat->GetName();

            bool bAlreadyExists = false;
            for (const auto& Exist : Out.MaterialInfos)
            {
                if (Exist.Name == MatName) { bAlreadyExists = true; break; }
            }
            if (bAlreadyExists) continue;

            FFbxMaterialInfo MatInfo;
            MatInfo.Name = MatName;

            // Diffuse / BaseColor 색상 읽기
            if (FbxMat->GetClassId().Is(fbxsdk::FbxSurfaceLambert::ClassId))
            {
                fbxsdk::FbxDouble3 Diffuse = ((fbxsdk::FbxSurfaceLambert*)FbxMat)->Diffuse.Get();
                MatInfo.BaseColor = FVector4((float)Diffuse[0], (float)Diffuse[1], (float)Diffuse[2], 1.0f);
            }

            // Diffuse 텍스처 경로 읽기
            fbxsdk::FbxProperty DiffuseProp = FbxMat->FindProperty(fbxsdk::FbxSurfaceMaterial::sDiffuse);
            MatInfo.DiffuseTexturePath = GetTexturePathFromProperty(DiffuseProp, FbxDir);

            // 만약 PBR 머티리얼이라 sDiffuse가 비어있다면 "BaseColor" 속성도 검사
            if (MatInfo.DiffuseTexturePath.empty())
            {
                fbxsdk::FbxProperty BaseColorProp = FbxMat->FindProperty("BaseColor");
                MatInfo.DiffuseTexturePath = GetTexturePathFromProperty(BaseColorProp, FbxDir);
            }

            // Normal 텍스처 경로 읽기
            fbxsdk::FbxProperty NormalProp = FbxMat->FindProperty(fbxsdk::FbxSurfaceMaterial::sNormalMap);
            MatInfo.NormalTexturePath = GetTexturePathFromProperty(NormalProp, FbxDir);

            if (MatInfo.NormalTexturePath.empty())
            {
                fbxsdk::FbxProperty BumpProp = FbxMat->FindProperty(fbxsdk::FbxSurfaceMaterial::sBump);
                MatInfo.NormalTexturePath = GetTexturePathFromProperty(BumpProp, FbxDir);
            }

            Out.MaterialInfos.Add(MatInfo);
        }

        // 자식 노드 순회
        for (int i = 0; i < Node->GetChildCount(); ++i)
        {
            ProcessMaterials(Node->GetChild(i), Out);
        }
    }
}

bool FFbxImporter::ParseFbx(const FString& Path, FFbxInfo& Out)
{
    // FBX SDK 초기화 및 파일 로드, 삼각형화
    fbxsdk::FbxManager* Manager = fbxsdk::FbxManager::Create();
    if (!Manager) return false;

    fbxsdk::FbxIOSettings* IOSettings = fbxsdk::FbxIOSettings::Create(Manager, IOSROOT);
    Manager->SetIOSettings(IOSettings);

    fbxsdk::FbxImporter* Importer = fbxsdk::FbxImporter::Create(Manager, "");
    // 한글 경로 등 대비 c_str() 전달
    if (!Importer->Initialize(Path.c_str(), -1, Manager->GetIOSettings()))
    {
        Manager->Destroy();
        return false;
    }

    fbxsdk::FbxScene* Scene = fbxsdk::FbxScene::Create(Manager, "Scene");
    Importer->Import(Scene);
    Importer->Destroy(); // 씬에 올렸으면 임포터 객체는 바로 닫아도 됩니다.

    // 크기 단위 변환, 원본 단위 / 목표 단위로 배율을 구하고 모든 정점의 좌표에 곱해줌, Bone의 이동거리와 애니메이션 키프레임의 위치 값에도 곱해줌
    fbxsdk::FbxSystemUnit SceneUnit = Scene->GetGlobalSettings().GetSystemUnit();
    if (SceneUnit != fbxsdk::FbxSystemUnit::m)
    {
        fbxsdk::FbxSystemUnit::m.ConvertScene(Scene);
    }

	// 좌표계 변환 (Z-Up, Left-Handed)
    fbxsdk::FbxAxisSystem EngineAxisSystem(
        fbxsdk::FbxAxisSystem::eZAxis,
        fbxsdk::FbxAxisSystem::eParityEven,
        fbxsdk::FbxAxisSystem::eRightHanded
    );

	// 씬의 좌표계를 엔진 좌표계로 변환
    fbxsdk::FbxAxisSystem SceneAxisSystem = Scene->GetGlobalSettings().GetAxisSystem();
    bool bFlipHandedness = (SceneAxisSystem.GetCoorSystem() == fbxsdk::FbxAxisSystem::eRightHanded);
	// FBX 좌표계와 엔진 좌표계가 다르면 씬 전체를 변환
    if (SceneAxisSystem != EngineAxisSystem)
    {
        // 기저 변환으로 회전시키고 삼각형 감김 순서를 뒤집어서 겉면이 제대로 보이도록 한다.
        EngineAxisSystem.ConvertScene(Scene);
    }

    // 삼각화
    fbxsdk::FbxGeometryConverter Converter(Manager);
    Converter.Triangulate(Scene, true);

    fbxsdk::FbxNode* RootNode = Scene->GetRootNode();
    if (!RootNode)
    {
        Manager->Destroy();
        return false;
    }

    Out.Path = Path;

    // 스켈레톤 정보 추출
    ProcessSkeleton(RootNode, Out, bFlipHandedness);

    // 머티리얼 및 텍스처 경로 추출
    ProcessMaterials(RootNode, Out);

    // 메시, 정점, 스키닝 가중치 추출
    ProcessMesh(RootNode, Out, bFlipHandedness);

    Manager->Destroy();
    return true;
}

TUniquePtr<FSkeletalMeshData> FFbxImporter::LoadSkeletalMeshData(const FString& Path)
{
    FFbxInfo RawInfo;
    if (!ParseFbx(Path, RawInfo))
    {
        return nullptr;
    }

    TUniquePtr<FSkeletalMeshData> MeshData = MakeUnique<FSkeletalMeshData>();
    if (!Cook(RawInfo, *MeshData))
    {
        return nullptr;
    }

    return MeshData;
}

bool FFbxImporter::Cook(const FFbxInfo& Raw, FSkeletalMeshData& Out)
{
    Out.Bones = Raw.Bones;

    int32 MaterialCount = Raw.MaterialInfos.Num();
    if (MaterialCount == 0)
    {
        FSkeletalMaterialSlot DefaultSlot;
        DefaultSlot.Name = "DefaultMaterial";
        Out.MaterialSlots.Add(DefaultSlot);
        MaterialCount = 1;
    }
    else
    {
        for (const FFbxMaterialInfo& MatInfo : Raw.MaterialInfos)
        {
            FSkeletalMaterialSlot Slot;
            Slot.Name = MatInfo.Name;
            Slot.BaseColor = MatInfo.BaseColor;
            Slot.DiffuseTexturePath = MatInfo.DiffuseTexturePath;
            Slot.NormalTexturePath = MatInfo.NormalTexturePath;
            Out.MaterialSlots.Add(Slot);
        }
    }

    // AABB 계산용 초기값
    FVector BoundsMin(FLT_MAX, FLT_MAX, FLT_MAX);
    FVector BoundsMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    for (int32 mtlIdx = 0; mtlIdx < MaterialCount; ++mtlIdx)
    {
        FSkeletalMeshSection Section;
        Section.StartIndex = static_cast<uint32>(Out.Indices.Num());
        Section.MaterialSlotIndex = mtlIdx;

        // 이 머티리얼 슬롯을 사용하는 삼각형만 모읍니다.
        for (const FFbxPolygon& Poly : Raw.Polygons)
        {
            // 폴리곤의 MaterialIndex가 현재 슬롯과 일치하는지 확인
            int32 PolyMtlIdx = (Poly.MaterialIndex >= 0 && Poly.MaterialIndex < MaterialCount) ? Poly.MaterialIndex : 0;
            if (PolyMtlIdx != mtlIdx) continue;

            const int CornerOrder[3] = { 2, 1, 0 };
            // 삼각형 3개 꼭짓점을 정점으로 변환 (CW 앞면 정렬)
            for (int i = 0; i < 3; ++i)
            {
                int corner = CornerOrder[i];
                const FFbxCorner& Corner = Poly.Corners[corner];
                int32 cpIdx = Corner.ControlPointIndex;

                FSkeletalVertex SkinnedVertex;

                // 위치(Position)
                if (cpIdx >= 0 && cpIdx < Raw.ControlPoints.Num())
                {
                    SkinnedVertex.Position = Raw.ControlPoints[cpIdx];

                    // AABB 갱신
                    BoundsMin.X = std::min(BoundsMin.X, SkinnedVertex.Position.X);
                    BoundsMin.Y = std::min(BoundsMin.Y, SkinnedVertex.Position.Y);
                    BoundsMin.Z = std::min(BoundsMin.Z, SkinnedVertex.Position.Z);

                    BoundsMax.X = std::max(BoundsMax.X, SkinnedVertex.Position.X);
                    BoundsMax.Y = std::max(BoundsMax.Y, SkinnedVertex.Position.Y);
                    BoundsMax.Z = std::max(BoundsMax.Z, SkinnedVertex.Position.Z);

                    // 본 인덱스 & 가중치 매핑
                    const FFbxBoneWeight& BoneWeight = Raw.ControlPointWeights[cpIdx];
                    for (int k = 0; k < 4; ++k)
                    {
                        SkinnedVertex.BoneIndices[k] = BoneWeight.BoneIndices[k];
                        SkinnedVertex.BoneWeights[k] = BoneWeight.BoneWeights[k];
                    }
                }

                // Normal, UV, Color
                SkinnedVertex.Normal = Corner.Normal;
                SkinnedVertex.UV = Corner.UVs;
                SkinnedVertex.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

                // 인덱스 및 정점 추가
                uint32 NewVertexIndex = static_cast<uint32>(Out.SkinnedVertices.Num());
                Out.SkinnedVertices.Add(SkinnedVertex);
                Out.Indices.Add(NewVertexIndex);
            }
        }

        Section.IndexCount = static_cast<uint32>(Out.Indices.Num()) - Section.StartIndex;
        if (Section.IndexCount > 0)
        {
            Out.Sections.Add(Section);
        }
    }
    Out.AABB.Min = BoundsMin;
    Out.AABB.Max = BoundsMax;
    Out.BuildTangentSpace();
    return true;
}
