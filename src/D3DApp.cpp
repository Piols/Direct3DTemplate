#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include "D3DApp.h"

namespace {
    const INT FrameCount = 2;
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGISwapChain3> swapChain;
    ComPtr<IDXGIFactory7> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Resource> renderTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
    ComPtr<ID3D12PipelineState> pipelineState;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    UINT rtvDescriptorSize;

    ComPtr<ID3D12Fence> fence;
    UINT frameIndex;
    UINT64 fenceValue;
    HANDLE fenceEvent;
}

void PopulateCommandList(HWND hwnd) {
    HRESULT hr = commandAllocator->Reset();
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        return;
    }
    hr = commandList->Reset(commandAllocator.Get(), pipelineState.Get());
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        return;
    }

    D3D12_RESOURCE_BARRIER barrier = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition = {renderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET}
    };
    commandList->ResourceBarrier(1, &barrier);

    rtvHandle.ptr = SIZE_T(INT64(rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr) + INT64(frameIndex) * INT64(rtvDescriptorSize));
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    const float clearColor[] = { 0.0f, 0.8f, 0.8f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    barrier = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition = {renderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET}
    };

    hr = commandList->Close();
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        return;
    }
}

void WaitForPreviousFrame(HWND hwnd) {
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. More advanced samples 
    // illustrate how to use fences for efficient resource usage.

    // Signal and increment the fence value.
    const UINT64 fenceVal = fenceValue;
    HRESULT hr = commandQueue->Signal(fence.Get(), fenceVal);
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        return;
    }
    fenceValue++;

    // Wait until the previous frame is finished.
    if (fence->GetCompletedValue() < fenceVal)
    {
        hr = fence->SetEventOnCompletion(fenceVal, fenceEvent);
        if (FAILED(hr)) {
            DestroyWindow(hwnd);
            return;
        }
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    frameIndex = swapChain->GetCurrentBackBufferIndex();
}

void InitDirect3D(HWND hwnd) {
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        return;
    }

    hr = D3D12CreateDevice(
        nullptr,
        D3D_FEATURE_LEVEL_12_0,
        IID_PPV_ARGS(&device)
    );
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        return;
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        return;
    }

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = 0;
    swapChainDesc.Height = 0;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;
    ComPtr<IDXGISwapChain1> tempSwapChain;
    hr = factory->CreateSwapChainForHwnd(
        commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
        hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &tempSwapChain
    );
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        return;
    }
    hr = tempSwapChain.As(&swapChain);
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        return;
    }

    frameIndex = swapChain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = (device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)));
    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        return;
    }

    rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT n = 0; n < FrameCount; n++) {
        hr = swapChain->GetBuffer(n, IID_PPV_ARGS(&renderTargets[n]));
        if (FAILED(hr)) {
            DestroyWindow(hwnd);
            return;
        }
        device->CreateRenderTargetView(renderTargets[n].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += rtvDescriptorSize;
    }

    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        return;
    }

    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), pipelineState.Get(), IID_PPV_ARGS(&commandList));
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        return;
    }
    hr = commandList->Close();
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        return;
    }

    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        return;
    }
    fenceValue = 1;

    // Create an event handle to use for frame synchronization.
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (fenceEvent == nullptr)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        if (FAILED(hr)) {
            DestroyWindow(hwnd);
            return;
        }
    }

    // Wait for the command list to execute; we are reusing the same command 
    // list in our main loop but for now, we just want to wait for setup to 
    // complete before continuing.
    WaitForPreviousFrame(hwnd);

}


void OnRender(HWND hwnd) {
    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList(hwnd);

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    HRESULT hr = swapChain->Present(1, 0);
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        return;
    }

    WaitForPreviousFrame(hwnd);
}


void OnDestroy(HWND hwnd) {

    // Wait for the GPU to be done with all resources.
    WaitForPreviousFrame(hwnd);

    CloseHandle(fenceEvent);
}