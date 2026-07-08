#include "..\inc\MyFirstPluginPr.h"
#include "WebViewWindow.h"

// ================== Custom Dialog Command ==================
int MyOpenDialog(void);

// ================== Web Page URL (change to your classmate's page) ==================
static const wchar_t* WEB_PAGE_URL = L"http://localhost:5173/";

/*
DESCRIPTION:
   Register custom command.
*/
int RegisterCustomCommand(void)
{
    cvxCmdFunc("MyOpenDialog", (void*)MyOpenDialog, VX_CODE_GENERAL);
    return 0;
}

/*
DESCRIPTION:
   Unload custom command.
*/
int UnloadCustomCommand(void)
{
    CloseWebViewWindow();
    cvxCmdFuncUnload("MyOpenDialog");
    return 0;
}

/*
DESCRIPTION:
   Open a window with embedded web browser.
*/
int MyOpenDialog(void)
{
    ShowWebViewWindow(WEB_PAGE_URL);
    return 0;
}
