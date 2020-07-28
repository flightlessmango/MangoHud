#include <dxgi.h>
#include <dxgi1_5.h>
#include <d3d12.h>
#include <wrl.h>
#ifndef __D3D12_IMPL_H__
#define __D3D12_IMPL_H__

namespace impl
{
	namespace d3d12
	{
		void init();
		void uninit();
	}
}

typedef long(__fastcall* PresentD3D12) (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
typedef HRESULT(__stdcall* tResizeBuffers)(IDXGISwapChain* pThis, UINT BufferCount, UINT Width,
    UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

extern PresentD3D12 oPresentD3D12;
extern void(*oExecuteCommandListsD3D12)(ID3D12CommandQueue*, UINT, ID3D12CommandList*);
extern tResizeBuffers oResizeBuffers;

typedef void(__fastcall* DrawInstancedD3D12)(ID3D12GraphicsCommandList* dCommandList, UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation);
extern DrawInstancedD3D12 oDrawInstancedD3D12;

typedef void(__fastcall* DrawIndexedInstancedD3D12)(ID3D12GraphicsCommandList* dCommandList, UINT IndexCount, UINT InstanceCount, UINT StartIndex, INT BaseVertex);
extern DrawIndexedInstancedD3D12 oDrawIndexedInstancedD3D12;

#endif // __D3D12_IMPL_H__
