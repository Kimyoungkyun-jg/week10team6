#pragma once

#include "Core/Types.h"
#include "Render/RenderDevice.h"
#include <wrl.h>

// GPU 명령 구간을 타임스탬프로 측정한다. 결과를 기다리며 GPU를 멈추지 않는다.
class FGPUTimer
{
public:
	bool Begin()
	{
		FRenderDevice* Device = FRenderDevice::Get();
		if (!Device || ActiveSlot >= 0) return false;
		FQuerySlot& Slot = Slots[NextSlot];
		if (Slot.bPending) return false;
		if (!Slot.Disjoint)
		{
			const D3D11_QUERY_DESC DisjointDesc{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
			const D3D11_QUERY_DESC TimestampDesc{ D3D11_QUERY_TIMESTAMP, 0 };
			if (FAILED(Device->GetDevice()->CreateQuery(&DisjointDesc, Slot.Disjoint.GetAddressOf())) ||
				FAILED(Device->GetDevice()->CreateQuery(&TimestampDesc, Slot.Start.GetAddressOf())) ||
				FAILED(Device->GetDevice()->CreateQuery(&TimestampDesc, Slot.End.GetAddressOf())))
			{
				Slot = FQuerySlot{};
				return false;
			}
		}
		ID3D11DeviceContext* Context = Device->GetContext();
		Context->Begin(Slot.Disjoint.Get());
		Context->End(Slot.Start.Get());
		Slot.Sequence = NextSequence++;
		Slot.bPending = true;
		ActiveSlot = static_cast<int32>(NextSlot);
		NextSlot = (NextSlot + 1) % SlotCount;
		return true;
	}

	void End()
	{
		if (ActiveSlot < 0) return;
		if (FRenderDevice* Device = FRenderDevice::Get())
		{
			FQuerySlot& Slot = Slots[ActiveSlot];
			ID3D11DeviceContext* Context = Device->GetContext();
			Context->End(Slot.End.Get());
			Context->End(Slot.Disjoint.Get());
		}
		ActiveSlot = -1;
	}

	void Poll()
	{
		FRenderDevice* Device = FRenderDevice::Get();
		if (!Device) return;
		ID3D11DeviceContext* Context = Device->GetContext();
		for (FQuerySlot& Slot : Slots)
		{
			if (!Slot.bPending || ActiveSlot >= 0) continue;
			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT Disjoint{};
			uint64 Start = 0, EndTime = 0;
			if (Context->GetData(Slot.Disjoint.Get(), &Disjoint, sizeof(Disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
				Context->GetData(Slot.Start.Get(), &Start, sizeof(Start), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
				Context->GetData(Slot.End.Get(), &EndTime, sizeof(EndTime), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				continue;
			Slot.bPending = false;
			if (Disjoint.Disjoint || !Disjoint.Frequency || EndTime < Start || Slot.Sequence <= PublishedSequence)
				continue;
			PublishedSequence = Slot.Sequence;
			LastMs = static_cast<double>(EndTime - Start) * 1000.0 / static_cast<double>(Disjoint.Frequency);
			SampleSum -= Samples[SampleIndex];
			Samples[SampleIndex] = LastMs;
			SampleSum += LastMs;
			SampleIndex = (SampleIndex + 1) % SampleCountLimit;
			if (SampleCount < SampleCountLimit) ++SampleCount;
			if (LastMs > MaxMs) MaxMs = LastMs;
		}
	}

	void Reset() { *this = FGPUTimer{}; }
	bool HasSample() const { return SampleCount > 0; }
	double GetLastMs() const { return LastMs; }
	double GetAverageMs() const { return SampleCount ? SampleSum / SampleCount : 0.0; }
	double GetMaxMs() const { return MaxMs; }

private:
	static constexpr uint32 SlotCount = 4;
	static constexpr uint32 SampleCountLimit = 60;
	struct FQuerySlot
	{
		Microsoft::WRL::ComPtr<ID3D11Query> Disjoint;
		Microsoft::WRL::ComPtr<ID3D11Query> Start;
		Microsoft::WRL::ComPtr<ID3D11Query> End;
		bool bPending = false;
		uint64 Sequence = 0;
	};
	FQuerySlot Slots[SlotCount];
	uint32 NextSlot = 0;
	int32 ActiveSlot = -1;
	uint64 NextSequence = 1;
	uint64 PublishedSequence = 0;
	double Samples[SampleCountLimit]{};
	double SampleSum = 0.0;
	double LastMs = 0.0;
	double MaxMs = 0.0;
	uint32 SampleIndex = 0;
	uint32 SampleCount = 0;
};
