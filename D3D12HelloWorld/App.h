#pragma once

#include "Helpers.h"
#include "Window.h"

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().

class App
{
public:
    App( uint32_t width, uint32_t height, std::wstring name );

    void OnInit();
    void OnUpdate();
    void OnRender();
    void OnDestroy();

    void LoadPipeline();
    void LoadAssets();
    std::vector<uint8_t> GenerateTextureData();
    void PopulateCommandList();
    // void WaitForPreviousFrame();
    void MoveToNextFrame();
    void WaitForGpu();

    // Accessors.
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    const wchar_t* GetTitle() const { return m_title.c_str(); }

private:
    std::wstring GetAssetFullPath( LPCWSTR assetName );

    void GetHardwareAdapter(
        _In_ IDXGIFactory1* pFactory,
        _Outptr_result_maybenull_ IDXGIAdapter1** ppAdapter,
        bool requestHighPerformanceAdapter = false );

private:
    static const uint32_t FrameCount = 2;
    static const uint32_t TextureWidth = 256;
    static const uint32_t TextureHeight = 256;
    static const uint32_t TexturePixelSize = 4;    // The number of bytes used to represent a pixel in the texture.

    struct Vertex
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT4 color;
    };

    struct SceneConstantBuffer
    {
        DirectX::XMFLOAT4 offset; // 16 bytes.
        float padding[60];        // Padding so the constant buffer is 256-byte aligned.
    };                            // Padding equals 240-bytes, so: 240 + 16 = 256.
    static_assert( ( sizeof( SceneConstantBuffer ) % 256 ) == 0, "Constant Buffer size must be 256-byte aligned" );

    // Viewport dimensions.
    uint32_t m_width;
    uint32_t m_height;
    float m_aspectRatio;

    // Adapter info.
    bool m_useWarpDevice;

private:
    // Root assets path.
    std::wstring m_assetsPath;

    // Window title.
    std::wstring m_title;

    // Pipeline objects.
    CD3DX12_VIEWPORT m_Viewport;
    CD3DX12_RECT m_ScissorRect;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_SwapChain;
    Microsoft::WRL::ComPtr<ID3D12Device> m_Device;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_RenderTargets[FrameCount];
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_CommandAllocators[FrameCount];
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_cbvHeap;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CommandList;
    uint32_t m_rtvDescriptorSize;

    // App resources.
    Microsoft::WRL::ComPtr<ID3D12Resource> m_VertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_Texture;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_ConstantBuffer;
    SceneConstantBuffer m_ConstantBufferData;
    uint8_t* m_pCbvDataBegin;

    // Synchronization objects.
    uint32_t m_FrameIndex;
    HANDLE m_FenceEvent;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
    uint64_t m_FenceValues[FrameCount];
};