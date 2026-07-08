#pragma once

// Embedded Web Browser Window
// Powered by Microsoft Edge WebView2 (Chromium engine).
// Requires: Microsoft Edge WebView2 Runtime + Microsoft.Web.WebView2 NuGet package

#ifdef __cplusplus
extern "C" {
#endif

// Show the web browser window and navigate to the specified URL.
// If the window already exists, it is brought to front and navigated to url.
// url: Web page address (supports http://, https://, file://, etc.)
// Returns 0 on success, non-zero on failure.
int ShowWebViewWindow(const wchar_t* url);

// Close the web browser window if it is open.
void CloseWebViewWindow();

// Check whether the web browser window is currently open.
// Returns 1 if open, 0 otherwise.
int IsWebViewWindowOpen();

#ifdef __cplusplus
}
#endif
