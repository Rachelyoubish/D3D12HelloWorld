#include "hwpch.h"
#include "App.h"

App::App( uint32_t width, uint32_t height, std::wstring name )
    : m_width(width), m_height(height), m_title( name ),
    m_FrameIndex( 0 ),
    m_Viewport( 0.0f, 0.0f, static_cast<float>( width ), static_cast<float>( height ) ),
    m_ScissorRect( 0, 0, static_cast<LONG>( width ), static_cast<LONG>( height ) ),
    m_rtvDescriptorSize( 0 ),
    m_useWarpDevice(false)
{
    WCHAR assetsPath[512];
    GetAssetsPath( assetsPath, _countof( assetsPath ) );
    m_assetsPath = assetsPath;

    m_aspectRatio = static_cast<float>( width ) / static_cast<float>( height );
}

void App::OnInit()
{
    LoadPipeline();
    LoadAssets();
}


// Update frame-based values.
void App::OnUpdate()
{
}

// Render the scene.
void App::OnRender()
{
    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_CommandList.Get() };
    m_CommandQueue->ExecuteCommandLists( _countof( ppCommandLists ), ppCommandLists );

    // Present the frame.
    ThrowIfFailed( m_SwapChain->Present( 1, 0 ) );

    MoveToNextFrame();
}

void App::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForGpu();

    CloseHandle(m_FenceEvent);
}

void App::LoadPipeline()
{
    uint32_t dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugController ) ) ))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed( CreateDXGIFactory2( dxgiFactoryFlags, IID_PPV_ARGS( &factory ) ) );

    if (m_useWarpDevice)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed( factory->EnumWarpAdapter( IID_PPV_ARGS( &warpAdapter ) ) );

        ThrowIfFailed( D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS( &m_Device )
        ) );
    }
    else
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter( factory.Get(), &hardwareAdapter );

        ThrowIfFailed( D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS( &m_Device )
        ) );
    }

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed( m_Device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &m_CommandQueue ) ) );

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed( factory->CreateSwapChainForHwnd(
        m_CommandQueue.Get(), // Swap chain needs the queue so that it can force a flush on it.
        Window::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
    ) );

    // This sample does not support fullscreen transition.
    ThrowIfFailed( factory->MakeWindowAssociation( Window::GetHwnd(), DXGI_MWA_NO_ALT_ENTER ) );

    ThrowIfFailed( swapChain.As( &m_SwapChain ) );
    m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed( m_Device->CreateDescriptorHeap( &rtvHeapDesc, IID_PPV_ARGS( &m_rtvHeap ) ) );

        // Describe and create a shader resource view (SRV) heap for the texture.
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 1;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed( m_Device->CreateDescriptorHeap( &srvHeapDesc, IID_PPV_ARGS( &m_srvHeap ) ) );

        m_rtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle( m_rtvHeap->GetCPUDescriptorHandleForHeapStart() );

        // Create a RTV for each frame.
        for (uint32_t n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed( m_SwapChain->GetBuffer( n, IID_PPV_ARGS( &m_RenderTargets[n] ) ) );
            m_Device->CreateRenderTargetView( m_RenderTargets[n].Get(), nullptr, rtvHandle );
            rtvHandle.Offset( 1, m_rtvDescriptorSize );

            ThrowIfFailed( m_Device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &m_CommandAllocators[n] ) ) );
        }
    }
}


// Load the sample assets.
void App::LoadAssets()
{
    // Create the root signature.
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (FAILED( m_Device->CheckFeatureSupport( D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof( featureData ) ) ))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
        ranges[0].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC );

        CD3DX12_ROOT_PARAMETER1 rootParameters[1];
        rootParameters[0].InitAsDescriptorTable( 1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL );

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.MipLODBias = 0;
        sampler.MaxAnisotropy = 0;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1( _countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT );

        Microsoft::WRL::ComPtr<ID3DBlob> signature;
        Microsoft::WRL::ComPtr<ID3DBlob> error;
        ThrowIfFailed( D3DX12SerializeVersionedRootSignature( &rootSignatureDesc, featureData.HighestVersion, &signature, &error ) );
        ThrowIfFailed( m_Device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &m_RootSignature ) ) );
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        uint32_t compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        uint32_t compileFlags = 0;
#endif

        ThrowIfFailed( D3DCompileFromFile( GetAssetFullPath( L"Shaders.hlsl" ).c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr ) );
        ThrowIfFailed( D3DCompileFromFile( GetAssetFullPath( L"Shaders.hlsl" ).c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr ) );

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof( inputElementDescs ) };
        psoDesc.pRootSignature = m_RootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE( vertexShader.Get() );
        psoDesc.PS = CD3DX12_SHADER_BYTECODE( pixelShader.Get() );
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC( D3D12_DEFAULT );
        psoDesc.BlendState = CD3DX12_BLEND_DESC( D3D12_DEFAULT );
        psoDesc.DepthStencilState.DepthEnable = false;
        psoDesc.DepthStencilState.StencilEnable = false;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;
        ThrowIfFailed( m_Device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &m_PipelineState ) ) );
    }

    // Create the command list.
    ThrowIfFailed( m_Device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CommandAllocators[m_FrameIndex].Get(), m_PipelineState.Get(), IID_PPV_ARGS( &m_CommandList ) ) );

    // Create the vertex buffer.
    {
        Vertex triangleVertices[] =
        {
            { { 0.0f, 0.25f * m_aspectRatio, 0.0f }, { 0.5f, 0.0f } },
            { { 0.25f, -0.25f * m_aspectRatio, 0.0f }, { 1.0f, 1.0f } },
            { { -0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 1.0f } }
        };

        const uint32_t vertexBufferSize = sizeof( triangleVertices );

        // Note: using upload heaps to transfer static data like vert buffers is not 
        // recommended. Every time the GPU needs it, the upload heap will be marshalled 
        // over. Please read up on Default Heap usage. An upload heap is used here for 
        // code simplicity and because there are very few verts to actually transfer.
        ThrowIfFailed( m_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer( vertexBufferSize ),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS( &m_VertexBuffer ) 
        ) );

        // Copy the triangle data to the vertex buffer. 
        uint8_t* pVertexDataBegin;
        CD3DX12_RANGE readRange( 0, 0 ); // We do not intend to read from this resource on the CPU.
        ThrowIfFailed( m_VertexBuffer->Map( 0, &readRange, reinterpret_cast<void**>( &pVertexDataBegin ) ) );
        memcpy( pVertexDataBegin, triangleVertices, sizeof( triangleVertices ) );
        m_VertexBuffer->Unmap( 0, nullptr );

        // Initialize teh vertex buffer view.
        m_VertexBufferView.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress();
        m_VertexBufferView.StrideInBytes = sizeof( Vertex );
        m_VertexBufferView.SizeInBytes = vertexBufferSize;
    }

    // Note: ComPtr's are CPU objects but this resource needs to stay in scope until
    // the command list that references it has finished executing on the GPU.
    // We will flush the GPU at the end of this method to ensure the resource is not
    // prematurely destroyed.
    ComPtr<ID3D12Resource> textureUploadHeap;

    // Create the texture.
    {
        // Describe and create a Texture2D.
        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.MipLevels = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.Width = TextureWidth;
        textureDesc.Height = TextureHeight;
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

        ThrowIfFailed( m_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS( &m_Texture )
        ) );

        const uint64_t uploadBufferSize = GetRequiredIntermediateSize( m_Texture.Get(), 0, 1 );

        // Create the GPU upload buffer.
        ThrowIfFailed( m_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer( uploadBufferSize ),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS( &textureUploadHeap )
        ) );

        // Copy data to the intermediate upload heap and then schedule a copy
        // from the upload heap to the Texture2D. 
        std::vector<uint8_t> texture = GenerateTextureData();

        D3D12_SUBRESOURCE_DATA textureData = {};
        textureData.pData = &texture[0];
        textureData.RowPitch = TextureWidth * TexturePixelSize;
        textureData.SlicePitch = textureData.RowPitch * TextureHeight;

        UpdateSubresources( m_CommandList.Get(), m_Texture.Get(), textureUploadHeap.Get(), 0, 0, 1, &textureData );
        m_CommandList->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( m_Texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE ) );

        // Describe and create a SRV for the texture.
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        m_Device->CreateShaderResourceView( m_Texture.Get(), &srvDesc, m_srvHeap->GetCPUDescriptorHandleForHeapStart() );
    }

    // Close the command list and execute it to begin the initial GPU setup.
    ThrowIfFailed( m_CommandList->Close() );
    ID3D12CommandList* ppCommandLists[] = { m_CommandList.Get() };
    m_CommandQueue->ExecuteCommandLists( _countof( ppCommandLists ), ppCommandLists );

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        ThrowIfFailed( m_Device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_Fence ) ) );
        m_FenceValues[m_FrameIndex]++;

        // Create an event handle to use for synchronization.
        m_FenceEvent = CreateEvent( nullptr, false, false, nullptr );
        if (m_FenceEvent == nullptr)
        {
            ThrowIfFailed( HRESULT_FROM_WIN32( GetLastError() ) );
        }

        // Wait for the command list to execute; we are reusing the same command 
        // list in our main loop but for now, we just want to wait for setup to 
        // complete before continuing.
        WaitForGpu();
    }
}

// Generate a simple black and white checkerboard texture.
std::vector<uint8_t> App::GenerateTextureData()
{
    const uint32_t rowPitch = TextureWidth * TexturePixelSize;
    const uint32_t cellPitch = rowPitch >> 3;        // The width of a cell in the checkboard texture.
    const uint32_t cellHeight = TextureWidth >> 3;    // The height of a cell in the checkerboard texture.
    const uint32_t textureSize = rowPitch * TextureHeight;

    std::vector<uint8_t> data(textureSize);
    uint8_t* pData = &data[0];

    for (uint32_t n = 0; n < textureSize; n += TexturePixelSize)
    {
        uint32_t x = n % rowPitch;
        uint32_t y = n / rowPitch;
        uint32_t i = x / cellPitch;
        uint32_t j = y / cellHeight;

        if (i % 2 == j % 2)
        {
            pData[n] = 0x00;        // R
            pData[n + 1] = 0x00;    // G
            pData[n + 2] = 0x00;    // B
            pData[n + 3] = 0xff;    // A
        }
        else
        {
            pData[n] = 0xff;        // R
            pData[n + 1] = 0xff;    // G
            pData[n + 2] = 0xff;    // B
            pData[n + 3] = 0xff;    // A
        }
    }

    return data;
}

void App::PopulateCommandList()
{
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    ThrowIfFailed( m_CommandAllocators[m_FrameIndex]->Reset() );

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed( m_CommandList->Reset( m_CommandAllocators[m_FrameIndex].Get(), m_PipelineState.Get() ) );

    // Set necessary state.
    m_CommandList->SetGraphicsRootSignature( m_RootSignature.Get() );

    ID3D12DescriptorHeap* ppHeaps[] = { m_srvHeap.Get() };
    m_CommandList->SetDescriptorHeaps( _countof( ppHeaps ), ppHeaps );

    m_CommandList->SetGraphicsRootDescriptorTable( 0, m_srvHeap->GetGPUDescriptorHandleForHeapStart() );
    m_CommandList->RSSetViewports( 1, &m_Viewport );
    m_CommandList->RSSetScissorRects( 1, &m_ScissorRect );

    // Indicate that the back buffer will be used as a render target.
    auto rtv = CD3DX12_RESOURCE_BARRIER::Transition( m_RenderTargets[m_FrameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET );
    m_CommandList->ResourceBarrier( 1, &rtv );

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle( m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_FrameIndex, m_rtvDescriptorSize );
    m_CommandList->OMSetRenderTargets( 1, &rtvHandle, false, nullptr );

    // Record commands.
    const float clearColor[] = { 0.16f, 0.16f, 0.16f, 1.0f };
    m_CommandList->ClearRenderTargetView( rtvHandle, clearColor, 0, nullptr );
    m_CommandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    m_CommandList->IASetVertexBuffers( 0, 1, &m_VertexBufferView );
    m_CommandList->DrawInstanced( 3, 1, 0, 0 );

    // Indicate that the back buffer will now be used to present.
    auto present = CD3DX12_RESOURCE_BARRIER::Transition( m_RenderTargets[m_FrameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT );
    m_CommandList->ResourceBarrier( 1, &present);

    ThrowIfFailed( m_CommandList->Close() );
}

// Prepare to render the next frame.
void App::MoveToNextFrame()
{
    // Schedule a Signal command in the queue.
    const uint64_t currentFenceValue = m_FenceValues[m_FrameIndex];
    ThrowIfFailed( m_CommandQueue->Signal( m_Fence.Get(), currentFenceValue ) );

    // Update the frame index.
    m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it's ready.
    if (m_Fence->GetCompletedValue() < m_FenceValues[m_FrameIndex])
    {
        ThrowIfFailed( m_Fence->SetEventOnCompletion( m_FenceValues[m_FrameIndex], m_FenceEvent ) );
        WaitForSingleObjectEx( m_FenceEvent, INFINITE, false );
    }

    // Set the fence value for the next frame.
    m_FenceValues[m_FrameIndex] = currentFenceValue + 1;
}


// Wait for pending GPU work to complete.
void App::WaitForGpu()
{
    // Schedule a Signal command in the queue.
    ThrowIfFailed( m_CommandQueue->Signal( m_Fence.Get(), m_FenceValues[m_FrameIndex] ) );

    // Wait until the fence has been processed.
    ThrowIfFailed( m_Fence->SetEventOnCompletion( m_FenceValues[m_FrameIndex], m_FenceEvent ) );
    WaitForSingleObjectEx( m_FenceEvent, INFINITE, false );

    // Increment the fence value for the current frame.
    m_FenceValues[m_FrameIndex]++;
}

// Helper function for resolving the full path of assets.
std::wstring App::GetAssetFullPath( LPCWSTR assetName )
{
    return m_assetsPath + assetName;
}

// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, *ppAdapter will be set to nullptr.
_Use_decl_annotations_
void App::GetHardwareAdapter(
    IDXGIFactory1* pFactory,
    IDXGIAdapter1** ppAdapter,
    bool requestHighPerformanceAdapter )
{
    *ppAdapter = nullptr;

    ComPtr<IDXGIAdapter1> adapter;

    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED( pFactory->QueryInterface( IID_PPV_ARGS( &factory6 ) ) ))
    {
        for (
            UINT adapterIndex = 0;
            DXGI_ERROR_NOT_FOUND != factory6->EnumAdapterByGpuPreference(
                adapterIndex,
                requestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                IID_PPV_ARGS( &adapter ) );
            ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1( &desc );

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED( D3D12CreateDevice( adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof( ID3D12Device ), nullptr ) ))
            {
                break;
            }
        }
    }
    else
    {
        for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1( adapterIndex, &adapter ); ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1( &desc );

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED( D3D12CreateDevice( adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof( ID3D12Device ), nullptr ) ))
            {
                break;
            }
        }
    }

    *ppAdapter = adapter.Detach();
}