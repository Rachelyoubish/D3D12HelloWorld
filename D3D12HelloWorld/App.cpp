#include "hwpch.h"
#include "App.h"

App::App( uint32_t width, uint32_t height, std::wstring name )
    : DXSample( width, height, name ),
    m_FrameIndex( 0 ),
    m_Viewport( 0.0f, 0.0f, static_cast<float>( width ), static_cast<float>( height ) ),
    m_ScissorRect( 0, 0, static_cast<LONG>( width ), static_cast<LONG>( height ) ),
    m_rtvDescriptorSize( 0 )
{}

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

    WaitForPreviousFrame();
}

void App::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForPreviousFrame();

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
        }
    }

    ThrowIfFailed( m_Device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &m_CommandAllocator ) ) );
}


// Load the sample assets.
void App::LoadAssets()
{
    // Create an empty root signature.
    {
        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init( 0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT );

        Microsoft::WRL::ComPtr<ID3DBlob> signature;
        Microsoft::WRL::ComPtr<ID3DBlob> error;
        ThrowIfFailed( D3D12SerializeRootSignature( &rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error ) );
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
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
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
    ThrowIfFailed( m_Device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CommandAllocator.Get(), m_PipelineState.Get(), IID_PPV_ARGS( &m_CommandList ) ) );

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now. 
    ThrowIfFailed( m_CommandList->Close() );

    // Create the vertex buffer.
    {
        Vertex triangleVertices[] =
        {
            { { 0.0f, 0.25f * m_aspectRatio, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
            { { 0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
            { { -0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
        };

        const uint32_t vertexBufferSize = sizeof( triangleVertices );

        // Note: using upload heaps to transfer static data like vert buffers is not 
        // recommended. Every time the GPU needs it, the upload heap will be marshalled 
        // over. Please read up on Default Heap usage. An upload heap is used here for 
        // code simplicity and because there are very few verts to actually transfer.
        auto heapType = CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD );
        auto buffer = CD3DX12_RESOURCE_DESC::Buffer( vertexBufferSize );
        ThrowIfFailed( m_Device->CreateCommittedResource(
            &heapType,
            D3D12_HEAP_FLAG_NONE,
            &buffer,
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

    // Create synchronization objects.
    {
        ThrowIfFailed( m_Device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_Fence ) ) );
        m_FenceValue = 1;

        // Create an event handle to use for synchronization.
        m_FenceEvent = CreateEvent( nullptr, false, false, nullptr );
        if (m_FenceEvent == nullptr)
        {
            ThrowIfFailed( HRESULT_FROM_WIN32( GetLastError() ) );
        }
    }

    // Wait for the command list to execute; we are reusing the same command 
    // list in our main loop but for now, we just want to wait for setup to 
    // complete before continuing.
    WaitForPreviousFrame();
}

void App::PopulateCommandList()
{
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    ThrowIfFailed( m_CommandAllocator->Reset() );

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed( m_CommandList->Reset( m_CommandAllocator.Get(), m_PipelineState.Get() ) );

    // Set necessary state.
    m_CommandList->SetGraphicsRootSignature( m_RootSignature.Get() );
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

void App::WaitForPreviousFrame()
{
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
    // sample illustrates how to use fences for efficient resource usage and to
    // maximize GPU utilization.

    // Signal and increment the fence value.
    const uint64_t fence = m_FenceValue;
    ThrowIfFailed( m_CommandQueue->Signal( m_Fence.Get(), fence ) );
    m_FenceValue++;

    // Wait until the previous frame is finished.
    if (m_Fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed( m_Fence->SetEventOnCompletion( fence, m_FenceEvent ) );
        WaitForSingleObject( m_FenceEvent, INFINITE );
    }

    m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
}