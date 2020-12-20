#include "hwpch.h"
#include "Window.h"

HWND Window::m_hWnd = nullptr;

int Window::Run( DXSample* pSample, HINSTANCE hInstance, int nCmdShow )
{
    // Initialize the window class.
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof( wc );
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor( nullptr, IDC_ARROW );
    wc.lpszClassName = L"DXSampleClass";
    RegisterClassEx( &wc );

    RECT windowRect = { 0, 0, static_cast<LONG>(pSample->GetWidth()), static_cast<LONG>( pSample->GetHeight() ) };
    AdjustWindowRect( &windowRect, WS_OVERLAPPED, false );

    // Create the window and store the handle to it. 
    m_hWnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        pSample->GetTitle(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        hInstance,
        pSample );

    // Initialize the sample. OnInit is defined in each child-implementation of DXSample.
    pSample->OnInit();

    ShowWindow( m_hWnd, nCmdShow );

    // Main sample loop.
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        // Process any messages in the queue.
        if (PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ))
        {
            TranslateMessage( &msg );
            DispatchMessage( &msg );
        }

        pSample->OnUpdate();
        pSample->OnRender();
    }

    pSample->OnDestroy();

    // Return this part of the WM_QUIT message to Windows.
    return static_cast<char>( msg.wParam );
}

LRESULT CALLBACK Window::WindowProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    DXSample* pSample = reinterpret_cast<DXSample*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) );

    switch (message)
    {
        case WM_CREATE:
        {
            // Save the DXSample* passed in to CreateWindow.
            LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>( lParam );
            SetWindowLongPtr( hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( pCreateStruct->lpCreateParams ) );
        }
        return 0;

        // TODO: Move into main loop after D312 is confirmed to be running.
        case WM_PAINT:
            if (pSample)
            {
               // pSample->OnUpdate();
               // pSample->OnRender();
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage( 0 );
            return 0;
    }

    // Handle any messages the switch statement didn't.
    return DefWindowProc( hWnd, message, wParam, lParam );
}
