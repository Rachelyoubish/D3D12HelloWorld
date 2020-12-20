#pragma once

#include "DXSample.h"

class DXSample;

class Window
{
public:
    static int Run( DXSample* pSample, HINSTANCE hInstance, int nCmdShow );
    static HWND GetHwnd() { return m_hWnd; }

private:
    static LRESULT CALLBACK WindowProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam );
private:
    static HWND m_hWnd;
};