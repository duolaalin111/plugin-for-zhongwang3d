// WebViewWindow.cpp
// Embedded Microsoft Edge WebView2 window for ZW3D.
//
// Test workflow implemented by the two toolbar buttons:
//
// 1. Import Part
//    Insert the existing Z3PRT directly into the current assembly.
//
// 2. Parametric Import
//    Copy the downloaded/template Z3PRT to an instances directory,
//    open the copy, change part expressions, save and close it,
//    restore the original assembly, and insert the modified copy.
//
// This file expects PartExpressionCommand_Fixed.cpp to remain in the
// same Visual Studio project because it calls:
//     SetCurrentPartExpression(name, expression)
//
// Front-end string messages supported:
//     directImport|C:\path\part.Z3PRT
//
//     parametricImport|C:\path\part.Z3PRT|
//         InDia=0.375;OutDia=0.875;Width=0.25
//
// If the path or parameter text is omitted, the test constants below
// are used.
//
// Requirements:
//     Microsoft Edge WebView2 Runtime
//     Microsoft.Web.WebView2 NuGet package
//     ZW3D C/C++ SDK

#include <windows.h>
#include <WebView2.h>
#include <wrl.h>

#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>

#include <shlobj.h>
#include <commdlg.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include "WebViewWindow.h"
#include "..\inc\MyFirstPluginPr.h"

#include "zwapi_cmd_assembly.h"
#include "zwapi_component.h"
#include "zwapi_matrix.h"
#include "zwapi_file.h"
#include "zwapi_root.h"
#include "zwapi_asm_comp.h"
#include "zwapi_part_var.h"

using namespace Microsoft::WRL;

// ============================================================
// Expression module dependency
// ============================================================

// Implemented in PartExpressionCommand_Fixed.cpp.
int SetCurrentPartExpression(
    const char* nameOrDescription,
    const char* newExpression);

// ============================================================
// Test configuration
// ============================================================

// Simulates a Z3PRT that has already been downloaded by the web side.
// ============================================================
// Test paths — used by toolbar buttons for local testing.
// ============================================================
static const char TEST_SOURCE_PART[] =
    "C:\\Users\\zxcvb\\Documents\\ZW3D\\testparts\\testBearing.Z3PRT";

static const char TEST_DOWNLOAD_DIRECTORY[] =
    "C:\\Users\\zxcvb\\Documents\\ZW3D\\testparts";

static const char TEST_INSTANCE_DIRECTORY[] =
    "D:\\ZW3D_Plugins\\testpart\\instances";

// ============================================================
// Production download path — used by frontend bridge.
// ============================================================
static const char PROD_DOWNLOAD_DIRECTORY[] =
    "D:\\ZW3D_Plugins\\testpart";

// Simulates the expression values selected by the front end.
static const char TEST_EXPRESSION_ASSIGNMENTS[] =
    "InDia=0.375;OutDia=0.875;Width=0.25";

// ============================================================
// Window constants
// ============================================================

static const wchar_t CLASS_NAME[] =
    L"MyPluginWebViewWnd";

static const wchar_t WINDOW_TITLE[] =
    L"My Plugin - Web View";

static const int BTN_IMPORT_PART =
    1001;

static const int BTN_IMPORT_PARAMETRIC =
    1002;

static const int BTN_BEARING_SMALL =
    1003;

static const int BTN_BEARING_LARGE =
    1004;

static const int BTN_BEARING_SHAPE =
    1005;

static const int BTN_BEARING_SHAPE2 =
    1006;

static const int BTN_PARAMETRIC_SHAPE =
    1007;

static const int BTN_BEARING_SHAPE3 =
    1008;

static const int BTN_CHECKIN =
    1009;

static const int BTN_ASM_NEWFILE =
    1010;

static const int BTN_ASM_SHAPE =
    1011;

static const int BTN_ASM_PARAMETRIC =
    1012;

static const int BTN_ASM_OPEN =
    1013;

static const int TOOLBAR_HEIGHT =
    48;

// ============================================================
// Global window state
// ============================================================

static HWND g_hwnd =
    nullptr;

static ComPtr<ICoreWebView2Controller> g_controller;
static ComPtr<ICoreWebView2> g_webview;

static bool g_initialized =
    false;

// ============================================================
// Forward declarations
// ============================================================

LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam);

void InitWebView2(
    HWND hwnd,
    const std::wstring& url);

void ResizeWebView();
void ReleaseWebView();

void CreateToolbar(
    HWND hwnd,
    HINSTANCE hInst);

void OnImportPart();
void OnImportParametric();
void OnBearingSmall();
void OnBearingLarge();
void OnBearingShape();
void OnBearingShape2();
void OnParametricShape();
void OnBearingShape3();
void OnCheckin();
void OnAsmNewFile();
void OnAsmShape();
void OnAsmParametric();
void OnAsmOpen();

// ============================================================
// Logging and string helpers
// ============================================================

static void DisplayMessage(
    const char* text)
{
    if (text == nullptr)
        return;

    cvxMsgDisp(text);
    OutputDebugStringA(text);
    OutputDebugStringA("\n");
}

static std::string Trim(
    const std::string& value)
{
    size_t begin = 0;

    while (begin < value.size() &&
           std::isspace(
               static_cast<unsigned char>(
                   value[begin])))
    {
        ++begin;
    }

    size_t end = value.size();

    while (end > begin &&
           std::isspace(
               static_cast<unsigned char>(
                   value[end - 1])))
    {
        --end;
    }

    return value.substr(
        begin,
        end - begin);
}

static std::vector<std::string> Split(
    const std::string& text,
    char delimiter)
{
    std::vector<std::string> result;

    size_t begin = 0;

    while (begin <= text.size())
    {
        const size_t end =
            text.find(delimiter, begin);

        if (end == std::string::npos)
        {
            result.push_back(
                text.substr(begin));
            break;
        }

        result.push_back(
            text.substr(
                begin,
                end - begin));

        begin = end + 1;
    }

    return result;
}

static bool FileExists(
    const char* path)
{
    if (path == nullptr ||
        path[0] == '\0')
    {
        return false;
    }

    const DWORD attributes =
        GetFileAttributesA(path);

    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool DirectoryExists(
    const char* path)
{
    if (path == nullptr ||
        path[0] == '\0')
    {
        return false;
    }

    const DWORD attributes =
        GetFileAttributesA(path);

    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool EnsureDirectory(
    const char* path)
{
    if (DirectoryExists(path))
        return true;

    if (CreateDirectoryA(
            path,
            nullptr))
    {
        return true;
    }

    return GetLastError() ==
           ERROR_ALREADY_EXISTS;
}

static bool SplitFilePath(
    const std::string& fullPath,
    std::string* directoryOut,
    std::string* fileNameOut)
{
    if (directoryOut == nullptr ||
        fileNameOut == nullptr)
    {
        return false;
    }

    const size_t separator =
        fullPath.find_last_of("\\/");

    if (separator == std::string::npos ||
        separator == 0 ||
        separator + 1 >= fullPath.size())
    {
        return false;
    }

    *directoryOut =
        fullPath.substr(0, separator);

    *fileNameOut =
        fullPath.substr(separator + 1);

    return true;
}

static std::string FileNameWithoutExtension(
    const std::string& path)
{
    const size_t separator =
        path.find_last_of("\\/");

    const size_t begin =
        separator == std::string::npos
            ? 0
            : separator + 1;

    const size_t dot =
        path.find_last_of('.');

    if (dot == std::string::npos ||
        dot < begin)
    {
        return path.substr(begin);
    }

    return path.substr(
        begin,
        dot - begin);
}

static std::string SanitizeFileToken(
    const std::string& value)
{
    std::string result;
    result.reserve(value.size());

    for (char character : value)
    {
        const unsigned char byte =
            static_cast<unsigned char>(
                character);

        if (std::isalnum(byte) ||
            character == '_' ||
            character == '-')
        {
            result.push_back(character);
        }
        else if (character == '.')
        {
            result.push_back('p');
        }
        else
        {
            result.push_back('_');
        }
    }

    return result;
}

// ============================================================
// ZW3D active document context
// ============================================================

struct ZwDocumentContext
{
    char file[600];
    char root[256];
    bool valid;
};

static ZwDocumentContext CaptureActiveContext()
{
    ZwDocumentContext context = {};

    cvxFileInqActive(
        context.file,
        static_cast<int>(
            sizeof(context.file)));

    cvxRootInqActive(
        context.root,
        static_cast<int>(
            sizeof(context.root)));

    context.valid =
        context.file[0] != '\0' &&
        context.root[0] != '\0';

    if (!context.valid)
    {
        DisplayMessage(
            "[PartOut] No active ZW3D file/root. "
            "Open and activate an assembly first.");
    }

    return context;
}

static int RestoreContext(
    const ZwDocumentContext& context)
{
    if (!context.valid)
        return -1;

    int ret =
        cvxFileActivate(context.file);

    if (ret != 0)
    {
        char message[1024] = {};

        sprintf_s(
            message,
            "[PartOut] cvxFileActivate failed: "
            "ret=%d, file=%s",
            ret,
            context.file);

        DisplayMessage(message);
        return ret;
    }

    char rootName[sizeof(context.root)] = {};
    strcpy_s(
        rootName,
        sizeof(rootName),
        context.root);

    ret =
        cvxRootActivate(rootName);

    if (ret != 0)
    {
        char message[1024] = {};

        sprintf_s(
            message,
            "[PartOut] cvxRootActivate failed: "
            "ret=%d, root=%s",
            ret,
            context.root);

        DisplayMessage(message);
        return ret;
    }

    return 0;
}

// ============================================================
// Expression handling
// ============================================================

static int ApplyExpressionAssignments(
    const std::string& assignmentText)
{
    const std::vector<std::string> assignments =
        Split(assignmentText, ';');

    int successCount = 0;
    int failedCount = 0;

    for (const std::string& rawAssignment :
         assignments)
    {
        const std::string assignment =
            Trim(rawAssignment);

        if (assignment.empty())
            continue;

        const size_t equalSign =
            assignment.find('=');

        if (equalSign == std::string::npos)
        {
            char message[1024] = {};

            sprintf_s(
                message,
                "[PartOut] Invalid expression assignment: "
                "%s",
                assignment.c_str());

            DisplayMessage(message);
            ++failedCount;
            continue;
        }

        const std::string name =
            Trim(
                assignment.substr(
                    0,
                    equalSign));

        const std::string expression =
            Trim(
                assignment.substr(
                    equalSign + 1));

        if (name.empty() ||
            expression.empty())
        {
            char message[1024] = {};

            sprintf_s(
                message,
                "[PartOut] Empty expression name/value: "
                "%s",
                assignment.c_str());

            DisplayMessage(message);
            ++failedCount;
            continue;
        }

        const int ret =
            SetCurrentPartExpression(
                name.c_str(),
                expression.c_str());

        if (ret == 0)
            ++successCount;
        else
            ++failedCount;
    }

    char summary[512] = {};

    sprintf_s(
        summary,
        "[PartOut] Expression update summary: "
        "success=%d, failed=%d",
        successCount,
        failedCount);

    DisplayMessage(summary);

    // Return 0 even if some expressions failed — individual failures are
    // logged but should not block the whole import workflow.
    return 0;
}

// ============================================================
// Instance file creation
// ============================================================

static std::string CreateUniqueInstancePath(
    const char* sourcePath,
    const std::string& assignmentText)
{
    SYSTEMTIME time = {};
    GetLocalTime(&time);

    const std::string sourceBaseName =
        FileNameWithoutExtension(
            sourcePath != nullptr
                ? sourcePath
                : "Part");

    std::string parameterToken =
        SanitizeFileToken(
            assignmentText);

    // Avoid an excessively long Windows file name.
    if (parameterToken.size() > 100)
        parameterToken.resize(100);

    char dateTimeToken[128] = {};

    sprintf_s(
        dateTimeToken,
        "%04u%02u%02u_%02u%02u%02u_%03u",
        static_cast<unsigned int>(
            time.wYear),
        static_cast<unsigned int>(
            time.wMonth),
        static_cast<unsigned int>(
            time.wDay),
        static_cast<unsigned int>(
            time.wHour),
        static_cast<unsigned int>(
            time.wMinute),
        static_cast<unsigned int>(
            time.wSecond),
        static_cast<unsigned int>(
            time.wMilliseconds));

    std::string fullPath =
        TEST_INSTANCE_DIRECTORY;

    fullPath += "\\";
    fullPath += sourceBaseName;
    fullPath += "_";

    if (!parameterToken.empty())
    {
        fullPath += parameterToken;
        fullPath += "_";
    }

    fullPath += dateTimeToken;
    fullPath += ".Z3PRT";

    return fullPath;
}

// ============================================================
// ZW3D ordinary component insertion
// ============================================================

static int InsertComponentFromFile(
    const char* fullPath,
    const char* internalRootName)
{
    if (fullPath == nullptr ||
        fullPath[0] == '\0' ||
        internalRootName == nullptr ||
        internalRootName[0] == '\0')
    {
        DisplayMessage(
            "[PartOut] InsertComponentFromFile: "
            "invalid path/root.");
        return -1;
    }

    std::string directory;
    std::string fileName;

    if (!SplitFilePath(
            fullPath,
            &directory,
            &fileName))
    {
        DisplayMessage(
            "[PartOut] Cannot split component file path.");
        return -1;
    }

    svxCompData component = {};

    int ret =
        cvxCompInsInit(
            &component);

    if (ret != 0)
    {
        char message[256] = {};

        sprintf_s(
            message,
            "[PartOut] cvxCompInsInit failed: ret=%d",
            ret);

        DisplayMessage(message);
        return ret;
    }

    strcpy_s(
        component.Dir,
        sizeof(component.Dir),
        directory.c_str());

    strcpy_s(
        component.File,
        sizeof(component.File),
        fileName.c_str());

    strcpy_s(
        component.Part,
        sizeof(component.Part),
        internalRootName);

    component.Frame.identity =
        1;

    // Keep the current assembly active after insertion.
    component.SettingsData.AutoActivated =
        0;

    // The copied/downloaded Z3PRT is already a standalone file.
    // Insert it as an external reference rather than copying again.
    component.InstanceData.CopyPart =
        0;

    int componentId =
        0;

    ret =
        cvxCompIns(
            &component,
            &componentId);

    char message[1536] = {};

    sprintf_s(
        message,
        "[PartOut] cvxCompIns: ret=%d, id=%d, "
        "file=%s, root=%s",
        ret,
        componentId,
        fullPath,
        internalRootName);

    DisplayMessage(message);

    return ret;
}

// ============================================================
// Read the internal root name of an ordinary Z3PRT
// ============================================================

static int OpenPartAndGetActiveRoot(
    const char* partPath,
    char* rootOut,
    int rootOutCapacity)
{
    if (partPath == nullptr ||
        partPath[0] == '\0' ||
        rootOut == nullptr ||
        rootOutCapacity <= 0)
    {
        return -1;
    }

    rootOut[0] =
        '\0';

    const int ret =
        cvxFileOpen(partPath);

    if (ret != 0)
    {
        char message[1024] = {};

        sprintf_s(
            message,
            "[PartOut] cvxFileOpen failed: "
            "ret=%d, file=%s",
            ret,
            partPath);

        DisplayMessage(message);
        return ret;
    }

    cvxRootInqActive(
        rootOut,
        rootOutCapacity);

    if (rootOut[0] == '\0')
    {
        DisplayMessage(
            "[PartOut] The opened Z3PRT has no active root.");
        return -1;
    }

    char message[1024] = {};

    sprintf_s(
        message,
        "[PartOut] Opened part: file=%s, root=%s",
        partPath,
        rootOut);

    DisplayMessage(message);

    return 0;
}

// ============================================================
// Workflow 1: direct import
// ============================================================

// ============================================================
// Download a URL to localPath. Returns true on success.
// ============================================================
static bool DownloadFile(const char* url, char* localPath, int localPathSize)
{
    if (!url || !localPath || localPathSize <= 0) return false;

    // Extract filename from URL
    const char* lastSlash = strrchr(url, '/');
    const char* fnStart = lastSlash ? lastSlash + 1 : url;
    const char* qm = strchr(fnStart, '?');
    char rawName[256] = {};
    if (qm) strncpy(rawName, fnStart, (size_t)(qm - fnStart) < sizeof(rawName)-1 ? (size_t)(qm - fnStart) : sizeof(rawName)-1);
    else strcpy(rawName, fnStart);

    // URL-decode
    char decoded[256] = {};
    char* dp = decoded;
    for (const char* sp = rawName; *sp && (size_t)(dp - decoded) < sizeof(decoded) - 1; ++sp)
    {
        if (*sp == '%' && sp[1] && sp[2])
        { char hex[3] = { sp[1], sp[2], 0 }; *dp++ = (char)strtol(hex, nullptr, 16); sp += 2; }
        else *dp++ = *sp;
    }
    *dp = 0;
    if (!decoded[0]) strcpy(decoded, rawName);

    // Convert UTF-8 filename to system codepage for correct filesystem naming
    wchar_t decWide[256] = {};
    MultiByteToWideChar(CP_UTF8, 0, decoded, -1, decWide, 256);
    char decLocal[256] = {};
    WideCharToMultiByte(CP_ACP, 0, decWide, -1, decLocal, 256, nullptr, nullptr);

    sprintf_s(localPath, localPathSize, "%s\\%s", PROD_DOWNLOAD_DIRECTORY,
              decLocal[0] ? decLocal : decoded);
    EnsureDirectory(PROD_DOWNLOAD_DIRECTORY);

    // WinHTTP download
    int wideUrlLen = MultiByteToWideChar(CP_UTF8, 0, url, -1, nullptr, 0);
    if (wideUrlLen <= 0) return false;

    wchar_t* wideUrl = new wchar_t[wideUrlLen];
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wideUrl, wideUrlLen);

    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = {}, urlPath[1024] = {};
    urlComp.lpszHostName = hostName; urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath;   urlComp.dwUrlPathLength = 1024;
    WinHttpCrackUrl(wideUrl, 0, 0, &urlComp);

    bool ok = false;
    DWORD status = 0;
    HINTERNET hSes = WinHttpOpen(L"ZW3D-Plugin/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSes)
    {
        HINTERNET hCon = WinHttpConnect(hSes, hostName, urlComp.nPort, 0);
        HINTERNET hReq = hCon ? WinHttpOpenRequest(hCon, L"GET", urlPath, nullptr,
                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : nullptr;
        if (hReq && WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hReq, nullptr))
        {
            DWORD statusSize = sizeof(status);
            WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
            if (status == 200)
            {
                HANDLE hFile = CreateFileA(localPath, GENERIC_WRITE, 0, nullptr,
                                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hFile != INVALID_HANDLE_VALUE)
                {
                    char buf[8192]; DWORD r = 0, w = 0;
                    while (WinHttpReadData(hReq, buf, sizeof(buf), &r) && r > 0)
                        WriteFile(hFile, buf, r, &w, nullptr);
                    CloseHandle(hFile);
                    ok = true;
                }
            }
        }
        if (hReq) WinHttpCloseHandle(hReq);
        if (hCon) WinHttpCloseHandle(hCon);
        WinHttpCloseHandle(hSes);
    }
    delete[] wideUrl;

    char msg[512];
    if (ok) { sprintf_s(msg, "[PartOut] Downloaded: %s", localPath); DisplayMessage(msg); }
    else    { sprintf_s(msg, "[PartOut] Download failed (HTTP %d): %s", (int)status, url); DisplayMessage(msg); }
    return ok;
}

// ============================================================
// Direct import: insert a local file as component.
// ============================================================
static int DirectImportWorkflow(
    const char* sourcePartPath)
{
    if (!FileExists(sourcePartPath))
    {
        char message[1024] = {};

        sprintf_s(
            message,
            "[PartOut] Source part does not exist: %s",
            sourcePartPath != nullptr
                ? sourcePartPath
                : "(null)");

        DisplayMessage(message);
        return -1;
    }

    const ZwDocumentContext assemblyContext =
        CaptureActiveContext();

    if (!assemblyContext.valid)
        return -1;

    char partRoot[256] = {};

    int ret =
        OpenPartAndGetActiveRoot(
            sourcePartPath,
            partRoot,
            static_cast<int>(
                sizeof(partRoot)));

    if (ret != 0)
    {
        RestoreContext(
            assemblyContext);
        return ret;
    }

    // Direct-import inspection makes no changes.
    cvxFileClose();

    ret =
        RestoreContext(
            assemblyContext);

    if (ret != 0)
        return ret;

    ret =
        InsertComponentFromFile(
            sourcePartPath,
            partRoot);

    if (ret == 0)
    {
        DisplayMessage(
            "[PartOut] Direct import completed.");
    }

    return ret;
}

// ============================================================
// Workflow 2: parameterized import
// ============================================================

static int ParametricImportWorkflow(
    const char* sourcePartPath,
    const char* assignmentText,
    int asShape = 0)
{
    // If the "path" is actually a URL, download it first.
    char localPathBuf[600] = {};
    if (sourcePartPath && (strncmp(sourcePartPath, "http://", 7) == 0 ||
                           strncmp(sourcePartPath, "https://", 8) == 0))
    {
        // Extract filename from URL
        const char* lastSlash = strrchr(sourcePartPath, '/');
        const char* fnStart = lastSlash ? lastSlash + 1 : sourcePartPath;
        const char* qm = strchr(fnStart, '?');
        char rawName[256] = {};
        if (qm)
            strncpy(rawName, fnStart, (size_t)(qm - fnStart) < sizeof(rawName)-1 ? (size_t)(qm - fnStart) : sizeof(rawName)-1);
        else
            strcpy(rawName, fnStart);

        // URL-decode filename
        char decoded[256] = {};
        char* dp = decoded;
        for (const char* sp = rawName; *sp && (size_t)(dp - decoded) < sizeof(decoded) - 1; ++sp)
        {
            if (*sp == '%' && sp[1] && sp[2])
            {
                char hex[3] = { sp[1], sp[2], 0 };
                *dp++ = (char)strtol(hex, nullptr, 16);
                sp += 2;
            }
            else *dp++ = *sp;
        }
        *dp = 0;
        if (!decoded[0]) strcpy(decoded, rawName);

        // Convert UTF-8 to system codepage for correct filesystem name
        wchar_t decWide[256] = {};
        MultiByteToWideChar(CP_UTF8, 0, decoded, -1, decWide, 256);
        char decLocal[256] = {};
        WideCharToMultiByte(CP_ACP, 0, decWide, -1, decLocal, 256, nullptr, nullptr);

        sprintf_s(localPathBuf, "%s\\%s", TEST_DOWNLOAD_DIRECTORY,
                  decLocal[0] ? decLocal : decoded);
        EnsureDirectory(TEST_DOWNLOAD_DIRECTORY);

        // Use WinHTTP for reliable download
        bool downloaded = false;
        int wideUrlLen = MultiByteToWideChar(CP_UTF8, 0, sourcePartPath, -1, nullptr, 0);
        if (wideUrlLen > 0)
        {
            wchar_t* wideUrl = new wchar_t[wideUrlLen];
            MultiByteToWideChar(CP_UTF8, 0, sourcePartPath, -1, wideUrl, wideUrlLen);

            URL_COMPONENTS urlComp = {};
            urlComp.dwStructSize = sizeof(urlComp);
            wchar_t hostName[256] = {}, urlPath[1024] = {};
            urlComp.lpszHostName = hostName;
            urlComp.dwHostNameLength = 256;
            urlComp.lpszUrlPath = urlPath;
            urlComp.dwUrlPathLength = 1024;
            WinHttpCrackUrl(wideUrl, 0, 0, &urlComp);

            HINTERNET hSession = WinHttpOpen(L"ZW3D-Plugin/1.0",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (hSession)
            {
                HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
                if (hConnect)
                {
                    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath,
                        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
                    if (hRequest)
                    {
                        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                            WinHttpReceiveResponse(hRequest, nullptr))
                        {
                            HANDLE hFile = CreateFileA(localPathBuf, GENERIC_WRITE, 0, nullptr,
                                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                            if (hFile != INVALID_HANDLE_VALUE)
                            {
                                char buf[8192];
                                DWORD bytesRead = 0, bytesWritten = 0;
                                while (WinHttpReadData(hRequest, buf, sizeof(buf), &bytesRead) && bytesRead > 0)
                                {
                                    WriteFile(hFile, buf, bytesRead, &bytesWritten, nullptr);
                                }
                                CloseHandle(hFile);
                                downloaded = true;
                            }
                        }
                        WinHttpCloseHandle(hRequest);
                    }
                    WinHttpCloseHandle(hConnect);
                }
                WinHttpCloseHandle(hSession);
            }
            delete[] wideUrl;
        }

        if (downloaded)
        {
            // Check file size to verify it's a real Z3PRT
            WIN32_FILE_ATTRIBUTE_DATA fileInfo = {};
            GetFileAttributesExA(localPathBuf, GetFileExInfoStandard, &fileInfo);
            DWORD fileSize = fileInfo.nFileSizeLow;
            char msg[512];
            sprintf_s(msg, "[PartOut] Downloaded: %s (%u bytes)", localPathBuf, fileSize);
            DisplayMessage(msg);

            if (fileSize < 1024)
            {
                DisplayMessage("[PartOut] Downloaded file too small (<1KB), might be error page");
                return -1;
            }
            sourcePartPath = localPathBuf;
        }
        else
        {
            char msg[512];
            sprintf_s(msg, "[PartOut] Download failed: %s", sourcePartPath);
            DisplayMessage(msg);
            return -1;
        }
    }

    if (!FileExists(sourcePartPath))
    {
        char message[1024] = {};

        sprintf_s(
            message,
            "[PartOut] Source part does not exist: %s",
            sourcePartPath != nullptr
                ? sourcePartPath
                : "(null)");

        DisplayMessage(message);
        return -1;
    }

    if (assignmentText == nullptr ||
        assignmentText[0] == '\0')
    {
        DisplayMessage(
            "[PartOut] Parameterized import has no "
            "expression assignments.");
        return -1;
    }

    // 1. Remember the assembly we're in
    char assemblyFile[512] = {};
    cvxFileInqActive(assemblyFile, sizeof(assemblyFile));

    // 2. Ensure instances directory
    if (!EnsureDirectory(TEST_INSTANCE_DIRECTORY))
    {
        char msg[512];
        sprintf_s(msg, "[PartOut] Cannot create instance dir: %s", TEST_INSTANCE_DIRECTORY);
        DisplayMessage(msg);
        return -1;
    }

    // 3. Create unique instance path and copy source there
    const std::string instancePath =
        CreateUniqueInstancePath(sourcePartPath, assignmentText);

    if (!CopyFileA(sourcePartPath, instancePath.c_str(), TRUE))
    {
        char msg[512];
        sprintf_s(msg, "[PartOut] CopyFile failed: %s -> %s",
                  sourcePartPath, instancePath.c_str());
        DisplayMessage(msg);
        return -1;
    }

    // 4. Open the instance to edit expressions
    int ret = cvxFileOpen(instancePath.c_str());
    if (ret != 0)
    {
        char msg[512];
        sprintf_s(msg, "[PartOut] cvxFileOpen failed: ret=%d file=%s", ret, instancePath.c_str());
        DisplayMessage(msg);
        return -1;
    }

    // 5. Apply expression changes
    ret = ApplyExpressionAssignments(assignmentText);
    if (ret != 0)
    {
        DisplayMessage("[PartOut] Expression update failed.");
        return ret;
    }

    // 6. Save the modified instance
    ret = cvxFileSave(1);
    if (ret != 0)
    {
        char msg[512];
        sprintf_s(msg, "[PartOut] cvxFileSave failed: ret=%d", ret);
        DisplayMessage(msg);
        return ret;
    }

    // 7. Switch back to the assembly
    cvxFileActivate(assemblyFile);

    // 8. Insert the instance into the assembly
    std::string dir, file;
    SplitFilePath(instancePath, &dir, &file);
    std::string partName = FileNameWithoutExtension(file);

    if (asShape)
    {
        ret = InsertPartWithParams(dir.c_str(), file.c_str(), partName.c_str(),
                                   "", 1, 0);
    }
    else
    {
        ret = InsertComponentFromFile(instancePath.c_str(), partName.c_str());
    }

    if (ret == 0)
    {
        char msg[512];
        sprintf_s(msg, "[PartOut] Imported: %s, mode=%s",
                  instancePath.c_str(), asShape ? "shape" : "component");
        DisplayMessage(msg);

        // Clean up the downloaded source file
        DeleteFileA(sourcePartPath);
    }

    return ret;
}

// ============================================================
// Toolbar button handlers
// ============================================================

void OnImportPart()
{
    DisplayMessage(
        "[PartOut] Test button: direct import.");

    DirectImportWorkflow(
        TEST_SOURCE_PART);
}

// ---------------------------------------------------------------------------
// Quick-test buttons for the two parameterized bearing configurations.
// ---------------------------------------------------------------------------

void OnBearingSmall()
{
    InsertPartWithParams(
        "D:\\BaiduNetdiskDownload\\ZWSOFT\\ZW3D WuKong 2027"
        "\\Reuse Library\\Standard Parts\\ANSI\\Bearings"
        "\\Radial Ball Bearing",
        "Radial Ball Bearing.Z3PRT",
        "Radial Ball Bearing",
        "I.D. (In.),3/16;O.D. (In.),11/16;Width (In.),.25",
        0, 1);
}

void OnBearingLarge()
{
    InsertPartWithParams(
        "D:\\BaiduNetdiskDownload\\ZWSOFT\\ZW3D WuKong 2027"
        "\\Reuse Library\\Standard Parts\\ANSI\\Bearings"
        "\\Radial Ball Bearing",
        "Radial Ball Bearing.Z3PRT",
        "Radial Ball Bearing",
        "I.D. (In.),3/8;O.D. (In.),7/8;Width (In.),.25",
        0, 1);
}

void OnBearingShape()
{
    // Insert bearing as SHAPE (asShape=1) into part document
    InsertPartWithParams(
        "D:\\BaiduNetdiskDownload\\ZWSOFT\\ZW3D WuKong 2027"
        "\\Reuse Library\\Standard Parts\\ANSI\\Bearings"
        "\\Radial Ball Bearing",
        "Radial Ball Bearing.Z3PRT",
        "Radial Ball Bearing",
        "I.D. (In.),3/8;O.D. (In.),7/8;Width (In.),.25",
        1,   // shape (not component)
        0);  // current root (shape ignores fFileType)
}

void OnBearingShape2()
{
    InsertPartWithParams(
        TEST_DOWNLOAD_DIRECTORY,
        "testBearing.Z3PRT",
        "testBearing",
        "I.D. (In.),3/8;O.D. (In.),7/8;Width (In.),.25",
        1,   // shape
        0);  // current root
}

void OnBearingShape3()
{
    InsertPartWithParams(
        "C:\\Users\\zxcvb\\Documents\\ZW3D\\testparts\\Radial Ball Bearing",
        "Radial Ball Bearing.Z3PRT",
        "Radial Ball Bearing",
        "I.D. (In.),3/8;O.D. (In.),7/8;Width (In.),.25",
        1,   // shape
        0);  // current root
}

// ============================================================
// Shared check-in logic — saves file, reads expressions, checks
// for Excel companion, and sends JSON to the front-end.
// Called from both toolbar button (test) and bridge method.
static const char* const ASM_DIR =
    "C:\\Users\\zxcvb\\Documents\\ZW3D\\zbttestpart\\test";

void OnAsmNewFile()
{
    DisplayMessage("[AsmOut] Assembly as new file...");
    InsertPartWithParams(
        ASM_DIR, "testzbt.Z3ASM", "testzbt",
        "",   // default params
        0,    // as component
        1);   // as new file
}

void OnAsmShape()
{
    DisplayMessage("[AsmOut] Assembly as shape...");
    InsertPartWithParams(
        ASM_DIR, "testzbt.Z3ASM", "testzbt",
        "",   // default params
        1,    // as shape
        0);   // current root
}

// ============================================================
// Asm Parametric — list components, log expressions, insert as new file
// ============================================================
void OnAsmParametric()
{
    DisplayMessage("[AsmParam] === START ===");

    char rackPath[512], curFile[512] = {};
    sprintf_s(rackPath, "%s\\齿条 GB_T1356-1-20-100x15x15.Z3PRT", ASM_DIR);
    char m[512];
    sprintf_s(m, "[AsmParam] 1.rack=%s", rackPath); DisplayMessage(m);

    // ① 打开装配体 → ② 切到齿条改 h=5 → ③ 保存关闭齿条
    // → ④ 重开装配体 → ⑤ 保存关闭装配体 → ⑥ 回到用户文档 → ⑦ 插入
    char srcPath[512]; sprintf_s(srcPath, "%s\\testzbt.Z3ASM", ASM_DIR);
    cvxFileInqActive(curFile, sizeof(curFile));
    cvxFileOpen(srcPath);                      // ① 打开装配体原件
    if (cvxFileOpen(rackPath) == 0)             // ② 切到齿条
    {
        SetCurrentPartExpression("h", "5");     // ② 改表达式
        cvxFileSave(1); cvxFileClose();         // ③ 保存并关闭齿条
    }
    if (curFile[0]) cvxFileActivate(curFile);  // ④ 回到用户文档
    InsertPartWithParams(ASM_DIR, "testzbt.Z3ASM", "testzbt", "", 0, 1); // ⑤ 插入

    DisplayMessage("[AsmParam] === DONE ===");
}

void OnAsmOpen()
{
    char asmPath[512];
    sprintf_s(asmPath, "%s\\testzbt.Z3ASM", ASM_DIR);
    cvxFileOpen(asmPath);
}

void OnImportParametric()
{
    DisplayMessage(
        "[PartOut] Test button: parametric import.");

    DirectImportWorkflow(TEST_SOURCE_PART);
}

void OnParametricShape()
{
    DisplayMessage(
        "[PartOut] Test button: parametric shape.");

    // Simple shape insert — no open/edit/save/close
    std::string dir, file;
    SplitFilePath(TEST_SOURCE_PART, &dir, &file);
    std::string partName = FileNameWithoutExtension(file);

    InsertPartWithParams(
        dir.c_str(),
        file.c_str(),
        partName.c_str(),
        "",
        1,   // shape
        0);  // current root
}

// ============================================================
// Check-in: save file, export STP, notify front-end
// ============================================================
static void DoCheckinAndNotify(const char* filePath = nullptr)
{
    DisplayMessage("[Checkin] === START ===");

    char z3prtPath[600] = {}, rootName[256] = {};

    if (filePath && filePath[0])
    {
        strcpy_s(z3prtPath, filePath);
        char m[512]; sprintf_s(m, "[Checkin] 1.path=%s", z3prtPath); DisplayMessage(m);
    }
    else
    {
        cvxFileInqActive(z3prtPath, sizeof(z3prtPath));
        cvxRootInqActive(rootName, sizeof(rootName));
        cvxFileSave(1);
        char m[512]; sprintf_s(m, "[Checkin] 1.path(active)=%s", z3prtPath); DisplayMessage(m);
    }

    if (z3prtPath[0] == '\0') { DisplayMessage("[Checkin] ABORT: no path"); return; }

    if (!rootName[0])
    {
        const char* ls = strrchr(z3prtPath, '\\');
        strcpy_s(rootName, ls ? ls + 1 : z3prtPath);
        char* d = strrchr(rootName, '.'); if (d) *d = '\0';
    }
    char m1[256]; sprintf_s(m1, "[Checkin] 2.root=%s", rootName); DisplayMessage(m1);

    // STP path
    char stpPath[512] = {};
    strcpy_s(stpPath, z3prtPath);
    char* dot = strrchr(stpPath, '.'); if (dot) *dot = '\0';
    strcat_s(stpPath, ".stp");

    bool stpExists = (GetFileAttributesA(stpPath) != INVALID_FILE_ATTRIBUTES);
    int stpRet = stpExists ? 0 : -1;
    char m2[512]; sprintf_s(m2, "[Checkin] 3.STP exists=%d path=%s", stpExists, stpPath); DisplayMessage(m2);

    if (!stpExists)
    {
        DisplayMessage("[Checkin] 4.cvxFileOpen...");
        int openRet = cvxFileOpen(z3prtPath);
        char m3[128]; sprintf_s(m3, "[Checkin] 4.open ret=%d", openRet); DisplayMessage(m3);

        if (openRet == 0)
        {
            svxSTEPData sd = {};
            cvxFileExportInit(VX_EXPORT_TYPE_STEP, 0, &sd);
            sd.AppProtocol = 2; sd.OutPut = 0;
            DisplayMessage("[Checkin] 5.exporting STP...");
            stpRet = cvxFileExport(VX_EXPORT_TYPE_STEP, stpPath, &sd);
            char m4[128]; sprintf_s(m4, "[Checkin] 5.export ret=%d", stpRet); DisplayMessage(m4);
            DisplayMessage("[Checkin] 6.cvxFileClose...");
            cvxFileClose();
            DisplayMessage("[Checkin] 6.closed");
        }
    }

    // Companions
    char base[512] = {}; strcpy_s(base, z3prtPath);
    dot = strrchr(base, '.'); if (dot) *dot = '\0';
    char xlsxPath[600] = {}; sprintf_s(xlsxPath, "%s.xlsx", base);
    char z3lPath[600]  = {}; sprintf_s(z3lPath,  "%s.z3l",  base);
    char pngPath[600]  = {}; sprintf_s(pngPath,  "%s.png",  base);
    bool hasX = (GetFileAttributesA(xlsxPath) != INVALID_FILE_ATTRIBUTES);
    bool hasZ = (GetFileAttributesA(z3lPath)  != INVALID_FILE_ATTRIBUTES);
    bool hasP = (GetFileAttributesA(pngPath)  != INVALID_FILE_ATTRIBUTES);
    char m5[256]; sprintf_s(m5, "[Checkin] 7.companions xlsx=%d z3l=%d png=%d", hasX, hasZ, hasP); DisplayMessage(m5);

    // Base64 encoder
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    auto base64 = [](const std::vector<unsigned char>& d) -> std::string {
        std::string o; o.reserve((d.size()+2)/3*4+4);
        for (size_t i = 0; i < d.size(); i += 3) {
            unsigned n = (unsigned)d[i] << 16;
            n |= (i+1<d.size()) ? ((unsigned)d[i+1]<<8) : 0;
            n |= (i+2<d.size()) ? (unsigned)d[i+2] : 0;
            o+=b64[(n>>18)&63]; o+=b64[(n>>12)&63];
            o+=(i+1<d.size()) ? b64[(n>>6)&63] : '=';
            o+=(i+2<d.size()) ? b64[n&63] : '=';
        }
        return o;
    };
    auto readF = [](const char* p) { std::vector<unsigned char> o;
        if(!p||!*p)return o; FILE*f=fopen(p,"rb");if(!f)return o;
        fseek(f,0,SEEK_END);long s=ftell(f);fseek(f,0,SEEK_SET);
        if(s>0){o.resize(s);fread(o.data(),1,s,f);} fclose(f); return o; };

    auto z3B = base64(readF(z3prtPath));
    auto stB = base64(readF(stpPath));
    auto pnB = hasP ? base64(readF(pngPath))  : "";
    auto zlB = hasZ ? base64(readF(z3lPath))  : "";
    auto xlB = hasX ? base64(readF(xlsxPath)) : "";

    char m6[512]; sprintf_s(m6, "[Checkin] 8.files z3prt=%d stp=%d png=%d z3l=%d xlsx=%d",
        (int)z3B.size(), (int)stB.size(), (int)pnB.size(), (int)zlB.size(), (int)xlB.size());
    DisplayMessage(m6);

    std::string json = "{\"action\":\"checkinReady\"";
    auto add = [&](const char* k, const std::string& v) { json += ",\"" + std::string(k) + "\":\"" + v + "\""; };
    add("z3prtBase64", z3B);
    add("stpBase64",   stB);
    add("root", rootName);
    json += ",\"hasPng\":" + std::string(hasP ? "true" : "false");
    json += ",\"hasZ3l\":" + std::string(hasZ ? "true" : "false");
    json += ",\"hasExcel\":" + std::string(hasX ? "true" : "false");
    if (hasP) add("pngBase64",  pnB);
    if (hasZ) add("z3lBase64",  zlB);
    if (hasX) add("xlsxBase64", xlB);
    json += "}";

    char m7[256]; sprintf_s(m7, "[Checkin] 9.JSON %d bytes webview=%p", (int)json.size(), (void*)g_webview.Get()); DisplayMessage(m7);

    if (g_webview) {
        int wl = MultiByteToWideChar(CP_ACP, 0, json.c_str(), -1, nullptr, 0);
        if (wl > 0) {
            wchar_t* wj = new wchar_t[wl];
            MultiByteToWideChar(CP_ACP, 0, json.c_str(), -1, wj, wl);
            HRESULT hr = g_webview->PostWebMessageAsJson(wj);
            char m8[128]; sprintf_s(m8, "[Checkin] 10.PostWebMessageAsJson HR=0x%08X", (unsigned)hr); DisplayMessage(m8);
            delete[] wj;
            g_webview->PostWebMessageAsJson(L"{\"action\":\"ping\"}");
            DisplayMessage("[Checkin] 11.ping sent");
        }
    }
    DisplayMessage("[Checkin] === DONE ===");
}

// Toolbar button wrapper
void OnCheckin()
{
    DisplayMessage("[Checkin] Preparing part for upload...");
    DoCheckinAndNotify();
}

// ============================================================
// Front-end WebView message bridge
// ============================================================

static std::string WideToAnsi(
    const wchar_t* wideText)
{
    if (wideText == nullptr ||
        wideText[0] == L'\0')
    {
        return std::string();
    }

    const int required =
        WideCharToMultiByte(
            CP_ACP,
            0,
            wideText,
            -1,
            nullptr,
            0,
            nullptr,
            nullptr);

    if (required <= 0)
        return std::string();

    std::vector<char> buffer(
        static_cast<size_t>(
            required));

    WideCharToMultiByte(
        CP_ACP,
        0,
        wideText,
        -1,
        buffer.data(),
        required,
        nullptr,
        nullptr);

    return std::string(
        buffer.data());
}

static void HandleFrontendMessage(
    const std::string& rawMessage)
{
    const std::string message =
        Trim(rawMessage);

    if (message.empty())
        return;

    const std::vector<std::string> fields =
        Split(message, '|');

    if (fields.empty())
        return;

    const std::string action =
        Trim(fields[0]);

    if (_stricmp(
            action.c_str(),
            "directImport") == 0)
    {
        const std::string path =
            fields.size() >= 2 &&
            !Trim(fields[1]).empty()
                ? Trim(fields[1])
                : TEST_SOURCE_PART;

        DirectImportWorkflow(
            path.c_str());

        return;
    }

    if (_stricmp(
            action.c_str(),
            "parametricImport") == 0 ||
            _stricmp(action.c_str(), "parametricShape") == 0)
    {
        // Frontend "零件导出" / "实例导出" —
        // Download if URL, then open→edit expressions→save→close→insert.
        bool asShape = (_stricmp(action.c_str(), "parametricShape") == 0);

        const std::string path =
            fields.size() >= 2 && !Trim(fields[1]).empty()
                ? Trim(fields[1])
                : TEST_SOURCE_PART;

        const std::string assignments =
            fields.size() >= 3 && !Trim(fields[2]).empty()
                ? Trim(fields[2])
                : "";

        // Download the file first (if URL)
        char localPath[600] = {};
        const char* filePath = path.c_str();
        if (strncmp(filePath, "http://", 7) == 0 ||
            strncmp(filePath, "https://", 8) == 0)
        {
            if (DownloadFile(filePath, localPath, sizeof(localPath)))
                filePath = localPath;
            else
                return;
        }

        // Copy → edit expressions → save → close → insert
        ParametricImportWorkflow(
            filePath,
            assignments.c_str(),
            asShape ? 1 : 0);

        return;
    }

    if (_stricmp(action.c_str(), "pickAndCheckin") == 0)
    {
        // C++ opens native file picker, gets full path, then does checkin
        char pathBuf[MAX_PATH] = {};
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFilter = "ZW3D Files (*.Z3PRT)\0*.Z3PRT\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = pathBuf;
        ofn.nMaxFile = sizeof(pathBuf);
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameA(&ofn))
        {
            DoCheckinAndNotify(pathBuf);
        }
        return;
    }

    if (_stricmp(action.c_str(), "stpImport") == 0)
    {
        // STP checkout: download (if URL) then import as shape
        const std::string path =
            fields.size() >= 2 && !Trim(fields[1]).empty()
                ? Trim(fields[1])
                : "";

        if (path.empty()) { DisplayMessage("[STP] No path."); return; }

        char localPath[600] = {};
        const char* fPath = path.c_str();
        if (strncmp(fPath, "http://", 7) == 0 || strncmp(fPath, "https://", 8) == 0)
        {
            if (DownloadFile(fPath, localPath, sizeof(localPath)))
                fPath = localPath;
            else
                return;
        }

        // Import STP as new shape into current part
        svxImportData imp = {};
        imp.type = VX_IMPORT_TYPE_STEP;
        strcpy_s(imp.filePath, fPath);
        imp.importTo = 0;  // current object
        int ret = cvxFileImport(&imp);
        char m[128]; sprintf_s(m, "[STP] import ret=%d", ret); DisplayMessage(m);
        return;
    }

    if (_stricmp(action.c_str(), "checkin") == 0)
    {
        // Frontend can pass a file path: checkin|D:\path\file.Z3PRT
        const std::string checkinPath =
            fields.size() >= 2 && !Trim(fields[1]).empty()
                ? Trim(fields[1])
                : "";
        DoCheckinAndNotify(checkinPath.empty() ? nullptr : checkinPath.c_str());
        return;
    }

    char log[1536] = {};

    sprintf_s(
        log,
        "[WebView2] Unknown web message: %s",
        message.c_str());

    DisplayMessage(log);
}

// ============================================================
// Toolbar creation
// ============================================================

void CreateToolbar(
    HWND hwnd,
    HINSTANCE hInst)
{
    CreateWindowExW(
        0,
        L"BUTTON",
        L"Import Part",
        WS_CHILD |
        WS_VISIBLE |
        BS_PUSHBUTTON,
        8,
        6,
        120,
        TOOLBAR_HEIGHT - 10,
        hwnd,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(
                BTN_IMPORT_PART)),
        hInst,
        nullptr);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"Parametric Import",
        WS_CHILD |
        WS_VISIBLE |
        BS_PUSHBUTTON,
        136,
        6,
        170,
        TOOLBAR_HEIGHT - 10,
        hwnd,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(
                BTN_IMPORT_PARAMETRIC)),
        hInst,
        nullptr);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"Bearing Small",
        WS_CHILD |
        WS_VISIBLE |
        BS_PUSHBUTTON,
        314,
        6,
        130,
        TOOLBAR_HEIGHT - 10,
        hwnd,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(
                BTN_BEARING_SMALL)),
        hInst,
        nullptr);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"Bearing Large",
        WS_CHILD |
        WS_VISIBLE |
        BS_PUSHBUTTON,
        452,
        6,
        130,
        TOOLBAR_HEIGHT - 10,
        hwnd,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(
                BTN_BEARING_LARGE)),
        hInst,
        nullptr);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"Bearing Shape",
        WS_CHILD |
        WS_VISIBLE |
        BS_PUSHBUTTON,
        590,
        6,
        130,
        TOOLBAR_HEIGHT - 10,
        hwnd,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(
                BTN_BEARING_SHAPE)),
        hInst,
        nullptr);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"Bearing Shape2",
        WS_CHILD |
        WS_VISIBLE |
        BS_PUSHBUTTON,
        728,
        6,
        140,
        TOOLBAR_HEIGHT - 10,
        hwnd,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(
                BTN_BEARING_SHAPE2)),
        hInst,
        nullptr);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"Parametric Shape",
        WS_CHILD |
        WS_VISIBLE |
        BS_PUSHBUTTON,
        876,
        6,
        150,
        TOOLBAR_HEIGHT - 10,
        hwnd,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(
                BTN_PARAMETRIC_SHAPE)),
        hInst,
        nullptr);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"Bearing Shape3",
        WS_CHILD |
        WS_VISIBLE |
        BS_PUSHBUTTON,
        1034,
        6,
        140,
        TOOLBAR_HEIGHT - 10,
        hwnd,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(
                BTN_BEARING_SHAPE3)),
        hInst,
        nullptr);

    CreateWindowExW(0, L"BUTTON", L"Check In",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        1182, 6, 100, TOOLBAR_HEIGHT - 10,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BTN_CHECKIN)),
        hInst, nullptr);

    CreateWindowExW(0, L"BUTTON", L"Asm NewFile",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        1290, 6, 130, TOOLBAR_HEIGHT - 10,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BTN_ASM_NEWFILE)),
        hInst, nullptr);

    CreateWindowExW(0, L"BUTTON", L"Asm Shape",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        1428, 6, 110, TOOLBAR_HEIGHT - 10,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BTN_ASM_SHAPE)),
        hInst, nullptr);

    CreateWindowExW(0, L"BUTTON", L"Asm Parametric",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        1546, 6, 140, TOOLBAR_HEIGHT - 10,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BTN_ASM_PARAMETRIC)),
        hInst, nullptr);

    CreateWindowExW(0, L"BUTTON", L"Asm Open",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        1694, 6, 100, TOOLBAR_HEIGHT - 10,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BTN_ASM_OPEN)),
        hInst, nullptr);
}

// ============================================================
// Window procedure
// ============================================================

LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        HINSTANCE hInst =
            reinterpret_cast<HINSTANCE>(
                GetWindowLongPtr(
                    hwnd,
                    GWLP_HINSTANCE));

        CreateToolbar(
            hwnd,
            hInst);

        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case BTN_IMPORT_PART:
            OnImportPart();
            return 0;

        case BTN_IMPORT_PARAMETRIC:
            OnImportParametric();
            return 0;

        case BTN_BEARING_SMALL:
            OnBearingSmall();
            return 0;

        case BTN_BEARING_LARGE:
            OnBearingLarge();
            return 0;

        case BTN_BEARING_SHAPE:
            OnBearingShape();
            return 0;

        case BTN_BEARING_SHAPE2:
            OnBearingShape2();
            return 0;

        case BTN_PARAMETRIC_SHAPE:
            OnParametricShape();
            return 0;

        case BTN_BEARING_SHAPE3:
            OnBearingShape3();
            return 0;

        case BTN_CHECKIN:
            OnCheckin();
            return 0;

        case BTN_ASM_NEWFILE:
            OnAsmNewFile();
            return 0;

        case BTN_ASM_SHAPE:
            OnAsmShape();
            return 0;

        case BTN_ASM_PARAMETRIC:
            OnAsmParametric();
            return 0;

        case BTN_ASM_OPEN:
            OnAsmOpen();
            return 0;

        }
        break;

    case WM_SIZE:
        ResizeWebView();
        return 0;

    case WM_CLOSE:
        ShowWindow(
            hwnd,
            SW_HIDE);
        return 0;

    case WM_DESTROY:
        ReleaseWebView();

        g_hwnd =
            nullptr;

        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(
        hwnd,
        uMsg,
        wParam,
        lParam);
}

// ============================================================
// WebView resize/release
// ============================================================

void ResizeWebView()
{
    if (g_controller &&
        g_hwnd)
    {
        RECT rectangle = {};

        GetClientRect(
            g_hwnd,
            &rectangle);

        rectangle.top +=
            TOOLBAR_HEIGHT;

        if (rectangle.bottom <=
            rectangle.top)
        {
            rectangle.bottom =
                rectangle.top + 100;
        }

        g_controller->put_Bounds(
            rectangle);
    }
}

void ReleaseWebView()
{
    if (g_controller)
    {
        g_controller->Close();

        g_controller =
            nullptr;

        g_webview =
            nullptr;
    }

    g_initialized =
        false;
}

// ============================================================
// WebView2 user-data directory
// ============================================================

static std::wstring GetUserDataFolder()
{
    wchar_t localAppData[MAX_PATH] = {};

    if (SUCCEEDED(
            SHGetFolderPathW(
                nullptr,
                CSIDL_LOCAL_APPDATA,
                nullptr,
                0,
                localAppData)))
    {
        std::wstring folder =
            std::wstring(localAppData) +
            L"\\MyPlugin_WebView2";

        CreateDirectoryW(
            folder.c_str(),
            nullptr);

        return folder;
    }

    return L"";
}

// ============================================================
// WebView2 initialization
// ============================================================

void InitWebView2(
    HWND hwnd,
    const std::wstring& url)
{
    if (g_initialized)
        return;

    g_initialized =
        true;

    const std::wstring userFolder =
        GetUserDataFolder();

    const HRESULT createEnvironmentResult =
        CreateCoreWebView2EnvironmentWithOptions(
            nullptr,
            userFolder.empty()
                ? nullptr
                : userFolder.c_str(),
            nullptr,
            Callback<
                ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [hwnd, url](
                    HRESULT result,
                    ICoreWebView2Environment* environment)
                    -> HRESULT
                {
                    if (FAILED(result) ||
                        environment == nullptr)
                    {
                        MessageBoxW(
                            hwnd,
                            L"WebView2 initialization failed.\n\n"
                            L"Install Microsoft Edge WebView2 Runtime.",
                            L"WebView2 Error",
                            MB_OK |
                            MB_ICONERROR);

                        g_initialized =
                            false;

                        return result;
                    }

                    environment->
                        CreateCoreWebView2Controller(
                            hwnd,
                            Callback<
                                ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                                [hwnd, url](
                                    HRESULT controllerResult,
                                    ICoreWebView2Controller* controller)
                                    -> HRESULT
                                {
                                    if (FAILED(
                                            controllerResult) ||
                                        controller == nullptr)
                                    {
                                        MessageBoxW(
                                            hwnd,
                                            L"Failed to create WebView2 controller.",
                                            L"WebView2 Error",
                                            MB_OK |
                                            MB_ICONERROR);

                                        g_initialized =
                                            false;

                                        return controllerResult;
                                    }

                                    g_controller =
                                        controller;

                                    g_controller->
                                        get_CoreWebView2(
                                            &g_webview);

                                    ComPtr<
                                        ICoreWebView2Settings>
                                        settings;

                                    if (SUCCEEDED(
                                            g_webview->
                                                get_Settings(
                                                    &settings)) &&
                                        settings)
                                    {
                                        settings->
                                            put_IsScriptEnabled(
                                                TRUE);

                                        settings->
                                            put_IsWebMessageEnabled(
                                                TRUE);

                                        settings->
                                            put_AreDefaultScriptDialogsEnabled(
                                                TRUE);

                                        settings->
                                            put_AreDevToolsEnabled(
                                                TRUE);
                                    }

                                    // Compatibility object plus a small,
                                    // explicit front-end bridge.
                                    g_webview->
                                        AddScriptToExecuteOnDocumentCreated(
                                            L"window.bound={__ENV__:'zw3d'};"
                                            L"window.CefSharp={"
                                            L"BindObjectAsync:function(){"
                                            L"return Promise.resolve(true);"
                                            L"}};"
                                            L"window.zw3dBridge={"
                                            L"directImport:function(path){"
                                            L"window.chrome.webview.postMessage("
                                            L"'directImport|'+(path||''));"
                                            L"},"
                                            L"parametricImport:function(path,params){"
                                            L"window.chrome.webview.postMessage("
                                            L"'parametricImport|'+(path||'')+'|'+"
                                            L"(params||''));"
                                            L"},"
                                            L"parametricShape:function(path,params){"
                                            L"window.chrome.webview.postMessage("
                                            L"'parametricShape|'+(path||'')+'|'+"
                                            L"(params||''));"
                                            L"},"
                                            L"checkin:function(path){"
                                            L"window.chrome.webview.postMessage("
                                            L"'checkin|'+(path||''));"
                                            L"},"
                                            L"stpImport:function(path){"
                                            L"window.chrome.webview.postMessage("
                                            L"'stpImport|'+(path||''));"
                                            L"},"
                                            L"pickAndCheckin:function(){"
                                            L"window.chrome.webview.postMessage("
                                            L"'pickAndCheckin');"
                                            L"},"
                                            L"};",
                                            nullptr);

                                    g_webview->
                                        add_WebMessageReceived(
                                            Callback<
                                                ICoreWebView2WebMessageReceivedEventHandler>(
                                                [](
                                                    ICoreWebView2*,
                                                    ICoreWebView2WebMessageReceivedEventArgs* args)
                                                    -> HRESULT
                                                {
                                                    LPWSTR rawMessage =
                                                        nullptr;

                                                    const HRESULT messageResult =
                                                        args->
                                                            TryGetWebMessageAsString(
                                                                &rawMessage);

                                                    if (SUCCEEDED(
                                                            messageResult) &&
                                                        rawMessage !=
                                                            nullptr)
                                                    {
                                                        OutputDebugStringW(
                                                            L"[ZW3D WebView2] ");

                                                        OutputDebugStringW(
                                                            rawMessage);

                                                        OutputDebugStringW(
                                                            L"\n");

                                                        const std::string message =
                                                            WideToAnsi(
                                                                rawMessage);

                                                        HandleFrontendMessage(
                                                            message);

                                                        CoTaskMemFree(
                                                            rawMessage);
                                                    }

                                                    return S_OK;
                                                }).Get(),
                                            nullptr);

                                    ResizeWebView();

                                    if (!url.empty())
                                    {
                                        g_webview->
                                            Navigate(
                                                url.c_str());
                                    }

                                    return S_OK;
                                }).Get());

                    return S_OK;
                }).Get());

    if (FAILED(
            createEnvironmentResult))
    {
        char message[256] = {};

        sprintf_s(
            message,
            "[WebView2] CreateEnvironment failed: "
            "0x%08X",
            static_cast<unsigned int>(
                createEnvironmentResult));

        DisplayMessage(message);

        g_initialized =
            false;
    }
}

// ============================================================
// Public API
// ============================================================

int ShowWebViewWindow(
    const wchar_t* url)
{
    if (g_hwnd &&
        IsWindow(g_hwnd))
    {
        if (g_webview &&
            url != nullptr &&
            wcslen(url) > 0)
        {
            g_webview->
                Navigate(url);
        }

        ShowWindow(
            g_hwnd,
            SW_SHOW);

        SetForegroundWindow(
            g_hwnd);

        return 0;
    }

    HINSTANCE instance =
        reinterpret_cast<HINSTANCE>(
            GetModuleHandleW(
                L"MyFirstPlugin.dll"));

    if (instance == nullptr)
    {
        instance =
            reinterpret_cast<HINSTANCE>(
                GetModuleHandleW(
                    nullptr));
    }

    WNDCLASSEXW windowClass = {};

    windowClass.cbSize =
        sizeof(WNDCLASSEXW);

    windowClass.lpfnWndProc =
        WindowProc;

    windowClass.hInstance =
        instance;

    windowClass.lpszClassName =
        CLASS_NAME;

    windowClass.hCursor =
        LoadCursorW(
            nullptr,
            IDC_ARROW);

    windowClass.hbrBackground =
        reinterpret_cast<HBRUSH>(
            COLOR_WINDOW + 1);

    RegisterClassExW(
        &windowClass);

    const int screenWidth =
        GetSystemMetrics(
            SM_CXSCREEN);

    const int screenHeight =
        GetSystemMetrics(
            SM_CYSCREEN);

    const int windowWidth =
        static_cast<int>(
            screenWidth * 0.65);

    const int windowHeight =
        static_cast<int>(
            screenHeight * 0.65);

    g_hwnd =
        CreateWindowExW(
            WS_EX_APPWINDOW,
            CLASS_NAME,
            WINDOW_TITLE,
            WS_OVERLAPPEDWINDOW,
            (screenWidth - windowWidth) / 2,
            (screenHeight - windowHeight) / 2,
            windowWidth,
            windowHeight,
            nullptr,
            nullptr,
            instance,
            nullptr);

    if (g_hwnd == nullptr)
    {
        DisplayMessage(
            "[WebView2] CreateWindowEx failed.");
        return 1;
    }

    ShowWindow(
        g_hwnd,
        SW_SHOW);

    UpdateWindow(
        g_hwnd);

    const std::wstring targetUrl =
        url != nullptr &&
        wcslen(url) > 0
            ? url
            : L"https://www.baidu.com";

    InitWebView2(
        g_hwnd,
        targetUrl);

    return 0;
}

void CloseWebViewWindow()
{
    if (g_hwnd &&
        IsWindow(g_hwnd))
    {
        ReleaseWebView();

        DestroyWindow(
            g_hwnd);

        g_hwnd =
            nullptr;
    }
}

int IsWebViewWindowOpen()
{
    return g_hwnd &&
           IsWindow(g_hwnd)
        ? 1
        : 0;
}
