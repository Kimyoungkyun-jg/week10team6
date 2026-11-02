#include "EnginePCH.h"
#include "SkeletalMeshBake.h"

#include <cstring>
#include <fstream>
#include <type_traits>

#include "Render/SkeletalMeshData.h"
#include "Core/EngineLog.h"

static_assert(sizeof(FSkeletalVertex) == 96);
static_assert(sizeof(FMatrix) == 64);

static_assert(std::is_trivially_copyable_v<FSkeletalVertex>);
static_assert(std::is_standard_layout_v<FMatrix>);
static_assert(std::is_trivially_copyable_v<FSkeletalMeshSection>);
static_assert(std::is_trivially_copyable_v<FVector4>);
static_assert(std::is_trivially_copyable_v<FBox>);

namespace
{
	// FSkeletalMeshData, FVertex, FSkeletalMeshSection, FStaticMaterialSlot 구조가 바뀌면 올린다
	constexpr uint32 BakeVersion{ 1 };

	struct FBakeHeader
	{
		// 'S''K'eletal Mesh 'B'a'K'e
		char Magic[4] = { 'S', 'K', 'B', 'K' };
		uint32 Version = BakeVersion;
	};

	// 숫자 || 숫자만 있는 구조체
	template<typename T>
	void WritePod(std::ofstream& Out, const T& Value)
	{
		static_assert(std::is_trivially_copyable_v<T> || std::is_standard_layout_v<T>, "invalid plain old data");
		Out.write(reinterpret_cast<const char*>(&Value), sizeof(T));
	}
	template<typename T>
	bool ReadPod(std::ifstream& In, T& Value)
	{
		static_assert(std::is_trivially_copyable_v<T> || std::is_standard_layout_v<T>);
		In.read(reinterpret_cast<char*>(&Value), sizeof(T));
		return static_cast<bool>(In);
	}

	// 배열: 개수 + 원소 전체를 한 번에
	template<typename T>
	void WriteArray(std::ofstream& Out, const TArray<T>& Array)
	{
		const uint32 Count = static_cast<uint32>(Array.Num());
		WritePod(Out, Count);
		if (Count > 0)
		{
			Out.write(reinterpret_cast<const char*>(Array.GetData()), sizeof(T) * Count);
		}
	}

	template<typename T>
	bool ReadArray(std::ifstream& In, TArray<T>& Array, uint32 MaxCount)
	{
		uint32 Count = 0;
		if (!ReadPod(In, Count) || Count > MaxCount)
		{
			return false;
		}

		Array.SetNum(static_cast<int32>(Count));
		if (Count > 0)
		{
			In.read(reinterpret_cast<char*>(Array.GetData()), sizeof(T) * Count);
		}
		return static_cast<bool>(In);
	}

	// 길이 + 문자열
	void WriteString(std::ofstream& Out, const FString& Str)
	{
		const uint32 Length = static_cast<uint32>(Str.size());
		WritePod(Out, Length);
		Out.write(Str.data(), Length);
	}

	bool ReadString(std::ifstream& In, FString& Str)
	{
		uint32 Length = 0;
		if (!ReadPod(In, Length) || Length > 4096)
		{
			return false;
		}
		Str.resize(Length);
		In.read(Str.data(), Length);
		return static_cast<bool>(In);
	}
}

void FSkeletalMeshBake::WriteBaked(const FString& BinPath, const FSkeletalMeshData& Data)
{
	std::ofstream Out(BinPath, std::ios::binary);
	if (!Out)
	{
		HTR_LOG(Warning, "[BAKE] {}: cannot open for writing", BinPath);
		return;
	}
	
	WritePod<FBakeHeader>(Out, FBakeHeader());
	WriteArray<FSkeletalVertex>(Out, Data.SkinnedVertices);
	WriteArray<uint32>(Out, Data.Indices);
	WriteArray<FSkeletalMeshSection>(Out, Data.Sections);
	// MaterialSlots
	WritePod<uint32>(Out, Data.MaterialSlots.Num());
	for (const auto& Slot : Data.MaterialSlots)
	{
		WriteString(Out, Slot.Name);
		WritePod<FVector4>(Out, Slot.BaseColor);
		WriteString(Out, Slot.DiffuseTexturePath);
		WriteString(Out, Slot.NormalTexturePath);
	}
	// Bones
	WritePod<uint32>(Out, Data.Bones.Num());
	for (const auto& Bone : Data.Bones)
	{
		WriteString(Out, Bone.Name);
		WritePod<int32>(Out, Bone.ParentIndex);
		WritePod<FMatrix>(Out, Bone.LocalBindPose);
		WritePod<FMatrix>(Out, Bone.GlobalBindPose);
		WritePod<FMatrix>(Out, Bone.InverseBindPose);
	}
	// AABB
	WritePod<FBox>(Out, Data.AABB);

	// 스트림은 한 번 실패하면 상태가 유지되므로 마지막에 한 번만 확인
	if (!Out)
	{
		HTR_LOG(Warning, "[BAKE] {}: write failed", BinPath);
	}
	else
	{
		HTR_LOG(Info, "[BAKE] {}: saved", BinPath);
	}
}

TUniquePtr<FSkeletalMeshData> FSkeletalMeshBake::ReadBaked(const FString& BinPath)
{
	std::ifstream In(BinPath, std::ios::binary);
	if (!In)
	{
		return nullptr;
	}

	TUniquePtr<FSkeletalMeshData> Data = MakeUnique<FSkeletalMeshData>();
	uint32 MaxCount = 1000000;

	FBakeHeader Header;
	const FBakeHeader ExpectedHeader;
	if(!ReadPod<FBakeHeader>(In, Header)||
		std::memcmp(Header.Magic, ExpectedHeader.Magic, sizeof(Header.Magic)) != 0 ||
		Header.Version != ExpectedHeader.Version)
	{
		HTR_LOG(Warning, "[BAKE] {}: invalid header", BinPath);
		return nullptr;
	}

	if (!ReadArray<FSkeletalVertex>(In, Data->SkinnedVertices, MaxCount) ||
		!ReadArray<uint32>(In, Data->Indices, MaxCount) ||
		!ReadArray<FSkeletalMeshSection>(In, Data->Sections, MaxCount))
	{
		return nullptr;
	}

	uint32 MaterialSlotsNum = 0;
	if (!ReadPod<uint32>(In, MaterialSlotsNum))
	{
		return nullptr;
	}

	for (int32 i = 0; i < MaterialSlotsNum; ++i)
	{
		FSkeletalMaterialSlot Slot{};
		if (!ReadString(In, Slot.Name) || !ReadPod<FVector4>(In, Slot.BaseColor) || !ReadString(In, Slot.DiffuseTexturePath) || !ReadString(In, Slot.NormalTexturePath)) {
			return nullptr;
		}
		Data->MaterialSlots.Add(std::move(Slot));
	}

	uint32 BonesNum = 0;
	if (!ReadPod<uint32>(In, BonesNum))
	{
		return nullptr;
	}

	for (int32 i = 0; i < BonesNum; ++i)
	{
		FBoneInfo Bone{};
		if (!ReadString(In, Bone.Name) || !ReadPod<int32>(In, Bone.ParentIndex) || !ReadPod<FMatrix>(In, Bone.LocalBindPose) || !ReadPod<FMatrix>(In, Bone.GlobalBindPose) || !ReadPod<FMatrix>(In, Bone.InverseBindPose)) {
			return nullptr;
		}
		Data->Bones.Add(std::move(Bone));
	}

	if(!ReadPod<FBox>(In, Data->AABB))
	{
		return nullptr;
	}

	FString Error;
	if (!Data->Validate(Error))
	{
		HTR_LOG(Warning, "[BAKE] {}: {}, re-cooking", BinPath, Error);
		return nullptr;
	}

	HTR_LOG(Info, "[BAKE] {}: loaded", BinPath);
	return Data;
}