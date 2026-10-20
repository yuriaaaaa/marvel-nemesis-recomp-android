#pragma once
// Android: the Win32 names the game's PC settings code (src/common) touches. There is no window to send messages, no
// monitors to list and no keyboard focus, so the calls report nothing and the touch/pad paths do the work.
#include <chrono>
#include <cstdint>
#include <cstring>

typedef long HRESULT;
typedef void* HANDLE;
typedef void* HMODULE;
typedef void* HWND;
typedef void* HCURSOR;
typedef void* HMONITOR;
typedef const wchar_t* PCWSTR;
typedef unsigned int UINT;
typedef int BOOL;
typedef uint32_t DWORD;
typedef long LONG;
typedef unsigned long long ULONGLONG;
typedef uint16_t WORD;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef uintptr_t UINT_PTR;
#define WINAPI
#define CALLBACK
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

inline HMODULE GetModuleHandleW(const wchar_t*) { return nullptr; }
inline void* GetProcAddress(HMODULE, const char*) { return nullptr; }
inline HANDLE GetCurrentThread() { return nullptr; }

enum : uint32_t {
    WM_DESTROY = 0x0002, WM_ACTIVATE = 0x0006, WM_SETCURSOR = 0x0020, WM_DISPLAYCHANGE = 0x007E, WM_INPUT = 0x00FF,
    WM_KEYDOWN = 0x0100, WM_KEYUP = 0x0101, WM_CHAR = 0x0102, WM_SYSKEYDOWN = 0x0104, WM_SYSKEYUP = 0x0105,
    WM_SYSCOMMAND = 0x0112, WM_TIMER = 0x0113, WM_KILLFOCUS = 0x0008, WM_SETFOCUS = 0x0007, WM_ACTIVATEAPP = 0x001C,
    WM_LBUTTONUP = 0x0202, WM_APP = 0x8000,
};
enum { WA_INACTIVE = 0, HTCLIENT = 1, SC_KEYMENU = 0xF100 };
enum {
    VK_BACK = 0x08, VK_TAB = 0x09, VK_RETURN = 0x0D, VK_SHIFT = 0x10, VK_CONTROL = 0x11, VK_MENU = 0x12, VK_PAUSE = 0x13,
    VK_CAPITAL = 0x14, VK_ESCAPE = 0x1B, VK_SPACE = 0x20, VK_PRIOR = 0x21, VK_NEXT = 0x22, VK_END = 0x23, VK_HOME = 0x24,
    VK_LEFT = 0x25, VK_UP = 0x26, VK_RIGHT = 0x27, VK_DOWN = 0x28, VK_INSERT = 0x2D, VK_DELETE = 0x2E,
    VK_LWIN = 0x5B, VK_RWIN = 0x5C, VK_APPS = 0x5D,
    VK_NUMPAD0 = 0x60, VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3, VK_NUMPAD4, VK_NUMPAD5, VK_NUMPAD6, VK_NUMPAD7, VK_NUMPAD8,
    VK_NUMPAD9, VK_MULTIPLY, VK_ADD, VK_SEPARATOR, VK_SUBTRACT, VK_DECIMAL, VK_DIVIDE,
    VK_F1 = 0x70, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12,
    VK_NUMLOCK = 0x90, VK_SCROLL = 0x91, VK_LSHIFT = 0xA0, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU,
    VK_OEM_1 = 0xBA, VK_OEM_PLUS, VK_OEM_COMMA, VK_OEM_MINUS, VK_OEM_PERIOD, VK_OEM_2, VK_OEM_3,
    VK_OEM_4 = 0xDB, VK_OEM_5, VK_OEM_6, VK_OEM_7,
};
#define LOWORD(v) ((uint16_t)((uintptr_t)(v) & 0xFFFF))
#define HIWORD(v) ((uint16_t)(((uintptr_t)(v) >> 16) & 0xFFFF))
#define MAKEINTRESOURCEW(i) ((const wchar_t*)(uintptr_t)(uint16_t)(i))
inline HCURSOR LoadCursorW(void*, const wchar_t*) { return nullptr; }
inline HCURSOR SetCursor(HCURSOR) { return nullptr; }
#define IDC_ARROW MAKEINTRESOURCEW(32512)
inline short GetAsyncKeyState(int) { return 0; }
inline short GetKeyState(int) { return 0; }

struct RECT { long left, top, right, bottom; };
struct POINT { long x, y; };
inline BOOL GetClientRect(HWND, RECT* r) { if (r) *r = {}; return FALSE; }
inline BOOL PostMessageW(HWND, UINT, WPARAM, LPARAM) { return FALSE; }
inline LRESULT SendMessageW(HWND, UINT, WPARAM, LPARAM) { return 0; }
inline UINT_PTR SetTimer(HWND, UINT_PTR, UINT, void*) { return 0; }
inline BOOL KillTimer(HWND, UINT_PTR) { return FALSE; }
inline HWND GetForegroundWindow() { return nullptr; }
inline BOOL IsWindow(HWND) { return FALSE; }
inline BOOL IsWindowVisible(HWND) { return FALSE; }
inline BOOL IsIconic(HWND) { return FALSE; }
inline BOOL ShowCursor(BOOL) { return 0; }

enum { MONITOR_DEFAULTTONULL = 0, MONITOR_DEFAULTTOPRIMARY = 1, MONITOR_DEFAULTTONEAREST = 2 };
struct MONITORINFO { DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; };
struct MONITORINFOEXW : MONITORINFO { wchar_t szDevice[32]; };
inline HMONITOR MonitorFromWindow(HWND, DWORD) { return nullptr; }
inline BOOL GetMonitorInfoW(HMONITOR, MONITORINFO*) { return FALSE; }
struct DEVMODEW {
    DWORD dmSize = sizeof(DEVMODEW), dmPelsWidth = 0, dmPelsHeight = 0, dmDisplayFrequency = 0, dmBitsPerPel = 0, dmFields = 0;
};
#define ENUM_CURRENT_SETTINGS ((DWORD)-1)
#define ENUM_REGISTRY_SETTINGS ((DWORD)-2)
inline BOOL EnumDisplaySettingsW(const wchar_t*, DWORD, DEVMODEW*) { return FALSE; }
inline UINT MapVirtualKeyW(UINT, UINT) { return 0; }
inline int GetKeyNameTextW(long, wchar_t*, int n) { return 0; }
#define MAPVK_VK_TO_VSC 0

inline ULONGLONG GetTickCount64() {
    using namespace std::chrono;
    return ULONGLONG(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
inline BOOL GetCursorPos(POINT* p) { if (p) *p = {}; return FALSE; }
inline HWND WindowFromPoint(POINT) { return nullptr; }
inline BOOL ScreenToClient(HWND, POINT*) { return FALSE; }
inline BOOL PtInRect(const RECT* r, POINT p) { return r && p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom; }
#define MAPVK_VSC_TO_VK_EX 3
#define MAPVK_VK_TO_VSC_EX 4
#define CP_UTF8 65001
inline int WideCharToMultiByte(UINT, DWORD, const wchar_t* w, int n, char* out, int size, const char*, BOOL*) {
    if (!out) return n;
    int i = 0;
    for (; i < n && i < size; i++) out[i] = w[i] < 128 ? char(w[i]) : '?';
    return i;
}
