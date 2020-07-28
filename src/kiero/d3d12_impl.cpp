#include "kiero.h"

#if KIERO_INCLUDE_D3D12

#include "d3d12_impl.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <assert.h>
#include <iostream>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include "dx_shared.h"
#include <winuser.h>

using namespace Microsoft::WRL;

decltype(&IDXGISwapChain3::Present) oPresent = nullptr;
void(*oExecuteCommandListsD3D12)(ID3D12CommandQueue*, UINT, ID3D12CommandList*);
HRESULT(*oSignalD3D12)(ID3D12CommandQueue*, ID3D12Fence*, UINT64) = nullptr;
PresentD3D12 oPresentD3D12;
tResizeBuffers oResizeBuffers;

ComPtr<ID3D12DescriptorHeap> d3d12DescriptorHeapBackBuffers;
ComPtr<ID3D12DescriptorHeap> d3d12DescriptorHeapImGuiRender;
ComPtr<ID3D12GraphicsCommandList> d3d12CommandList;
ComPtr<ID3D12Fence> d3d12Fence;
UINT64 d3d12FenceValue = 0;
ComPtr<ID3D12CommandQueue> d3d12CommandQueue;
ComPtr<ID3D12Device> d3d12Device;
bool initDX12 = false;

struct FrameContext {
	ID3D12CommandAllocator* commandAllocator = nullptr;
	ID3D12Resource* main_render_target_resource = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE main_render_target_descriptor;
};

std::vector<FrameContext> frameContext;

bool DX12Init(IDXGISwapChain* pSwapChain)
{
	if (SUCCEEDED(pSwapChain->GetDevice(IID_PPV_ARGS(&d3d12Device)))) {

		imgui_create(pSwapChain, d3d12Device.Get());
		ImGui::SetCurrentContext(state.imgui_ctx);
		//CreateEvent(nullptr, false, false, nullptr);

		DXGI_SWAP_CHAIN_DESC sdesc;
		pSwapChain->GetDesc(&sdesc);

		frameContext.resize(sdesc.BufferCount);

		D3D12_DESCRIPTOR_HEAP_DESC descriptorImGuiRender = {};
		descriptorImGuiRender.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		descriptorImGuiRender.NumDescriptors = sdesc.BufferCount;
		descriptorImGuiRender.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(d3d12Device->CreateDescriptorHeap(&descriptorImGuiRender, IID_PPV_ARGS(&d3d12DescriptorHeapImGuiRender))))
			return false;

		ID3D12CommandAllocator* allocator;
		 if (FAILED(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
		 	return false;

		for (size_t i = 0; i < frameContext.size(); i++) {
			frameContext[i].commandAllocator = allocator;
		}

		if (d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL, IID_PPV_ARGS(&d3d12CommandList)) < 0)
			return false;

		// Reset() expects it to be closed, so close it now.
		d3d12CommandList->Close();

		D3D12_DESCRIPTOR_HEAP_DESC descriptorBackBuffers;
		descriptorBackBuffers.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		descriptorBackBuffers.NumDescriptors = sdesc.BufferCount;
		descriptorBackBuffers.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		descriptorBackBuffers.NodeMask = 1;

		if (d3d12Device->CreateDescriptorHeap(&descriptorBackBuffers, IID_PPV_ARGS(&d3d12DescriptorHeapBackBuffers)) != S_OK)
			return false;

		const auto rtvDescriptorSize = d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = d3d12DescriptorHeapBackBuffers->GetCPUDescriptorHandleForHeapStart();

		for (size_t i = 0; i < frameContext.size(); i++) {
			ID3D12Resource* pBackBuffer = nullptr;

			frameContext[i].main_render_target_descriptor = rtvHandle;
			pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
			d3d12Device->CreateRenderTargetView(pBackBuffer, nullptr, rtvHandle);
			frameContext[i].main_render_target_resource = pBackBuffer;
			rtvHandle.ptr += rtvDescriptorSize;
		}
		auto mainWindow = sdesc.OutputWindow;
		ImGui_ImplWin32_Init(sdesc.OutputWindow);
		ImGui_ImplDX12_Init(d3d12Device.Get(), frameContext.size(),
			DXGI_FORMAT_R8G8B8A8_UNORM, d3d12DescriptorHeapImGuiRender.Get(),
			d3d12DescriptorHeapImGuiRender->GetCPUDescriptorHandleForHeapStart(),
			d3d12DescriptorHeapImGuiRender->GetGPUDescriptorHandleForHeapStart());

		ImGui_ImplDX12_CreateDeviceObjects();
	}
	return true;
}

long __fastcall hkPresent12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags)
{
	auto prev_ctx = ImGui::GetCurrentContext();

	if (!initDX12)
	{
		initDX12 = DX12Init(pSwapChain);
	}

	ImGui::SetCurrentContext(state.imgui_ctx);
	check_keybinds(params);
	update_hud_info(sw_stats, params, vendorID);
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	position_layer(sw_stats, params, window_size);
	render_imgui(sw_stats, params, window_size, "D3D12");
	ImGui::EndFrame();
	FrameContext& currentFrameContext = frameContext[pSwapChain->GetCurrentBackBufferIndex()];
	//currentFrameContext.commandAllocator->Reset();

	D3D12_RESOURCE_BARRIER barrier;
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = currentFrameContext.main_render_target_resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

	d3d12CommandList->Reset(currentFrameContext.commandAllocator, nullptr);
	d3d12CommandList->ResourceBarrier(1, &barrier);
	d3d12CommandList->OMSetRenderTargets(1, &currentFrameContext.main_render_target_descriptor, FALSE, nullptr);
	d3d12CommandList->SetDescriptorHeaps(1, d3d12DescriptorHeapImGuiRender.GetAddressOf());
	// printf("imgui/d3d12 render\n");
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), d3d12CommandList.Get());

	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

	d3d12CommandList->ResourceBarrier(1, &barrier);
	d3d12CommandList->Close();

	d3d12CommandQueue->ExecuteCommandLists(1, (ID3D12CommandList * const *)d3d12CommandList.GetAddressOf());

	//ImGui::SetCurrentContext(prev_ctx);
	return std::invoke(oPresent, pSwapChain, SyncInterval, Flags);
}

void hookExecuteCommandListsD3D12(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists) {
	if (!d3d12CommandQueue)
		d3d12CommandQueue = queue;

	oExecuteCommandListsD3D12(queue, NumCommandLists, ppCommandLists);
}

HRESULT hookSignalD3D12(ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64 value) {
	if (d3d12CommandQueue != nullptr && queue == d3d12CommandQueue.Get()) {
		d3d12Fence = fence;
		d3d12FenceValue = value;
	}

	return oSignalD3D12(queue, fence, value);
}

HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* pThis, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    auto allocator = frameContext[0].commandAllocator;
    auto rtvHandle = frameContext[0].main_render_target_descriptor;
    for (size_t i = 0; i < frameContext.size(); i++) {
        frameContext[i].main_render_target_resource->Release();
    }

    auto hr = oResizeBuffers(pThis, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    if (hr != S_OK)
		printf("ResizeBuffers Failed\n");

    frameContext.resize(BufferCount);

    const auto rtvDescriptorSize = d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (size_t i = 0; i < frameContext.size(); i++) {
        ID3D12Resource* pBackBuffer = nullptr;

        frameContext[i].main_render_target_descriptor = rtvHandle;
        pThis->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
        d3d12Device->CreateRenderTargetView(pBackBuffer, nullptr, rtvHandle);
        frameContext[i].main_render_target_resource = pBackBuffer;
        rtvHandle.ptr += rtvDescriptorSize;
    }

    ImGui_ImplDX12_Resize(BufferCount);
    return hr;
}

void impl::d3d12::init()
{
	printf("init dx12\n");
	auto ret = kiero::bind(54, (void**)&oExecuteCommandListsD3D12, hookExecuteCommandListsD3D12);
	assert(ret == kiero::Status::Success);
	ret = kiero::bind(58, (void**)&oSignalD3D12, reinterpret_cast<void*>(hookSignalD3D12));
	assert(ret == kiero::Status::Success);
	ret = kiero::bind(140, (void**)&oPresent, hkPresent12);
	assert(ret == kiero::Status::Success);
	ret = kiero::bind(145, (void**)&oResizeBuffers, hkResizeBuffers);
    assert(ret == kiero::Status::Success);
}

#endif // KIERO_INCLUDE_D3D12
