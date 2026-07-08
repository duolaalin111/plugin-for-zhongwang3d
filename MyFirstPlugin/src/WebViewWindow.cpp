// WebViewWindow.cpp
// Embedded Web Browser powered by Microsoft Edge WebView2 (Chromium engine).
// Requires: WebView2 Runtime + Microsoft.Web.WebView2 NuGet package

#include <windows.h>
#include <WebView2.h>
#include <wrl.h>
#include <string>
#include <cstdio>
#include <shlobj.h>

#include "WebViewWindow.h"
#include "..\inc\MyFirstPluginPr.h"

using namespace Microsoft::WRL;

// ==================== Constants ====================
static const wchar_t CLASS_NAME[]   = L"MyPluginWebViewWnd";
static const wchar_t WINDOW_TITLE[] = L"My Plugin - Web View";

// ==================== Global State ====================
static HWND                              g_hwnd       = nullptr;
static ComPtr<ICoreWebView2Controller>   g_controller;
static ComPtr<ICoreWebView2>             g_webview;
static bool                              g_initialized = false;

// ==================== Forward Declarations ====================
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void InitWebView2(HWND hwnd, const std::wstring& url);
void ResizeWebView();
void ReleaseWebView();

// ==================== Window Procedure ====================
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_SIZE:
        ResizeWebView();
        return 0;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        ReleaseWebView();
        g_hwnd = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// ==================== Resize WebView ====================
void ResizeWebView()
{
    if (g_controller && g_hwnd)
    {
        RECT rc;
        GetClientRect(g_hwnd, &rc);
        g_controller->put_Bounds(rc);
    }
}

// ==================== Release WebView ====================
void ReleaseWebView()
{
    if (g_controller)
    {
        g_controller->Close();
        g_controller = nullptr;
        g_webview = nullptr;
    }
    g_initialized = false;
}

// ==================== Get User Data Folder ====================
static std::wstring GetUserDataFolder()
{
    wchar_t tempPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, tempPath)))
    {
        std::wstring folder = std::wstring(tempPath) + L"\\MyPlugin_WebView2";
        CreateDirectoryW(folder.c_str(), nullptr);
        return folder;
    }
    return L"";
}

// ==================== Initialize WebView2 ====================
void InitWebView2(HWND hwnd, const std::wstring& url)
{
    if (g_initialized) return;
    g_initialized = true;

    std::wstring userFolder = GetUserDataFolder();

    // Step 1: Create WebView2 environment (async)
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,                     // browserExecutableFolder (null = use system Runtime)
        userFolder.empty() ? nullptr : userFolder.c_str(),
        nullptr,                     // environment options
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd, url](HRESULT result, ICoreWebView2Environment* env) -> HRESULT
            {
                if (FAILED(result) || !env)
                {
                    MessageBoxW(hwnd,
                        L"WebView2 initialization failed.\n\n"
                        L"Please install Microsoft Edge WebView2 Runtime:\n"
                        L"https://go.microsoft.com/fwlink/p/?LinkId=2124703",
                        L"WebView2 Error", MB_OK | MB_ICONERROR);
                    g_initialized = false;
                    return result;
                }

                // Step 2: Create the WebView2 controller for our window (async)
                env->CreateCoreWebView2Controller(
                    hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [hwnd, url](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT
                        {
                            if (FAILED(result) || !controller)
                            {
                                MessageBoxW(hwnd,
                                    L"Failed to create WebView2 controller.",
                                    L"WebView2 Error", MB_OK | MB_ICONERROR);
                                g_initialized = false;
                                return result;
                            }

                            g_controller = controller;
                            g_controller->get_CoreWebView2(&g_webview);

                            // Configure settings
                            ICoreWebView2Settings* settings = nullptr;
                            if (SUCCEEDED(g_webview->get_Settings(&settings)) && settings)
                            {
                                settings->put_IsScriptEnabled(TRUE);
                                settings->put_IsWebMessageEnabled(TRUE);
                                settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                                settings->put_AreDevToolsEnabled(TRUE);
                            }

                            // ---- ZW3D Bridge: inject window.bound before any page script runs ----
                            g_webview->AddScriptToExecuteOnDocumentCreated(
                                L"window.bound = { __ENV__: 'zw3d' };"
                                L"window.CefSharp = { BindObjectAsync: function(name) { return Promise.resolve(true); } };",
                                nullptr
                            );

                            // ---- Handle messages from web page (for future export/import commands) ----
                            g_webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT
                                    {
                                        LPWSTR msgRaw = nullptr;
                                        if (SUCCEEDED(args->get_WebMessageAsJson(&msgRaw)) && msgRaw)
                                        {
                                            // TODO: handle commands from web page (e.g. export, import)
                                            OutputDebugStringW(L"[ZW3D Plugin] WebMessage: ");
                                            OutputDebugStringW(msgRaw);
                                            OutputDebugStringW(L"\n");
                                            CoTaskMemFree(msgRaw);
                                        }
                                        return S_OK;
                                    }
                                ).Get(),
                                nullptr
                            );

                            // Resize to fill window
                            ResizeWebView();

                            // Navigate to URL
                            if (!url.empty())
                            {
                                g_webview->Navigate(url.c_str());
                            }

                            return S_OK;
                        }
                    ).Get()
                );

                return S_OK;
            }
        ).Get()
    );

    if (FAILED(hr))
    {
        char buf[256];
        sprintf_s(buf, "[WebView2] CreateEnvironment failed: 0x%08X", (unsigned)hr);
        cvxMsgDisp(buf);
        g_initialized = false;
    }
}

// ==================== Public API ====================

int ShowWebViewWindow(const wchar_t* url)
{
    // If window exists, bring to front and navigate
    if (g_hwnd && IsWindow(g_hwnd))
    {
        if (g_webview && url && wcslen(url) > 0)
        {
            g_webview->Navigate(url);
        }
        ShowWindow(g_hwnd, SW_SHOW);
        SetForegroundWindow(g_hwnd);
        return 0;
    }

    // ---- Create window ----
    HINSTANCE hInst = (HINSTANCE)GetModuleHandleW(L"MyFirstPlugin.dll");
    if (!hInst) hInst = (HINSTANCE)GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = (int)(sw * 0.65);
    int wh = (int)(sh * 0.65);

    g_hwnd = CreateWindowExW(WS_EX_APPWINDOW, CLASS_NAME, WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW, (sw - ww) / 2, (sh - wh) / 2, ww, wh,
        nullptr, nullptr, hInst, nullptr);

    if (!g_hwnd)
    {
        cvxMsgDisp("[WebView2] CreateWindowEx failed");
        return 1;
    }

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    // Init WebView2 asynchronously
    std::wstring targetUrl = (url && wcslen(url) > 0) ? url : L"https://www.baidu.com";
    InitWebView2(g_hwnd, targetUrl);

    return 0;
}

void CloseWebViewWindow()
{
    if (g_hwnd && IsWindow(g_hwnd))
    {
        ReleaseWebView();
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
}

int IsWebViewWindowOpen()
{
    return (g_hwnd && IsWindow(g_hwnd)) ? 1 : 0;
}
