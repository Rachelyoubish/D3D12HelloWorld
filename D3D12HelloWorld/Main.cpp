#include "hwpch.h"
#include "App.h"

int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow )
{
   // Windows 10 Creators update adds Per Monitor V2 DPI awareness context.
   // Using this awareness context allows the client area of the window 
   // to achieve 100% scaling while still allowing non-client window content to 
   // be rendered in a DPI sensitive fashion.
    SetThreadDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );

    App sample( 1280, 720, L"D3D12 Hello Texture" );
    return Window::Run( &sample, hInstance, nCmdShow );
}