#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void InitDirect3D(HWND hwnd);
//void RecreateRenderTarget(HWND hwnd);
//void DestroyRenderTarget();
void DestroyDirect3D();
void OnRender(HWND hwnd);