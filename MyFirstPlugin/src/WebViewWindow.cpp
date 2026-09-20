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
//     previewZ3prt|requestId|C:\path\part.Z3PRT|bearerToken
//
//     asmOpen|C:\path\asm.Z3ASM
//     asmImport|C:\path\asm.Z3ASM            (component, new file)
//     asmImportRef|C:\path\asm.Z3ASM         (component, reference)
//     asmShape|C:\path\asm.Z3ASM             (shape)
//     asmParametricImport|pathOrUrl|substr:expr=val;...|token|manifestJson
//     asmParametricShape|pathOrUrl|substr:expr=val;...|token|manifestJson
//         (http(s) URLs are downloaded with the manifest component files
//          into one staging directory before the workflow runs; result is
//          posted back as asmExportResult)
//     asmExportStep|requestId|pathOrUrl|token
//         (exports the assembly as STEP and posts asmStepExportReady with
//          stepFileName/stepBase64 for POST /3d-assemblies/{id}/stp)
//     readPartParams|requestId|pathOrUrl|token
//         (opens the part and posts partParamsReady with its expression
//          list — fallback when the part library has no specs)
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
#include <cstdint>
#include <cstdarg>
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
#include "zwapi_file_data.h"
#include "zwapi_file_path.h"
#include "zwapi_root.h"
#include "zwapi_asm_comp.h"
#include "zwapi_part_var.h"
#include "zwapi_asm_reuselibrary.h"
#include "zwapi_shape.h"
#include "zwapi_face.h"
#include "zwapi_entity.h"
#include "zwapi_part_facets.h"
#include "zwapi_userinput.h"

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

static const int BTN_ASM_PARAMETRIC_SHAPE =
    1014;

static const int BTN_ASM_PARAMETRIC2 =
    1015;

static const int BTN_TAKEOUT_OPEN =
    1016;

static const int BTN_TAKEOUT_INSERT =
    1017;

static const int BTN_TAKEOUT_SHAPE =
    1018;

static const int TOOLBAR_HEIGHT =
    88;

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
void OnAsmParametricShape();
void OnAsmParametric2();
void OnTakeoutOpen();
void OnTakeoutInsert();
void OnTakeoutShape();
static int InsertAssemblyComponent(
    const char* dir, const char* file, const char* part, int copyPart,
    const svxPoint* insertionPoint = nullptr);
static int InsertAssemblyShape(
    const char* dir, const char* file, const char* part,
    const svxPoint* insertionPoint = nullptr);

// ============================================================
// Logging and string helpers
// ============================================================

// Convert wide string to system codepage (GBK on Chinese Windows).
// Use L"中文" literals to avoid source file encoding issues.
static std::string SysStr(const wchar_t* w)
{
    if (!w) return "";
    char buf[1024] = {};
    WideCharToMultiByte(CP_ACP, 0, w, -1, buf, sizeof(buf), nullptr, nullptr);
    return buf;
}

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

static std::string GetInstanceDirectory()
{
    char tempDirectory[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, tempDirectory) == 0)
        return "";

    std::string pluginDirectory =
        std::string(tempDirectory) + "MyFirstPlugin";
    if (!EnsureDirectory(pluginDirectory.c_str()))
        return "";

    std::string instanceDirectory =
        pluginDirectory + "\\instances";
    if (!EnsureDirectory(instanceDirectory.c_str()))
        return "";

    return instanceDirectory;
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

    // Never insert an instance whose requested parameter values were only
    // partially applied.  Reporting success here silently produced a model
    // with default or stale dimensions.
    return successCount > 0 && failedCount == 0 ? 0 : -1;
}

// ============================================================
// Instance file creation
// ============================================================

static std::string CreateUniqueInstancePath(
    const char* sourcePath,
    const std::string& assignmentText,
    const std::string& instanceDirectory)
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
        instanceDirectory;

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

static int PickInsertionPoint(svxPoint* point)
{
    if (point == nullptr)
        return -1;

    const bool restoreWebWindow =
        g_hwnd != nullptr && IsWindow(g_hwnd) && IsWindowVisible(g_hwnd);

    if (restoreWebWindow)
        ShowWindow(g_hwnd, SW_HIDE);

    DisplayMessage(
        SysStr(L"[PartOut] 请在 ZW3D 视图区单击零件放置位置；按 Esc 取消。")
            .c_str());

    const std::string prompt =
        SysStr(L"请选择零件放置位置（Esc 取消）");
    const int ret = cvxGetPoint(
        prompt.c_str(),
        VX_INP_PNT_GENERAL,
        0,
        point,
        nullptr);

    if (restoreWebWindow)
    {
        ShowWindow(g_hwnd, SW_SHOW);
        SetForegroundWindow(g_hwnd);
    }

    if (ret != 0)
    {
        DisplayMessage(
            SysStr(L"[PartOut] 已取消放置，未导入零件。")
                .c_str());
    }

    return ret;
}

static int InsertComponentFromFile(
    const char* fullPath,
    const char* internalRootName,
    const svxPoint* insertionPoint = nullptr)
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

    if (insertionPoint != nullptr)
    {
        component.Frame.identity = 0;
        component.Frame.xx = 1.0;
        component.Frame.yy = 1.0;
        component.Frame.zz = 1.0;
        component.Frame.xt = insertionPoint->x;
        component.Frame.yt = insertionPoint->y;
        component.Frame.zt = insertionPoint->z;
    }
    else
    {
        component.Frame.identity = 1;
    }

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
        cvxFileClose();
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
static bool DownloadFile(
    const char* url,
    char* localPath,
    int localPathSize,
    const char* bearerToken = nullptr,
    DWORD* statusOut = nullptr)
{
    if (statusOut)
        *statusOut = 0;

    if (!url || !localPath || localPathSize <= 0)
        return false;

    localPath[0] = '\0';

    // Extract filename from URL
    const char* lastSlash = strrchr(url, '/');
    const char* fnStart = lastSlash ? lastSlash + 1 : url;
    const char* qm = strchr(fnStart, '?');
    char rawName[256] = {};
    const size_t rawNameLength = qm
        ? static_cast<size_t>(qm - fnStart)
        : strlen(fnStart);
    const size_t copyLength =
        rawNameLength < sizeof(rawName) - 1
            ? rawNameLength
            : sizeof(rawName) - 1;
    memcpy(rawName, fnStart, copyLength);
    rawName[copyLength] = '\0';

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
    if (!decoded[0])
        strcpy_s(decoded, rawName);

    // Convert UTF-8 filename to system codepage for correct filesystem naming
    wchar_t decWide[256] = {};
    MultiByteToWideChar(CP_UTF8, 0, decoded, -1, decWide, 256);
    char decLocal[256] = {};
    WideCharToMultiByte(CP_ACP, 0, decWide, -1, decLocal, 256, nullptr, nullptr);

    const char* preferredName =
        decLocal[0] ? decLocal : decoded;

    char safeName[256] = {};
    size_t safeLength = 0;

    for (const char* cursor = preferredName;
         *cursor && safeLength < sizeof(safeName) - 1;
         ++cursor)
    {
        const unsigned char byte =
            static_cast<unsigned char>(*cursor);

        const bool invalidAscii =
            byte < 32 ||
            *cursor == '<' ||
            *cursor == '>' ||
            *cursor == ':' ||
            *cursor == '"' ||
            *cursor == '/' ||
            *cursor == '\\' ||
            *cursor == '|' ||
            *cursor == '?' ||
            *cursor == '*';

        safeName[safeLength++] =
            invalidAscii ? '_' : *cursor;
    }

    safeName[safeLength] = '\0';

    if (safeName[0] == '\0' ||
        strcmp(safeName, ".") == 0 ||
        strcmp(safeName, "..") == 0)
    {
        strcpy_s(safeName, "model.Z3PRT");
    }

    if (strstr(url, ".z3prt") || strstr(url, ".Z3PRT"))
    {
        if (!strstr(safeName, ".z3prt") &&
            !strstr(safeName, ".Z3PRT"))
        {
            const size_t extensionLength = strlen(".Z3PRT");

            if (strlen(safeName) + extensionLength >=
                sizeof(safeName))
            {
                safeName[
                    sizeof(safeName) -
                    extensionLength - 1] = '\0';
            }

            strcat_s(safeName, ".Z3PRT");
        }
    }

    char tempDirectory[MAX_PATH] = {};

    if (GetTempPathA(MAX_PATH, tempDirectory) == 0)
        return false;

    char pluginTempDirectory[MAX_PATH] = {};
    if (_snprintf_s(
        pluginTempDirectory,
        _countof(pluginTempDirectory),
        _TRUNCATE,
        "%sMyFirstPlugin",
        tempDirectory) < 0)
    {
        return false;
    }

    if (!EnsureDirectory(pluginTempDirectory))
        return false;

    const int pathLength = _snprintf_s(
        localPath,
        localPathSize,
        _TRUNCATE,
        "%s\\%lu_%llu_%s",
        pluginTempDirectory,
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long long>(GetTickCount64()),
        safeName);

    if (pathLength <= 0 || pathLength >= localPathSize)
    {
        localPath[0] = '\0';
        return false;
    }

    // WinHTTP download
    int wideUrlLen = MultiByteToWideChar(CP_UTF8, 0, url, -1, nullptr, 0);
    if (wideUrlLen <= 0) return false;

    wchar_t* wideUrl = new wchar_t[wideUrlLen];
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wideUrl, wideUrlLen);

    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwHostNameLength = static_cast<DWORD>(-1);
    urlComp.dwUrlPathLength = static_cast<DWORD>(-1);
    urlComp.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(wideUrl, 0, 0, &urlComp))
    {
        delete[] wideUrl;
        return false;
    }

    const std::wstring hostName(
        urlComp.lpszHostName,
        urlComp.dwHostNameLength);

    std::wstring requestTarget(
        urlComp.lpszUrlPath,
        urlComp.dwUrlPathLength);

    if (urlComp.lpszExtraInfo && urlComp.dwExtraInfoLength > 0)
    {
        requestTarget.append(
            urlComp.lpszExtraInfo,
            urlComp.dwExtraInfoLength);
    }

    const DWORD requestFlags =
        urlComp.nScheme == INTERNET_SCHEME_HTTPS
            ? WINHTTP_FLAG_SECURE
            : 0;

    bool ok = false;
    DWORD status = 0;
    HINTERNET hSes = WinHttpOpen(L"ZW3D-Plugin/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSes)
    {
        HINTERNET hCon = WinHttpConnect(
            hSes,
            hostName.c_str(),
            urlComp.nPort,
            0);

        HINTERNET hReq = hCon
            ? WinHttpOpenRequest(
                hCon,
                L"GET",
                requestTarget.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                requestFlags)
            : nullptr;

        std::wstring authorizationHeader;

        if (hReq && bearerToken && bearerToken[0] != '\0')
        {
            const int tokenLength = MultiByteToWideChar(
                CP_UTF8,
                0,
                bearerToken,
                -1,
                nullptr,
                0);

            if (tokenLength > 0)
            {
                std::vector<wchar_t> wideToken(
                    static_cast<size_t>(tokenLength));

                MultiByteToWideChar(
                    CP_UTF8,
                    0,
                    bearerToken,
                    -1,
                    wideToken.data(),
                    tokenLength);

                authorizationHeader = L"Authorization: Bearer ";
                authorizationHeader += wideToken.data();

                WinHttpAddRequestHeaders(
                    hReq,
                    authorizationHeader.c_str(),
                    static_cast<DWORD>(-1),
                    WINHTTP_ADDREQ_FLAG_ADD |
                    WINHTTP_ADDREQ_FLAG_REPLACE);
            }
        }

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
                    char buf[8192];
                    DWORD bytesRead = 0;
                    unsigned long long totalBytesWritten = 0;
                    bool readSucceeded = true;
                    bool writeSucceeded = true;

                    for (;;)
                    {
                        if (!WinHttpReadData(
                                hReq,
                                buf,
                                sizeof(buf),
                                &bytesRead))
                        {
                            readSucceeded = false;
                            break;
                        }

                        if (bytesRead == 0)
                            break;

                        DWORD totalWritten = 0;

                        while (totalWritten < bytesRead)
                        {
                            DWORD bytesWritten = 0;
                            if (!WriteFile(
                                    hFile,
                                    buf + totalWritten,
                                    bytesRead - totalWritten,
                                    &bytesWritten,
                                    nullptr) ||
                                bytesWritten == 0)
                            {
                                writeSucceeded = false;
                                break;
                            }

                            totalWritten += bytesWritten;
                            totalBytesWritten += bytesWritten;
                        }

                        if (!writeSucceeded)
                            break;
                    }

                    CloseHandle(hFile);
                    ok = readSucceeded &&
                         writeSucceeded &&
                         totalBytesWritten > 0;
                }
            }
        }
        if (hReq) WinHttpCloseHandle(hReq);
        if (hCon) WinHttpCloseHandle(hCon);
        WinHttpCloseHandle(hSes);
    }
    delete[] wideUrl;

    if (!ok && localPath[0] != '\0')
        DeleteFileA(localPath);

    char msg[512];
    if (ok) { sprintf_s(msg, "[PartOut] Downloaded: %s", localPath); DisplayMessage(msg); }
    else    { sprintf_s(msg, "[PartOut] Download failed (HTTP %d): %s", (int)status, url); DisplayMessage(msg); }
    if (statusOut) *statusOut = status;
    return ok;
}

// ============================================================
// Z3PRT preview extraction for the embedded front end
// ============================================================

static std::vector<unsigned char> ReadBinaryFile(
    const char* path)
{
    std::vector<unsigned char> data;

    FILE* file = nullptr;
    if (fopen_s(&file, path, "rb") != 0 || file == nullptr)
        return data;

    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size > 0)
    {
        data.resize(static_cast<size_t>(size));
        const size_t bytesRead =
            fread(data.data(), 1, data.size(), file);

        if (bytesRead != data.size())
            data.clear();
    }

    fclose(file);
    return data;
}

static std::string EncodeBase64(
    const std::vector<unsigned char>& data)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve(((data.size() + 2) / 3) * 4);

    for (size_t index = 0; index < data.size(); index += 3)
    {
        const unsigned int first = data[index];
        const unsigned int second =
            index + 1 < data.size() ? data[index + 1] : 0;
        const unsigned int third =
            index + 2 < data.size() ? data[index + 2] : 0;
        const unsigned int value =
            (first << 16) | (second << 8) | third;

        encoded.push_back(alphabet[(value >> 18) & 0x3F]);
        encoded.push_back(alphabet[(value >> 12) & 0x3F]);
        encoded.push_back(
            index + 1 < data.size()
                ? alphabet[(value >> 6) & 0x3F]
                : '=');
        encoded.push_back(
            index + 2 < data.size()
                ? alphabet[value & 0x3F]
                : '=');
    }

    return encoded;
}

static std::string EscapeJsonString(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());

    static const char hex[] = "0123456789ABCDEF";

    for (const unsigned char byte : value)
    {
        switch (byte)
        {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (byte < 0x20)
            {
                escaped += "\\u00";
                escaped.push_back(hex[(byte >> 4) & 0x0F]);
                escaped.push_back(hex[byte & 0x0F]);
            }
            else
            {
                escaped.push_back(static_cast<char>(byte));
            }
            break;
        }
    }

    return escaped;
}

static void PostWebJson(const std::string& json)
{
    if (!g_webview || json.empty())
        return;

    const int wideLength = MultiByteToWideChar(
        CP_ACP, 0, json.c_str(), -1, nullptr, 0);
    if (wideLength <= 0)
        return;

    std::vector<wchar_t> wideJson(static_cast<size_t>(wideLength));
    MultiByteToWideChar(
        CP_ACP, 0, json.c_str(), -1, wideJson.data(), wideLength);
    g_webview->PostWebMessageAsJson(wideJson.data());
}

static void PostStockInFailure(
    const char* action,
    const char* error,
    bool cancelled)
{
    std::string json = "{\"action\":\"";
    json += EscapeJsonString(action ? action : "stockInReady");
    json += "\",\"success\":false,\"cancelled\":";
    json += cancelled ? "true" : "false";
    json += ",\"error\":\"";
    json += EscapeJsonString(error ? error : "Unable to select the model");
    json += "\"}";
    PostWebJson(json);
}

static void PostZ3prtPreviewResult(
    const std::string& requestId,
    const std::string& bitmapBase64,
    const char* error,
    const char* action = "z3prtPreviewReady",
    const char* mimeType = "image/bmp")
{
    if (!g_webview)
        return;

    const std::string safeRequestId =
        SanitizeFileToken(requestId.empty() ? "preview" : requestId);

    std::string json =
        "{\"action\":\"" +
        std::string(action) +
        "\",\"requestId\":\"" +
        safeRequestId + "\",\"success\":";

    if (error == nullptr && !bitmapBase64.empty())
    {
        json += "true,\"mimeType\":\"";
        json += mimeType;
        json += "\",\"base64\":\"";
        json += bitmapBase64;
        json += "\"}";
    }
    else
    {
        json += "false,\"error\":\"";
        json += error != nullptr ? error : "Preview extraction failed";
        json += "\"}";
    }

    const int wideLength =
        MultiByteToWideChar(
            CP_UTF8,
            0,
            json.c_str(),
            -1,
            nullptr,
            0);

    if (wideLength <= 0)
        return;

    std::vector<wchar_t> wideJson(
        static_cast<size_t>(wideLength));

    MultiByteToWideChar(
        CP_UTF8,
        0,
        json.c_str(),
        -1,
        wideJson.data(),
        wideLength);

    g_webview->PostWebMessageAsJson(
        wideJson.data());
}

static void PostPartExportResult(
    bool asShape,
    int result)
{
    if (!g_webview)
        return;

    const char* mode = asShape ? "whole" : "instance";
    char json[256] = {};
    sprintf_s(
        json,
        "{\"action\":\"partExportResult\",\"mode\":\"%s\","
        "\"success\":%s,\"code\":%d}",
        mode,
        result == 0 ? "true" : "false",
        result);

    const int wideLength = MultiByteToWideChar(
        CP_UTF8, 0, json, -1, nullptr, 0);
    if (wideLength <= 0)
        return;

    std::vector<wchar_t> wideJson(
        static_cast<size_t>(wideLength));
    MultiByteToWideChar(
        CP_UTF8, 0, json, -1, wideJson.data(), wideLength);
    g_webview->PostWebMessageAsJson(wideJson.data());
}

static bool SameLocalFilePath(
    const char* first,
    const char* second)
{
    if (!first || !second || !first[0] || !second[0])
        return false;

    char firstFull[1024] = {};
    char secondFull[1024] = {};

    if (!_fullpath(firstFull, first, sizeof(firstFull)) ||
        !_fullpath(secondFull, second, sizeof(secondFull)))
    {
        return _stricmp(first, second) == 0;
    }

    return _stricmp(firstFull, secondFull) == 0;
}

// cvxFileInqActive commonly returns only the file name.  Resolve it through
// the active document directory before comparing it with a plugin-owned path.
// This prevents a same-name document elsewhere in the session from being
// edited, saved, or closed accidentally.
static bool ActiveDocumentMatchesPath(const char* expectedPath)
{
    if (!expectedPath || !expectedPath[0])
        return false;

    char activeFile[600] = {};
    cvxFileInqActive(activeFile, static_cast<int>(sizeof(activeFile)));
    if (!activeFile[0])
        return false;

    if (SameLocalFilePath(activeFile, expectedPath))
        return true;

    char activeDirectory[600] = {};
    cvxFileDirectoryByLongPath(
        activeDirectory,
        static_cast<int>(sizeof(activeDirectory)));
    if (!activeDirectory[0])
        return false;

    std::string activePath = activeDirectory;
    if (activePath.back() != '\\' && activePath.back() != '/')
        activePath += "\\";
    activePath += activeFile;

    return SameLocalFilePath(activePath.c_str(), expectedPath);
}

// Close only a document that has been proven to be the plugin-owned path.
// Passing NULL closes the active document reliably across ZW3D versions;
// the path check immediately before it prevents touching a user's document.
static bool ClosePluginDocument(const char* expectedPath)
{
    if (!expectedPath || !expectedPath[0])
        return true;

    if (ActiveDocumentMatchesPath(expectedPath))
        cvxFileClose2(nullptr, 3);
    else
        cvxFileClose2(expectedPath, 3);

    return !ActiveDocumentMatchesPath(expectedPath);
}

// ============================================================
// Assembly STEP export — powers POST /3d-assemblies/{id}/stp
// (backend converts the STEP into a GLB preview).
// ============================================================

// Extracts the decoded file name from a URL path (query stripped),
// converted from UTF-8 to the system codepage and sanitized.
static std::string UrlFileName(const std::string& url)
{
    const size_t lastSlash = url.find_last_of('/');
    std::string name =
        lastSlash == std::string::npos ? url : url.substr(lastSlash + 1);

    const size_t query = name.find_first_of("?#");
    if (query != std::string::npos)
        name = name.substr(0, query);

    std::string decoded;
    decoded.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i)
    {
        if (name[i] == '%' && i + 2 < name.size())
        {
            char hex[3] = { name[i + 1], name[i + 2], 0 };
            decoded.push_back(static_cast<char>(strtol(hex, nullptr, 16)));
            i += 2;
        }
        else
        {
            decoded.push_back(name[i]);
        }
    }

    wchar_t wide[512] = {};
    MultiByteToWideChar(CP_UTF8, 0, decoded.c_str(), -1, wide, 512);
    char local[512] = {};
    WideCharToMultiByte(CP_ACP, 0, wide, -1, local, 512, nullptr, nullptr);

    std::string result = local[0] ? std::string(local) : decoded;
    for (char& character : result)
    {
        if (character == '<' || character == '>' || character == ':' ||
            character == '"' || character == '/' || character == '\\' ||
            character == '|' || character == '?' || character == '*')
        {
            character = '_';
        }
    }

    if (result.empty() || result == "." || result == "..")
        result = "model.Z3ASM";

    return result;
}

// Exports the given .Z3ASM (or the active document when it matches) to a
// unique STEP file under %TEMP%\MyFirstPlugin\steps. Returns false when the
// export fails; on success *stepPathOut receives the created file path and
// the caller owns deletion.
static bool ExportAssemblyStepFile(
    const char* assemblyPath,
    std::string* stepPathOut)
{
    if (assemblyPath == nullptr ||
        assemblyPath[0] == '\0' ||
        !FileExists(assemblyPath))
    {
        DisplayMessage("[AsmStep] assembly file not found.");
        return false;
    }

    char tempDirectory[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, tempDirectory) == 0)
        return false;

    char stepDirectory[MAX_PATH] = {};
    sprintf_s(stepDirectory, "%sMyFirstPlugin\\steps", tempDirectory);
    if (!EnsureDirectory(stepDirectory))
        return false;

    char stepPath[MAX_PATH * 2] = {};
    sprintf_s(
        stepPath,
        "%s\\%lu_%llu_%s.stp",
        stepDirectory,
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long long>(GetTickCount64()),
        SanitizeFileToken(FileNameWithoutExtension(assemblyPath)).c_str());

    const ZwDocumentContext originalContext = CaptureActiveContext();
    const bool exportFromActiveFile =
        originalContext.valid &&
        SameLocalFilePath(originalContext.file, assemblyPath);
    bool openedForExport = false;

    if (!exportFromActiveFile)
        openedForExport = cvxFileOpen(assemblyPath) == 0;

    int stepResult = -1;

    if (exportFromActiveFile || openedForExport)
    {
        svxSTEPData stepData = {};
        if (cvxFileExportInit(VX_EXPORT_TYPE_STEP, 0, &stepData) == 0)
        {
            stepData.AppProtocol = 2;
            stepData.OutPut = 0;
            stepResult = cvxFileExport(
                VX_EXPORT_TYPE_STEP, stepPath, &stepData);
        }

        if (openedForExport)
            cvxFileClose();
    }

    if (originalContext.valid && !exportFromActiveFile)
        RestoreContext(originalContext);

    char message[1024] = {};
    sprintf_s(
        message,
        "[AsmStep] export ret=%d path=%s",
        stepResult,
        stepPath);
    DisplayMessage(message);

    if (stepResult != 0 || !FileExists(stepPath))
        return false;

    if (stepPathOut != nullptr)
        *stepPathOut = stepPath;

    return true;
}

static void PostAsmStepExportResult(
    const std::string& requestId,
    const std::string& stepFileName,
    const std::string& stepBase64,
    const char* error)
{
    std::string json =
        "{\"action\":\"asmStepExportReady\",\"requestId\":\"" +
        EscapeJsonString(SanitizeFileToken(
            requestId.empty() ? "asmstep" : requestId)) +
        "\",\"success\":";

    if (error == nullptr && !stepBase64.empty())
    {
        json += "true,\"stepFileName\":\"" +
                EscapeJsonString(stepFileName) +
                "\",\"stepBase64\":\"" + stepBase64 + "\"}";
    }
    else
    {
        json += "false,\"error\":\"" +
                EscapeJsonString(
                    error != nullptr ? error : "STEP export failed") +
                "\"}";
    }

    PostWebJson(json);
}

// Result receipt for asmParametricImport / asmParametricShape so the
// front end can await completion instead of fire-and-forget.
// `machineCode` carries a stable ASCII reason (e.g. no-active-document)
// that the front end maps to a localized message.
static void PostAsmExportResult(
    bool asShape,
    int result,
    const std::string& machineCode)
{
    if (!g_webview)
        return;

    std::string json = "{\"action\":\"asmExportResult\",\"mode\":\"";
    json += asShape ? "instance" : "part";
    json += "\",\"success\":";
    json += result == 0 ? "true" : "false";
    json += ",\"code\":" + std::to_string(result);
    if (!machineCode.empty())
    {
        json += ",\"message\":\"" + EscapeJsonString(machineCode) + "\"";
    }
    json += "}";

    const int wideLength = MultiByteToWideChar(
        CP_UTF8, 0, json.c_str(), -1, nullptr, 0);
    if (wideLength <= 0)
        return;

    std::vector<wchar_t> wideJson(static_cast<size_t>(wideLength));
    MultiByteToWideChar(
        CP_UTF8, 0, json.c_str(), -1, wideJson.data(), wideLength);
    g_webview->PostWebMessageAsJson(wideJson.data());
}

// Result receipt for readPartParams: the expression list of a part file.
static void PostPartParamsResult(
    const std::string& requestId,
    const std::string& rootName,
    const std::vector<svxVariable>* variables,
    const char* error)
{
    std::string json =
        "{\"action\":\"partParamsReady\",\"requestId\":\"" +
        EscapeJsonString(SanitizeFileToken(
            requestId.empty() ? "partparams" : requestId)) +
        "\",\"success\":";

    if (error == nullptr)
    {
        json += "true,\"root\":\"" + EscapeJsonString(rootName) +
                "\",\"params\":[";
        bool first = true;
        if (variables != nullptr)
        {
            for (const svxVariable& variable : *variables)
            {
                if (!first)
                    json += ',';
                first = false;

                char value[64] = {};
                sprintf_s(value, "%.15g", variable.Value);

                json += "{\"name\":\"" +
                        EscapeJsonString(variable.Name) +
                        "\",\"description\":\"" +
                        EscapeJsonString(variable.description) +
                        "\",\"value\":\"" + EscapeJsonString(value) +
                        "\",\"expression\":\"" +
                        EscapeJsonString(variable.Expression) + "\"}";
            }
        }
        json += "]}";
    }
    else
    {
        json += "false,\"error\":\"" +
                EscapeJsonString(
                    error != nullptr ? error : "Part parameters read failed") +
                "\"}";
    }

    PostWebJson(json);
}

static void AppendPreviewLog(const char* format, ...)
{
    char tempDirectory[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, tempDirectory) == 0)
        return;

    char logPath[MAX_PATH] = {};
    sprintf_s(
        logPath,
        "%sMyFirstPlugin_preview.log",
        tempDirectory);

    FILE* logFile = nullptr;
    fopen_s(&logFile, logPath, "a");
    if (!logFile)
        return;

    SYSTEMTIME now = {};
    GetLocalTime(&now);
    fprintf(
        logFile,
        "[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        now.wMilliseconds);

    va_list args;
    va_start(args, format);
    vfprintf(logFile, format, args);
    va_end(args);
    fputc('\n', logFile);
    fclose(logFile);
}

static bool WriteActiveModelBinaryStl(
    const char* stlPath,
    int* resultOut)
{
    if (resultOut)
        *resultOut = -1;

    if (!stlPath || !stlPath[0])
        return false;

    int shapeCount = 0;
    szwEntityHandle* shapes = nullptr;
    int result = ZwShapeListGet(&shapeCount, &shapes);
    AppendPreviewLog(
        "ZwShapeListGet ret=%d shapeCount=%d",
        result,
        shapeCount);

    if (result != 0 || shapeCount <= 0 || !shapes)
    {
        if (shapes)
            ZwEntityHandleListFree(shapeCount, &shapes);
        if (resultOut)
            *resultOut = result != 0 ? result : ZW_API_OBJ_DATA_GET_ERROR;
        return false;
    }

    std::vector<int> faceIds;

    for (int shapeIndex = 0;
         shapeIndex < shapeCount;
         ++shapeIndex)
    {
        int faceCount = 0;
        szwEntityHandle* shapeFaces = nullptr;
        const int faceResult =
            ZwShapeFaceListGet(
                shapes[shapeIndex],
                &faceCount,
                &shapeFaces);

        if (faceResult == 0 && faceCount > 0 && shapeFaces)
        {
            std::vector<int> ids(static_cast<size_t>(faceCount));
            const int idResult =
                ZwEntityIdGet(
                    faceCount,
                    shapeFaces,
                    ids.data());

            AppendPreviewLog(
                "ZwEntityIdGet shape=%d ret=%d faceCount=%d",
                shapeIndex,
                idResult,
                faceCount);

            if (idResult == 0)
            {
                faceIds.insert(
                    faceIds.end(),
                    ids.begin(),
                    ids.end());
            }
        }

        if (shapeFaces)
            ZwEntityHandleListFree(
                faceCount,
                &shapeFaces);
    }

    ZwEntityHandleListFree(shapeCount, &shapes);

    if (faceIds.empty())
    {
        if (resultOut)
            *resultOut = ZW_API_OBJ_DATA_GET_ERROR;
        return false;
    }

    FILE* file = nullptr;
    fopen_s(&file, stlPath, "wb");

    if (!file)
    {
        if (resultOut)
            *resultOut = ZW_API_INVALID_PATH;
        return false;
    }

    unsigned char header[80] = {};
    const char headerText[] =
        "MyFirstPlugin ZW3D interactive preview";
    memcpy(
        header,
        headerText,
        sizeof(headerText) - 1);
    fwrite(header, 1, sizeof(header), file);

    uint32_t triangleCount = 0;
    fwrite(
        &triangleCount,
        sizeof(triangleCount),
        1,
        file);

    int lastMeshError = ZW_API_NO_ERROR;

    for (const int faceId : faceIds)
    {
        int faceTriangleCount = 0;
        svxTriangle* faceTriangles = nullptr;
        const int meshResult =
            cvxPartFaceMesh(
                faceId,
                0.03,
                0.0,
                &faceTriangleCount,
                &faceTriangles);

        if (meshResult != 0)
            lastMeshError = meshResult;

        if (meshResult == 0 &&
            faceTriangleCount > 0 &&
            faceTriangles)
        {
            for (int triangleIndex = 0;
                 triangleIndex < faceTriangleCount;
                 ++triangleIndex)
            {
                const svxTriangle& triangle =
                    faceTriangles[triangleIndex];

                float values[12] = {
                    static_cast<float>(triangle.Normal.x),
                    static_cast<float>(triangle.Normal.y),
                    static_cast<float>(triangle.Normal.z),
                    static_cast<float>(triangle.Pnt[0].x),
                    static_cast<float>(triangle.Pnt[0].y),
                    static_cast<float>(triangle.Pnt[0].z),
                    static_cast<float>(triangle.Pnt[1].x),
                    static_cast<float>(triangle.Pnt[1].y),
                    static_cast<float>(triangle.Pnt[1].z),
                    static_cast<float>(triangle.Pnt[2].x),
                    static_cast<float>(triangle.Pnt[2].y),
                    static_cast<float>(triangle.Pnt[2].z)
                };
                const uint16_t attributeByteCount = 0;

                fwrite(values, sizeof(float), 12, file);
                fwrite(
                    &attributeByteCount,
                    sizeof(attributeByteCount),
                    1,
                    file);
            }

            triangleCount +=
                static_cast<uint32_t>(faceTriangleCount);
        }

        if (faceTriangles)
            ZwMemoryFree(
                reinterpret_cast<void**>(&faceTriangles));
    }

    if (fseek(file, 80, SEEK_SET) == 0)
        fwrite(&triangleCount, sizeof(triangleCount), 1, file);

    const bool writeSucceeded =
        ferror(file) == 0 && triangleCount > 0;
    fclose(file);

    AppendPreviewLog(
        "cvxPartFaceMesh faces=%d triangles=%u lastError=%d",
        static_cast<int>(faceIds.size()),
        static_cast<unsigned int>(triangleCount),
        lastMeshError);

    if (!writeSucceeded)
        DeleteFileA(stlPath);

    if (resultOut)
        *resultOut =
            writeSucceeded
                ? ZW_API_NO_ERROR
                : (lastMeshError != 0
                       ? lastMeshError
                       : ZW_API_GENERAL_ERROR);

    return writeSucceeded && FileExists(stlPath);
}

static bool ExportZ3prtToStl(
    const char* sourcePath,
    const char* stlPath,
    int* resultOut)
{
    if (resultOut)
        *resultOut = -1;

    if (!sourcePath || !stlPath)
        return false;

    // Do not depend on cvxFileOpen changing the visible active document. The
    // WebView is a secondary ZW3D window and the main window can remain on the
    // assembly, causing shape queries to see the assembly instead of sourcePath.
    // Activate the source file's part root as a paired background context.
    int rootCount = 0;
    vxRootName* rootNames = nullptr;
    int result =
        cvxRootList(
            sourcePath,
            &rootCount,
            &rootNames);
    AppendPreviewLog(
        "cvxRootList path=%s ret=%d rootCount=%d",
        sourcePath,
        result,
        rootCount);

    bool rootActivated = false;

    if (result == 0 && rootCount > 0 && rootNames)
    {
        for (int rootIndex = 0;
             rootIndex < rootCount;
             ++rootIndex)
        {
            const int activateResult =
                cvxRootActivate2(
                    sourcePath,
                    rootNames[rootIndex]);
            AppendPreviewLog(
                "cvxRootActivate2 index=%d name=%s ret=%d",
                rootIndex,
                rootNames[rootIndex],
                activateResult);

            if (activateResult != 0)
            {
                result = activateResult;
                continue;
            }

            int rootId = 0;
            evxRootType rootType = VX_ROOT_NULL;
            const int rootResult =
                cvxRootId(
                    rootNames[rootIndex],
                    &rootId,
                    &rootType);
            AppendPreviewLog(
                "cvxRootId index=%d ret=%d id=%d type=%d",
                rootIndex,
                rootResult,
                rootId,
                static_cast<int>(rootType));

            if (rootResult == 0 &&
                (rootType == VX_ROOT_PART ||
                 rootType == VX_ROOT_PART_NEW))
            {
                rootActivated = true;
                result = 0;
                break;
            }

            cvxRootActivate2(nullptr, nullptr);
            result =
                rootResult != 0
                    ? rootResult
                    : ZW_API_OBJ_TYPE_ERROR;
        }
    }

    if (rootNames)
        cvxMemFree(
            reinterpret_cast<void**>(&rootNames));

    if (!rootActivated)
    {
        if (resultOut)
            *resultOut =
                result != 0
                    ? result
                    : ZW_API_ROOT_OBJ_ACT_FAIL;
        return false;
    }

    DeleteFileA(stlPath);

    // Prefer the geometry API: read every face and create the triangle mesh
    // directly. This avoids interactive export-command state and produces a
    // real mesh even when the generic STL exporter is unavailable.
    result = -1;
    bool exported =
        WriteActiveModelBinaryStl(
            stlPath,
            &result);

    // Keep ZW3D's regular STL exporter as a compatibility fallback.
    if (!exported)
    {
        DeleteFileA(stlPath);

        svxSTLData options = {};
        result =
            cvxFileExportInit(
                VX_EXPORT_TYPE_STL,
                0,
                &options);

        if (result == 0)
        {
            options.FileFormat = 0;
            options.Coordinate = 0;
            options.TessellationTol = 0.03;
            options.MeshSize = 0.0;

            result =
                cvxFileExport(
                    VX_EXPORT_TYPE_STL,
                    stlPath,
                    &options);
        }

        exported =
            result == 0 &&
            FileExists(stlPath);
    }

    // Restore the exact file/root context saved by cvxRootActivate2.
    cvxRootActivate2(nullptr, nullptr);

    if (resultOut)
        *resultOut = result;

    return exported;
}

static void ExtractZ3prtPreview(
    const std::string& requestId,
    const std::string& source,
    const std::string& bearerToken)
{
    if (source.empty())
    {
        PostZ3prtPreviewResult(
            requestId,
            "",
            "No Z3PRT path was provided");
        return;
    }

    char downloadedPath[600] = {};
    const char* sourcePath = source.c_str();
    bool downloadedSource = false;

    if (strncmp(sourcePath, "http://", 7) == 0 ||
        strncmp(sourcePath, "https://", 8) == 0)
    {
        DWORD httpStatus = 0;

        if (!DownloadFile(
                sourcePath,
                downloadedPath,
                static_cast<int>(sizeof(downloadedPath)),
                bearerToken.c_str(),
                &httpStatus))
        {
            char errorMessage[160] = {};
            sprintf_s(
                errorMessage,
                "Unable to download the Z3PRT file (HTTP %lu)",
                static_cast<unsigned long>(httpStatus));

            PostZ3prtPreviewResult(
                requestId,
                "",
                errorMessage);
            return;
        }

        sourcePath = downloadedPath;
        downloadedSource = true;
    }

    if (!FileExists(sourcePath))
    {
        if (downloadedSource)
            DeleteFileA(downloadedPath);

        PostZ3prtPreviewResult(
            requestId,
            "",
            "The Z3PRT file does not exist");
        return;
    }

    char tempDirectory[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, tempDirectory) == 0)
    {
        if (downloadedSource)
            DeleteFileA(downloadedPath);

        PostZ3prtPreviewResult(
            requestId,
            "",
            "Unable to resolve the temporary directory");
        return;
    }

    // Z3PRT is a proprietary CAD format that browsers cannot render directly.
    // Ask ZW3D to tessellate it to a temporary binary STL and return that mesh
    // to the embedded front end for an interactive Three.js preview.
    char stlPath[MAX_PATH] = {};
    sprintf_s(
        stlPath,
        "%sMyFirstPlugin_mesh_%lu_%llu.stl",
        tempDirectory,
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long long>(GetTickCount64()));

    int stlResult = -1;
    if (ExportZ3prtToStl(
            sourcePath,
            stlPath,
            &stlResult))
    {
        const std::vector<unsigned char> stl =
            ReadBinaryFile(stlPath);
        DeleteFileA(stlPath);

        if (!stl.empty())
        {
            if (downloadedSource)
                DeleteFileA(downloadedPath);

            PostZ3prtPreviewResult(
                requestId,
                EncodeBase64(stl),
                nullptr,
                "z3prtMeshReady",
                "model/stl");
            return;
        }

        DisplayMessage(
            "[Preview] ZW3D exported an empty STL; "
            "falling back to the embedded bitmap.");
    }
    else
    {
        char message[256] = {};
        sprintf_s(
            message,
            "[Preview] STL export failed (ret=%d); "
            "falling back to the embedded bitmap.",
            stlResult);
        DisplayMessage(message);
        DeleteFileA(stlPath);
    }

    char bitmapPath[MAX_PATH] = {};
    sprintf_s(
        bitmapPath,
        "%sMyFirstPlugin_preview_%lu_%llu.bmp",
        tempDirectory,
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long long>(GetTickCount64()));

    DeleteFileA(bitmapPath);

    const int result =
        cvxFilePreviewExtract(
            sourcePath,
            bitmapPath);

    if (result != 0 || !FileExists(bitmapPath))
    {
        char message[256] = {};
        sprintf_s(
            message,
            "ZW3D preview extraction failed (ret=%d)",
            result);
        DisplayMessage(message);
        PostZ3prtPreviewResult(
            requestId,
            "",
            message);
        DeleteFileA(bitmapPath);
        if (downloadedSource)
            DeleteFileA(downloadedPath);
        return;
    }

    const std::vector<unsigned char> bitmap =
        ReadBinaryFile(bitmapPath);
    DeleteFileA(bitmapPath);
    if (downloadedSource)
        DeleteFileA(downloadedPath);

    if (bitmap.empty())
    {
        PostZ3prtPreviewResult(
            requestId,
            "",
            "ZW3D returned an empty preview bitmap");
        return;
    }

    PostZ3prtPreviewResult(
        requestId,
        EncodeBase64(bitmap),
        nullptr);
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
    int asShape = 0,
    const char* bearerToken = nullptr)
{
    // If the "path" is actually a URL, download it first.
    char localPathBuf[600] = {};
    bool downloadedSource = false;
    if (sourcePartPath && (strncmp(sourcePartPath, "http://", 7) == 0 ||
                           strncmp(sourcePartPath, "https://", 8) == 0))
    {
        if (!DownloadFile(
                sourcePartPath,
                localPathBuf,
                static_cast<int>(sizeof(localPathBuf)),
                bearerToken))
        {
            return -1;
        }

        sourcePartPath = localPathBuf;
        downloadedSource = true;
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
        if (downloadedSource)
            DeleteFileA(sourcePartPath);
        return -1;
    }

    if (assignmentText == nullptr ||
        assignmentText[0] == '\0')
    {
        DisplayMessage(
            "[PartOut] Parameterized import has no "
            "expression assignments.");
        if (downloadedSource)
            DeleteFileA(sourcePartPath);
        return -1;
    }

    // 1. Remember the exact destination file and root we're in.  A ZW3D file
    // can contain more than one root object, so restoring only the filename
    // may insert into a different root than the one the user was viewing.
    ZwDocumentContext destinationContext = {};
    cvxFileInqActive(
        destinationContext.file,
        static_cast<int>(sizeof(destinationContext.file)));
    cvxRootInqActive(
        destinationContext.root,
        static_cast<int>(sizeof(destinationContext.root)));
    destinationContext.valid =
        destinationContext.root[0] != '\0';

    // Stock-out must place the generated part into the model that was active
    // when the user started the export.  Do not fall back to leaving the
    // generated instance open as a new document: that bypasses placement and
    // makes it look as though the part was exported to the wrong file.
    if (!destinationContext.valid)
    {
        DisplayMessage(
            SysStr(
                L"[PartOut] 请先打开并激活要放置零件的目标文件，然后重新出库。")
                .c_str());
        if (downloadedSource)
            DeleteFileA(sourcePartPath);
        return -1;
    }

    // 2. Use a writable per-user temporary directory.  The previous fixed
    // D:\\ZW3D_Plugins path caused every export to fail on machines where
    // that directory did not exist or could not be created.
    const std::string instanceDirectory = GetInstanceDirectory();
    if (instanceDirectory.empty())
    {
        DisplayMessage("[PartOut] Cannot create the user instance directory.");
        if (downloadedSource)
            DeleteFileA(sourcePartPath);
        return -1;
    }

    // 3. Create unique instance path and copy source there
    const std::string instancePath =
        CreateUniqueInstancePath(
            sourcePartPath,
            assignmentText,
            instanceDirectory);

    if (!CopyFileA(sourcePartPath, instancePath.c_str(), TRUE))
    {
        char msg[512];
        sprintf_s(msg, "[PartOut] CopyFile failed: %s -> %s",
                  sourcePartPath, instancePath.c_str());
        DisplayMessage(msg);
        if (downloadedSource)
            DeleteFileA(sourcePartPath);
        return -1;
    }

    // 4. Find the copied part's root without opening it in a visible window.
    // cvxRootActivate2 then edits it in the background and remembers the
    // current target (including a new, unsaved assembly) on ZW3D's stack.
    int rootCount = 0;
    vxRootName* rootNames = nullptr;
    int ret = cvxRootList(instancePath.c_str(), &rootCount, &rootNames);
    std::string instanceRoot;
    if (ret == 0 && rootCount > 0 && rootNames != nullptr)
        instanceRoot = rootNames[0];
    if (rootNames != nullptr)
        cvxMemFree(reinterpret_cast<void**>(&rootNames));

    if (ret != 0 || instanceRoot.empty())
    {
        char msg[512];
        sprintf_s(
            msg,
            "[PartOut] cvxRootList failed: ret=%d file=%s",
            ret,
            instancePath.c_str());
        DisplayMessage(msg);
        DeleteFileA(instancePath.c_str());
        if (downloadedSource)
            DeleteFileA(sourcePartPath);
        return ret != 0 ? ret : -1;
    }

    ret = cvxRootActivate2(instancePath.c_str(), instanceRoot.c_str());
    if (ret != 0)
    {
        char msg[512];
        sprintf_s(
            msg,
            "[PartOut] cvxRootActivate2 failed: ret=%d file=%s root=%s",
            ret,
            instancePath.c_str(),
            instanceRoot.c_str());
        DisplayMessage(msg);
        DeleteFileA(instancePath.c_str());
        if (downloadedSource)
            DeleteFileA(sourcePartPath);
        return -1;
    }

    // 5. Apply expression changes
    ret = ApplyExpressionAssignments(assignmentText);
    if (ret != 0)
    {
        DisplayMessage("[PartOut] Expression update failed.");
        cvxRootActivate2(nullptr, nullptr);
        ClosePluginDocument(instancePath.c_str());
        DeleteFileA(instancePath.c_str());
        if (downloadedSource)
            DeleteFileA(sourcePartPath);
        return ret;
    }

    // 6. Save the modified instance
    ret = cvxFileSave(1);
    if (ret != 0)
    {
        char msg[512];
        sprintf_s(msg, "[PartOut] cvxFileSave failed: ret=%d", ret);
        DisplayMessage(msg);
        cvxRootActivate2(nullptr, nullptr);
        ClosePluginDocument(instancePath.c_str());
        DeleteFileA(instancePath.c_str());
        if (downloadedSource)
            DeleteFileA(sourcePartPath);
        return ret;
    }

    // 7. Pop the background target and return to the exact destination that
    // was active before export. This also works for an unsaved blank assembly.
    ret = cvxRootActivate2(nullptr, nullptr);
    if (ret != 0)
    {
        DisplayMessage("[PartOut] Unable to restore the target document.");
        ClosePluginDocument(instancePath.c_str());
        DeleteFileA(instancePath.c_str());
        if (downloadedSource)
            DeleteFileA(sourcePartPath);
        return ret;
    }

    // The generated file remains on disk for the component reference, but it
    // must not remain open as a separate user-visible document.
    ClosePluginDocument(instancePath.c_str());

    // 8. Let the user choose the actual placement in the ZW3D viewport.  The
    // WebView is hidden during the native point-pick so it cannot intercept
    // the click intended for the model window.
    svxPoint insertionPoint = {};
    ret = PickInsertionPoint(&insertionPoint);
    if (ret != 0)
    {
        DeleteFileA(instancePath.c_str());
        if (downloadedSource)
            DeleteFileA(sourcePartPath);
        return ret;
    }

    // 9. Insert the instance into the assembly at the selected point.
    std::string dir, file;
    SplitFilePath(instancePath, &dir, &file);
    std::string partName = FileNameWithoutExtension(file);

    if (asShape)
    {
        ret = InsertPartWithParamsAt(
            dir.c_str(), file.c_str(), partName.c_str(),
            "", 1, 0,
            insertionPoint.x, insertionPoint.y, insertionPoint.z);
    }
    else
    {
        ret = InsertComponentFromFile(
            instancePath.c_str(), partName.c_str(), &insertionPoint);
    }

    if (ret == 0)
    {
        char msg[512];
        sprintf_s(msg, "[PartOut] Imported: %s, mode=%s",
                  instancePath.c_str(), asShape ? "shape" : "component");
        DisplayMessage(msg);

        // Only remove the temporary download. Never delete a caller-provided
        // local Z3PRT source file after a successful export.
        if (downloadedSource)
            DeleteFileA(sourcePartPath);
    }
    else
    {
        DeleteFileA(instancePath.c_str());
        if (downloadedSource)
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
    InsertAssemblyComponent(ASM_DIR, "testzbt.Z3ASM", "testzbt", 1);
}

void OnAsmShape()
{
    DisplayMessage("[AsmOut] Assembly as shape...");
    InsertAssemblyShape(ASM_DIR, "testzbt.Z3ASM", "testzbt");
}

// ============================================================
// Asm Parametric — list components, log expressions, insert as new file
// ============================================================
void OnAsmParametric()
{
    DisplayMessage("[AsmParam] === START ===");

    char srcPath[512], curFile[512] = {}, rackPath[512] = {};
    sprintf_s(srcPath, "%s\\testzbt.Z3ASM", ASM_DIR);
    char m[512];

    // 1. Remember current doc, open assembly to query components
    cvxFileInqActive(curFile, sizeof(curFile));
    if (cvxFileOpen(srcPath) != 0) { DisplayMessage("[AsmParam] open asm failed"); return; }

    // 2. Query components, find rack by model number (ASCII-safe matching)
    char root[256] = {}; cvxRootInqActive(root, sizeof(root));
    int count = 0; vxLongPath* paths = nullptr; vxRootName* names = nullptr;
    cvxPartInqCompsInfoByLongPath(srcPath, root, &count, &paths, &names);

    for (int i = 0; i < count && paths && names; ++i)
        if (strstr(names[i], "T1356")) { strcpy_s(rackPath, paths[i]); break; }
    if (paths) cvxMemFree((void**)&paths);
    if (names) cvxMemFree((void**)&names);
    sprintf_s(m, "[AsmParam] 1.rack=%s", rackPath); DisplayMessage(m);

    // 3. Open rack, modify h=5, save, close, restore
    if (rackPath[0] && cvxFileOpen(rackPath) == 0)
    {
        SetCurrentPartExpression("h", "5");
        cvxFileSave(1); cvxFileClose();
    }
    if (curFile[0]) cvxFileActivate(curFile);

    // 4. Insert
    InsertAssemblyComponent(ASM_DIR, "testzbt.Z3ASM", "testzbt", 1);

    DisplayMessage("[AsmParam] === DONE ===");
}

void OnAsmParametricShape()
{
    DisplayMessage("[AsmParamShape] === START ===");

    char srcPath[512], curFile[512] = {}, rackPath[512] = {};
    sprintf_s(srcPath, "%s\\testzbt.Z3ASM", ASM_DIR);

    cvxFileInqActive(curFile, sizeof(curFile));
    if (cvxFileOpen(srcPath) != 0) return;

    char root[256] = {}; cvxRootInqActive(root, sizeof(root));
    int count = 0; vxLongPath* paths = nullptr; vxRootName* names = nullptr;
    cvxPartInqCompsInfoByLongPath(srcPath, root, &count, &paths, &names);

    for (int i = 0; i < count && paths && names; ++i)
        if (strstr(names[i], "T1356")) { strcpy_s(rackPath, paths[i]); break; }
    if (paths) cvxMemFree((void**)&paths);
    if (names) cvxMemFree((void**)&names);

    if (rackPath[0] && cvxFileOpen(rackPath) == 0)
    {
        SetCurrentPartExpression("h", "5");
        cvxFileSave(1); cvxFileClose();
    }
    if (curFile[0]) cvxFileActivate(curFile);

    InsertAssemblyShape(ASM_DIR, "testzbt.Z3ASM", "testzbt");

    DisplayMessage("[AsmParamShape] === DONE ===");
}

void OnAsmParametric2()
{
    DisplayMessage("[AsmParam2] === START ===");

    char srcPath[512], curFile[512] = {};
    sprintf_s(srcPath, "%s\\testzbt.Z3ASM", ASM_DIR);

    cvxFileInqActive(curFile, sizeof(curFile));
    if (cvxFileOpen(srcPath) != 0) return;

    char root[256] = {}; cvxRootInqActive(root, sizeof(root));
    int count = 0; vxLongPath* paths = nullptr; vxRootName* names = nullptr;
    cvxPartInqCompsInfoByLongPath(srcPath, root, &count, &paths, &names);

    char rackPath[512] = {}, studPath[512] = {};
    for (int i = 0; i < count && paths && names; ++i)
    {
        if (strstr(names[i], "T1356")) strcpy_s(rackPath, paths[i]);
        if (strstr(names[i], "899"))   strcpy_s(studPath, paths[i]);
    }
    if (paths) cvxMemFree((void**)&paths);
    if (names) cvxMemFree((void**)&names);

    // Modify rack: h=15
    char m[512];
    sprintf_s(m, "[AsmParam2] rack=%s stud=%s", rackPath, studPath); DisplayMessage(m);

    if (rackPath[0] && cvxFileOpen(rackPath) == 0)
    {
        DisplayMessage("[AsmParam2] modifying rack h=15");
        SetCurrentPartExpression("h", "15");
        cvxFileSave(1); cvxFileClose();   // 改完就关
    }
    if (studPath[0] && cvxFileOpen(studPath) == 0)
    {
        DisplayMessage("[AsmParam2] modifying stud l=24");
        SetCurrentPartExpression("l", "24");
        cvxFileSave(1); cvxFileClose();   // 改完就关
    }
    if (curFile[0]) cvxFileActivate(curFile);
    InsertAssemblyComponent(ASM_DIR, "testzbt.Z3ASM", "testzbt", 1);

    DisplayMessage("[AsmParam2] === DONE ===");
}

// \u88c5\u914d = 装配 (Unicode escapes, encoding-independent)
#define TAKE_DIR  L"C:\\Users\\zxcvb\\Documents\\ZW3D\\zbttestpart\\test\\takeouttest"
#define TAKE_FILE L"\u88c5\u914d001.Z3ASM"
#define TAKE_PART L"\u88c5\u914d001"

void OnTakeoutOpen()
{
    char m[1024];
    sprintf_s(m, "[TakeoutOpen] open %s",
        SysStr(TAKE_DIR L"\\" TAKE_FILE).c_str());
    DisplayMessage(m);
    cvxFileOpen(SysStr(TAKE_DIR L"\\" TAKE_FILE).c_str());
}

// Insert an assembly (.Z3ASM) as a component via the standard component insert
// API. cvxLibPartIns (reuse-library insert) fails to resolve nested
// sub-assemblies (装配001 -> abc/cba), but cvxCompIns uses the normal
// same-directory + search-path resolution, so nested assemblies work.
static int InsertAssemblyComponent(
    const char* dir,
    const char* file,
    const char* part,
    int copyPart,
    const svxPoint* insertionPoint)
{
    const std::string fullPath = std::string(dir) + "\\" + file;
    cvxPathSearchFirst(dir);

    // The legacy cvxCompIns interface can ignore Dir and then search only by
    // file name.  That is exactly the failure shown as "cannot locate ...".
    // The current API explicitly supports a full path for an unopened file.
    szwComponentInsertData component = {};
    int ret = static_cast<int>(ZwComponentInsertInit(&component));
    if (ret != 0)
    {
        char m[512];
        sprintf_s(m, "[AsmIns] ZwComponentInsertInit failed ret=%d", ret);
        DisplayMessage(m);
        return ret;
    }

    strcpy_s(component.pathFile, sizeof(component.pathFile), fullPath.c_str());
    strcpy_s(component.root, sizeof(component.root), part);
    component.instanceData.copyPart = copyPart ? 1 : 0;

    szwMatrix placement = {};
    if (insertionPoint)
    {
        ret = static_cast<int>(ZwMatrixInitByTranslation(
            insertionPoint->x, insertionPoint->y, insertionPoint->z,
            &placement));
        if (ret != 0)
            return ret;
        component.frame = &placement;
    }

    szwEntityHandle componentHandle = {};
    ret = static_cast<int>(
        ZwComponentInsert(component, nullptr, &componentHandle));

    char m[1024];
    sprintf_s(m, "[AsmIns] ZwComponentInsert ret=%d file=%s root=%s copyPart=%d",
        ret, fullPath.c_str(), part, copyPart ? 1 : 0);
    DisplayMessage(m);

    return ret;
}

void OnTakeoutInsert()
{
    InsertAssemblyComponent(
        SysStr(TAKE_DIR).c_str(),
        SysStr(TAKE_FILE).c_str(),
        SysStr(TAKE_PART).c_str(),
        0);   // reference, no copy (test)
}

// Insert an assembly (.Z3ASM) as a shape via cvxInstPartAsShp. Same root cause
// as component insert: cvxLibPartIns fails on nested sub-assemblies, so use the
// non-reuse-library shape API which resolves through the normal mechanism.
static int InsertAssemblyShape(
    const char* dir,
    const char* file,
    const char* part,
    const svxPoint* insertionPoint)
{
    cvxPathSearchFirst(dir);

    svxCompData component = {};

    int ret = cvxCompInsInit(&component);
    if (ret != 0)
    {
        char m[512];
        sprintf_s(m, "[AsmShape] cvxCompInsInit failed ret=%d", ret);
        DisplayMessage(m);
        return ret;
    }

    const std::string fullPath = std::string(dir) + "\\" + file;
    strcpy_s(component.Dir, sizeof(component.Dir), dir);
    strcpy_s(component.File, sizeof(component.File), fullPath.c_str());
    strcpy_s(component.Part, sizeof(component.Part), part);

    if (insertionPoint)
    {
        component.Frame.identity = 0;
        component.Frame.xx = 1.0;
        component.Frame.yy = 1.0;
        component.Frame.zz = 1.0;
        component.Frame.xt = insertionPoint->x;
        component.Frame.yt = insertionPoint->y;
        component.Frame.zt = insertionPoint->z;
    }
    else
    {
        component.Frame.identity = 1;
    }
    component.SettingsData.AutoActivated = 0;
    component.InstanceData.CopyPart = 0;

    evxErrors e = cvxInstPartAsShp(&component);

    char m[1024];
    sprintf_s(m, "[AsmShape] cvxInstPartAsShp err=%d file=%s part=%s",
        (int)e, fullPath.c_str(), part);
    DisplayMessage(m);

    return (int)e;
}

void OnTakeoutShape()
{
    InsertAssemblyShape(
        SysStr(TAKE_DIR).c_str(),
        SysStr(TAKE_FILE).c_str(),
        SysStr(TAKE_PART).c_str());
}

void OnAsmOpen()
{
    char asmPath[512];
    sprintf_s(asmPath, "%s\\testzbt.Z3ASM", ASM_DIR);
    cvxFileOpen(asmPath);
}

// ============================================================
// Assembly bridge workflows — front-end callable interfaces
// ============================================================
//
// These mirror the second-row toolbar buttons but take the assembly path and
// parameter spec from the front end instead of the hardcoded test constants.
// The toolbar buttons (OnAsmNewFile / OnAsmShape / ...) are left unchanged.

struct AsmSubComponent
{
    std::string name;       // root name inside the assembly (for matching)
    std::string sourcePath; // source long path of the sub-component file
    std::string fileName;   // filename portion of sourcePath
};

static std::string LowerAscii(std::string text)
{
    for (char& character : text)
    {
        character = static_cast<char>(
            tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

// Comparison form for instance matching: removes spaces / underscores /
// hyphens / path separators and uppercases ASCII, so that
// "垫圈 GB_T 96.2-3" matches "垫圈GBT96.23".
static std::string NormalizeInstanceName(const std::string& text)
{
    std::string result;
    result.reserve(text.size());
    for (unsigned char byte : text)
    {
        if (byte == ' ' || byte == '_' || byte == '-' ||
            byte == '\\' || byte == '/')
            continue;
        result.push_back(static_cast<char>(toupper(byte)));
    }
    return result;
}

// Matches the "nameSubstring" in a parameter segment against a component's
// root name or file name: first exact match, then bidirectional substring match.
static bool ComponentNameMatches(
    const AsmSubComponent& component,
    const std::string& substring)
{
    const std::string want = NormalizeInstanceName(substring);
    if (want.empty())
        return false;

    const std::string name = NormalizeInstanceName(component.name);
    const std::string file =
        NormalizeInstanceName(FileNameWithoutExtension(component.fileName));

    if (!name.empty() && (name == want ||
        name.find(want) != std::string::npos ||
        (name.size() >= 4 && want.find(name) != std::string::npos)))
        return true;

    if (!file.empty() && (file == want ||
        file.find(want) != std::string::npos ||
        (file.size() >= 4 && want.find(file) != std::string::npos)))
        return true;

    return false;
}

static bool ContainsIgnoreCase(
    const char* haystack,
    const char* needle)
{
    if (haystack == nullptr ||
        needle == nullptr ||
        needle[0] == '\0')
    {
        return false;
    }

    const size_t hayLen = strlen(haystack);
    const size_t needleLen = strlen(needle);

    if (needleLen > hayLen)
        return false;

    for (size_t i = 0; i + needleLen <= hayLen; ++i)
    {
        size_t j = 0;

        for (; j < needleLen; ++j)
        {
            if (toupper(
                    static_cast<unsigned char>(
                        haystack[i + j])) !=
                toupper(
                    static_cast<unsigned char>(
                        needle[j])))
            {
                break;
            }
        }

        if (j == needleLen)
            return true;
    }

    return false;
}

static std::string CreateUniqueAssemblyInstanceDirectory(
    const std::string& baseName)
{
    SYSTEMTIME time = {};
    GetLocalTime(&time);

    char token[128] = {};

    sprintf_s(
        token,
        "%04u%02u%02u_%02u%02u%02u_%03u",
        static_cast<unsigned int>(time.wYear),
        static_cast<unsigned int>(time.wMonth),
        static_cast<unsigned int>(time.wDay),
        static_cast<unsigned int>(time.wHour),
        static_cast<unsigned int>(time.wMinute),
        static_cast<unsigned int>(time.wSecond),
        static_cast<unsigned int>(time.wMilliseconds));

    std::string directory =
        GetInstanceDirectory();

    if (directory.empty())
        return "";

    directory += "\\";
    directory += baseName;
    directory += "_";
    directory += token;

    return directory;
}

// asmOpen|path — open a .Z3ASM.
static int AsmOpenWorkflow(
    const char* path)
{
    if (path == nullptr || path[0] == '\0')
    {
        DisplayMessage("[AsmOut] asmOpen: empty path.");
        return -1;
    }

    if (!FileExists(path))
    {
        char m[1024] = {};
        sprintf_s(m, "[AsmOut] asmOpen: file not found: %s", path);
        DisplayMessage(m);
        return -1;
    }

    const int ret = cvxFileOpen(path);

    char m[512] = {};
    sprintf_s(m, "[AsmOut] asmOpen: cvxFileOpen ret=%d file=%s", ret, path);
    DisplayMessage(m);

    return ret;
}

// asmImport|path / asmImportRef|path — insert .Z3ASM as component.
// copyPart=1 → new file, copyPart=0 → external reference.
static int AsmComponentInsertWorkflow(
    const char* path,
    int copyPart)
{
    if (path == nullptr || path[0] == '\0')
    {
        DisplayMessage("[AsmOut] asmImport: empty path.");
        return -1;
    }

    if (!FileExists(path))
    {
        char m[1024] = {};
        sprintf_s(m, "[AsmOut] asmImport: file not found: %s", path);
        DisplayMessage(m);
        return -1;
    }

    std::string directory;
    std::string fileName;

    if (!SplitFilePath(path, &directory, &fileName))
    {
        DisplayMessage("[AsmOut] asmImport: cannot split path.");
        return -1;
    }

    const std::string part =
        FileNameWithoutExtension(fileName);

    return InsertAssemblyComponent(
        directory.c_str(),
        fileName.c_str(),
        part.c_str(),
        copyPart);
}

// asmShape|path — insert .Z3ASM as shape.
static int AsmShapeInsertWorkflow(
    const char* path)
{
    if (path == nullptr || path[0] == '\0')
    {
        DisplayMessage("[AsmOut] asmShape: empty path.");
        return -1;
    }

    if (!FileExists(path))
    {
        char m[1024] = {};
        sprintf_s(m, "[AsmOut] asmShape: file not found: %s", path);
        DisplayMessage(m);
        return -1;
    }

    std::string directory;
    std::string fileName;

    if (!SplitFilePath(path, &directory, &fileName))
    {
        DisplayMessage("[AsmOut] asmShape: cannot split path.");
        return -1;
    }

    const std::string part =
        FileNameWithoutExtension(fileName);

    return InsertAssemblyShape(
        directory.c_str(),
        fileName.c_str(),
        part.c_str());
}

// asmParametricImport|path|spec / asmParametricShape|path|spec
//
// Non-destructive parameterized assembly insert: copy the source .Z3ASM and its
// direct sub-components into a fresh instance directory, edit the copied
// sub-component expressions there, then insert the copied assembly.
//
// spec format: "nameSubstring:expr=value;...", e.g. "T1356:h=5;899:l=24".

// Machine-readable failure/warning code surfaced to the front end so the
// user gets an actionable message instead of a bare "error code -1".
static std::string g_asmOutError;

static int AsmParametricWorkflow(
    const char* path,
    const char* spec,
    int asShape,
    const char* sourceRootHint = nullptr)
{
    g_asmOutError.clear();

    if (path == nullptr || path[0] == '\0')
    {
        DisplayMessage("[AsmParamOut] empty assembly path.");
        g_asmOutError = "empty-path";
        return -1;
    }

    std::string sourcePath = path;

    if (!FileExists(sourcePath.c_str()))
    {
        // The library may have been imported on the server, so the recorded
        // source path can be absent on this ZW3D machine. When the active
        // document carries the same file base name, use it instead.
        const ZwDocumentContext probe = CaptureActiveContext();
        const std::string wantBase =
            LowerAscii(FileNameWithoutExtension(sourcePath));
        if (probe.valid && !wantBase.empty() &&
            LowerAscii(FileNameWithoutExtension(probe.file)) == wantBase)
        {
            char activeDirectory[600] = {};
            cvxFileDirectoryByLongPath(
                activeDirectory,
                static_cast<int>(sizeof(activeDirectory)));

            std::string activePath = activeDirectory;
            if (!activePath.empty() &&
                activePath.back() != '\\' &&
                activePath.back() != '/')
            {
                activePath += "\\";
            }
            activePath += probe.file;

            if (activeDirectory[0] == '\0' ||
                !FileExists(activePath.c_str()))
            {
                DisplayMessage(
                    "[AsmParamOut] matching active assembly has no "
                    "resolvable saved path.");
                g_asmOutError = "source-not-found";
                return -1;
            }

            char m[1024] = {};
            sprintf_s(
                m,
                "[AsmParamOut] source not present locally; falling back to "
                "active document %s",
                activePath.c_str());
            DisplayMessage(m);
            sourcePath = activePath;
        }
        else
        {
            char m[1024] = {};
            sprintf_s(m, "[AsmParamOut] file not found: %s", path);
            DisplayMessage(m);
            g_asmOutError = "source-not-found";
            return -1;
        }
    }

    // An empty spec is allowed: the assembly simply has no parameters to
    // modify, so the copy/insert workflow runs without editing expressions.
    const bool hasSpec = spec != nullptr && spec[0] != '\0';

    std::string sourceDir;
    std::string sourceFile;

    if (!SplitFilePath(sourcePath, &sourceDir, &sourceFile))
    {
        DisplayMessage("[AsmParamOut] cannot split assembly path.");
        g_asmOutError = "bad-path";
        return -1;
    }

    // 1. Enumerate direct sub-components of the source assembly.
    const ZwDocumentContext context =
        CaptureActiveContext();

    if (!context.valid)
    {
        DisplayMessage(
            "[AsmParamOut] no active document to insert into.");
        g_asmOutError = "no-active-document";
        return -1;
    }

    char root[256] = {};
    std::string sourceRootName =
        Trim(sourceRootHint ? sourceRootHint : "");
    if (sourceRootName.empty())
        sourceRootName = FileNameWithoutExtension(sourceFile);
    strcpy_s(root, sourceRootName.c_str());

    int count = 0;
    vxLongPath* paths = nullptr;
    vxRootName* names = nullptr;
    int componentsResult =
        root[0] != '\0'
            ? cvxPartInqCompsInfoByLongPath(
                  sourcePath.c_str(),
                  root,
                  &count,
                  &paths,
                  &names)
            : -1;

    // Most files can be queried directly by path/root without activating a
    // document.  Only fall back to a visible open when the offline query is
    // unavailable, and close exactly the file opened by this workflow.
    if (componentsResult != 0 || count <= 0 || !paths || !names)
    {
        if (paths) cvxMemFree((void**)&paths);
        if (names) cvxMemFree((void**)&names);
        paths = nullptr;
        names = nullptr;
        count = 0;

        const bool sourceAlreadyActive =
            ActiveDocumentMatchesPath(sourcePath.c_str());
        const int openRet = sourceAlreadyActive
            ? 0
            : cvxFileOpen(sourcePath.c_str());

        if (openRet == 0 && ActiveDocumentMatchesPath(sourcePath.c_str()))
        {
            cvxRootInqActive(root, sizeof(root));
            sourceRootName = root;
            componentsResult = root[0] != '\0'
                ? cvxPartInqCompsInfoByLongPath(
                      sourcePath.c_str(),
                      root,
                      &count,
                      &paths,
                      &names)
                : -1;

            if (!sourceAlreadyActive)
            {
                if (!ClosePluginDocument(sourcePath.c_str()))
                {
                    if (paths) cvxMemFree((void**)&paths);
                    if (names) cvxMemFree((void**)&names);
                    g_asmOutError = "close-failed";
                    return -1;
                }
                RestoreContext(context);
            }
        }
        else
        {
            componentsResult = openRet != 0 ? openRet : -1;
        }
    }

    if (componentsResult != 0 || count <= 0 || !paths || !names)
    {
        if (paths) cvxMemFree((void**)&paths);
        if (names) cvxMemFree((void**)&names);

        char m[256] = {};
        sprintf_s(
            m,
            "[AsmParamOut] cannot enumerate sub-components: ret=%d count=%d",
            componentsResult,
            count);
        DisplayMessage(m);
        g_asmOutError =
            componentsResult != 0 ? "enum-failed" : "enum-empty";
        return componentsResult != 0 ? componentsResult : -1;
    }

    std::vector<AsmSubComponent> components;

    for (int i = 0; i < count && paths && names; ++i)
    {
        AsmSubComponent component;

        component.name = names[i];
        component.sourcePath = paths[i];

        std::string subDir;
        std::string subFile;

        if (SplitFilePath(component.sourcePath, &subDir, &subFile))
            component.fileName = subFile;
        else
            component.fileName = component.sourcePath;

        components.push_back(component);
    }

    if (paths) cvxMemFree((void**)&paths);
    if (names) cvxMemFree((void**)&names);

    // Recurse into nested sub-assemblies (.Z3ASM components) so every
    // level's parts get copied into the instance directory and remain
    // editable. Database assembly reuse is represented by explicit parent-child
    // relations; this recursion only follows the component structure in the file.
    {
        auto appendComponent = [&components](
            const std::string& rootName,
            const std::string& longPath) {
            AsmSubComponent component;
            component.name = rootName;
            component.sourcePath = longPath;
            std::string directory;
            std::string fileName;
            if (SplitFilePath(component.sourcePath, &directory, &fileName))
                component.fileName = fileName;
            else
                component.fileName = component.sourcePath;
            components.push_back(component);
        };

        std::vector<AsmSubComponent> frontier(components);
        std::vector<std::string> visited;
        for (const AsmSubComponent& component : components)
            visited.push_back(LowerAscii(component.sourcePath));

        auto alreadyVisited = [&visited](const std::string& path) {
            const std::string key = LowerAscii(path);
            for (const std::string& existing : visited)
            {
                if (existing == key)
                    return true;
            }
            return false;
        };

        for (int depth = 0; depth < 6 && !frontier.empty(); ++depth)
        {
            std::vector<AsmSubComponent> next;

            for (const AsmSubComponent& parent : frontier)
            {
                const size_t dot = parent.fileName.find_last_of('.');
                const std::string extension = dot == std::string::npos
                    ? std::string()
                    : LowerAscii(parent.fileName.substr(dot));
                if (extension != ".z3asm")
                    continue;

                int nestedCount = 0;
                vxLongPath* nestedPaths = nullptr;
                vxRootName* nestedNames = nullptr;
                int nestedRet = cvxPartInqCompsInfoByLongPath(
                    parent.sourcePath.c_str(),
                    parent.name.c_str(),
                    &nestedCount,
                    &nestedPaths,
                    &nestedNames);

                if (nestedRet != 0 || !nestedPaths || !nestedNames)
                {
                    // Offline enumeration failed; open the sub-assembly
                    // briefly and retry with its active root name.
                    if (nestedPaths) cvxMemFree((void**)&nestedPaths);
                    if (nestedNames) cvxMemFree((void**)&nestedNames);
                    nestedPaths = nullptr;
                    nestedNames = nullptr;
                    nestedCount = 0;

                    const ZwDocumentContext nestedContext =
                        CaptureActiveContext();
                    const bool nestedAlreadyActive =
                        ActiveDocumentMatchesPath(parent.sourcePath.c_str());

                    if ((nestedAlreadyActive ||
                         cvxFileOpen(parent.sourcePath.c_str()) == 0) &&
                        ActiveDocumentMatchesPath(parent.sourcePath.c_str()))
                    {
                        char nestedRoot[256] = {};
                        cvxRootInqActive(nestedRoot, sizeof(nestedRoot));
                        nestedRet = cvxPartInqCompsInfoByLongPath(
                            parent.sourcePath.c_str(),
                            nestedRoot,
                            &nestedCount,
                            &nestedPaths,
                            &nestedNames);
                        if (!nestedAlreadyActive)
                        {
                            if (!ClosePluginDocument(parent.sourcePath.c_str()))
                            {
                                nestedRet = -1;
                                g_asmOutError = "close-failed";
                            }
                            if (nestedContext.valid)
                                RestoreContext(nestedContext);
                        }
                    }
                    else
                    {
                        nestedRet = -1;
                    }
                }

                if (nestedRet == 0 && nestedPaths && nestedNames)
                {
                    for (int i = 0; i < nestedCount; ++i)
                    {
                        const std::string nestedPath = nestedPaths[i];
                        if (nestedPath.empty() || alreadyVisited(nestedPath))
                            continue;
                        visited.push_back(LowerAscii(nestedPath));
                        appendComponent(nestedNames[i], nestedPath);
                        next.push_back(components.back());
                    }
                    cvxMemFree((void**)&nestedPaths);
                    cvxMemFree((void**)&nestedNames);
                }
                else
                {
                    char message[512] = {};
                    sprintf_s(
                        message,
                        "[AsmParamOut] nested enumeration skipped for %s ret=%d",
                        parent.fileName.c_str(),
                        nestedRet);
                    DisplayMessage(message);
                }
            }

            frontier.swap(next);
        }
    }

    const int initialRestoreResult =
        RestoreContext(context);

    if (initialRestoreResult != 0)
    {
        g_asmOutError = "restore-failed";
        return initialRestoreResult;
    }

    char cntMsg[256] = {};
    sprintf_s(cntMsg, "[AsmParamOut] enumerated %d sub-components",
              static_cast<int>(components.size()));
    DisplayMessage(cntMsg);

    // 2. Create a unique instance directory and copy the assembly plus its
    //    direct sub-components into it (same directory → references resolve).
    const std::string instanceDir =
        CreateUniqueAssemblyInstanceDirectory(
            FileNameWithoutExtension(sourceFile));

    if (!EnsureDirectory(instanceDir.c_str()))
    {
        char m[1024] = {};
        sprintf_s(m, "[AsmParamOut] cannot create instance dir: %s",
                  instanceDir.c_str());
        DisplayMessage(m);
        g_asmOutError = "copy-failed";
        return -1;
    }

    const std::string instanceAsmPath =
        instanceDir + "\\" + sourceFile;
    // Keep file preparation non-interactive. cvxFileSaveAs makes ZW3D switch
    // the edited document from the staging path to the instance path and can
    // emit a warning or retain the downloaded document in the session. A
    // byte-for-byte copy is sufficient; the real root object is queried from
    // the resulting file below, and insertion explicitly prioritizes this
    // instance directory in ZW3D's search path.
    if (!CopyFileA(sourcePath.c_str(), instanceAsmPath.c_str(), TRUE))
    {
        char m[1024] = {};
        sprintf_s(m, "[AsmParamOut] copy assembly failed: %s -> %s",
                  sourcePath.c_str(), instanceAsmPath.c_str());
        DisplayMessage(m);
        g_asmOutError = "copy-failed";
        return -1;
    }

    bool copyFailed = false;

    for (const AsmSubComponent& component : components)
    {
        const std::string destination =
            instanceDir + "\\" + component.fileName;

        // 嵌套层级里同名文件只需落盘一次
        if (FileExists(destination.c_str()) ||
            CopyFileA(
                component.sourcePath.c_str(),
                destination.c_str(),
                TRUE))
        {
            char m[512] = {};
            sprintf_s(m, "[AsmParamOut] copied %s", component.fileName.c_str());
            DisplayMessage(m);
        }
        else
        {
            copyFailed = true;
            char m[512] = {};
            sprintf_s(m, "[AsmParamOut] copy failed: %s",
                      component.fileName.c_str());
            DisplayMessage(m);
        }
    }

    if (copyFailed)
    {
        DisplayMessage(
            "[AsmParamOut] aborted because one or more sub-components "
            "could not be copied.");
        g_asmOutError = "copy-failed";
        return -1;
    }

    // Resolve the actual assembly root from the copied file. The external
    // file name is deliberately unique, while the root commonly keeps its
    // original name (for example "装配001"). Never derive one from the other.
    int instanceRootCount = 0;
    vxRootName* instanceRoots = nullptr;
    const int rootListResult = cvxRootList(
        instanceAsmPath.c_str(), &instanceRootCount, &instanceRoots);
    bool foundAssemblyRoot = false;

    if (rootListResult == 0 && instanceRoots)
    {
        for (int i = 0; i < instanceRootCount; ++i)
        {
            int isAssembly = 0;
            if (cvxRootIsAsm(
                    instanceAsmPath.c_str(), instanceRoots[i], &isAssembly) == 0 &&
                isAssembly)
            {
                sourceRootName = instanceRoots[i];
                foundAssemblyRoot = true;
                break;
            }
        }
        cvxMemFree((void**)&instanceRoots);
    }

    if (!foundAssemblyRoot)
    {
        char m[1024] = {};
        sprintf_s(
            m,
            "[AsmParamOut] copied file has no assembly root: ret=%d count=%d file=%s",
            rootListResult,
            instanceRootCount,
            instanceAsmPath.c_str());
        DisplayMessage(m);
        g_asmOutError = "enum-failed";
        return rootListResult != 0 ? rootListResult : -1;
    }

    cvxPathSearchFirst(instanceDir.c_str());

    // 3. Parse the spec and group edits by component file.  Opening/saving a
    // Z3PRT for every single expression makes ZW3D repeatedly switch active
    // documents (visible as continuous flashing) and turns a 40-parameter
    // assembly into dozens of expensive open/save/close cycles.
    const std::vector<std::string> segments =
        Split(spec, ';');
    int successfulEdits = 0;
    int failedEdits = 0;

    struct PendingExpressionEdit
    {
        std::string name;
        std::string value;
    };

    struct PendingComponentEdits
    {
        std::string fileName;
        std::vector<PendingExpressionEdit> expressions;
    };

    std::vector<PendingComponentEdits> pendingComponents;

    for (const std::string& rawSegment : segments)
    {
        const std::string segment =
            Trim(rawSegment);

        if (segment.empty())
            continue;

        const size_t colon =
            segment.find(':');

        if (colon == std::string::npos)
        {
            char m[512] = {};
            sprintf_s(m, "[AsmParamOut] invalid segment (no ':'): %s",
                      segment.c_str());
            DisplayMessage(m);
            ++failedEdits;
            continue;
        }

        const std::string nameSubstring =
            Trim(segment.substr(0, colon));

        const std::string exprPart =
            Trim(segment.substr(colon + 1));

        const size_t equalSign =
            exprPart.find('=');

        if (equalSign == std::string::npos)
        {
            char m[512] = {};
            sprintf_s(m, "[AsmParamOut] invalid segment (no '='): %s",
                      segment.c_str());
            DisplayMessage(m);
            ++failedEdits;
            continue;
        }

        const std::string exprName =
            Trim(exprPart.substr(0, equalSign));

        const std::string exprValue =
            Trim(exprPart.substr(equalSign + 1));

        if (nameSubstring.empty() ||
            exprName.empty() ||
            exprValue.empty())
        {
            char m[512] = {};
            sprintf_s(m, "[AsmParamOut] empty field in segment: %s",
                      segment.c_str());
            DisplayMessage(m);
            ++failedEdits;
            continue;
        }

        const AsmSubComponent* matched = nullptr;

        for (const AsmSubComponent& component : components)
        {
            if (ComponentNameMatches(component, nameSubstring))
            {
                matched = &component;
                break;
            }
        }

        if (matched == nullptr)
        {
            char m[512] = {};
            sprintf_s(m, "[AsmParamOut] no sub-component matches '%s'",
                      nameSubstring.c_str());
            DisplayMessage(m);
            ++failedEdits;
            continue;
        }

        PendingComponentEdits* pending = nullptr;
        const std::string matchedFileKey =
            LowerAscii(matched->fileName);

        for (PendingComponentEdits& candidate : pendingComponents)
        {
            if (LowerAscii(candidate.fileName) == matchedFileKey)
            {
                pending = &candidate;
                break;
            }
        }

        if (pending == nullptr)
        {
            PendingComponentEdits componentEdits;
            componentEdits.fileName = matched->fileName;
            pendingComponents.push_back(componentEdits);
            pending = &pendingComponents.back();
        }

        PendingExpressionEdit expression;
        expression.name = exprName;
        expression.value = exprValue;
        pending->expressions.push_back(expression);
    }

    for (const PendingComponentEdits& pending : pendingComponents)
    {
        const std::string editedPath =
            instanceDir + "\\" + pending.fileName;
        const ZwDocumentContext editContext = CaptureActiveContext();

        char m[1024] = {};
        sprintf_s(
            m,
            "[AsmParamOut] edit %s: %d expression(s)",
            pending.fileName.c_str(),
            static_cast<int>(pending.expressions.size()));
        DisplayMessage(m);

        if (cvxFileOpen(editedPath.c_str()) != 0 ||
            !ActiveDocumentMatchesPath(editedPath.c_str()))
        {
            sprintf_s(
                m,
                "[AsmParamOut] open sub-component failed or a same-name "
                "document is already open: %s",
                editedPath.c_str());
            DisplayMessage(m);
            failedEdits += static_cast<int>(pending.expressions.size());
            if (editContext.valid)
                RestoreContext(editContext);
            continue;
        }

        int fileSuccessfulEdits = 0;
        int fileFailedEdits = 0;

        for (const PendingExpressionEdit& expression : pending.expressions)
        {
            const int expressionResult = SetCurrentPartExpression(
                expression.name.c_str(),
                expression.value.c_str());

            if (expressionResult == 0)
            {
                ++fileSuccessfulEdits;
            }
            else
            {
                ++fileFailedEdits;
                sprintf_s(
                    m,
                    "[AsmParamOut] expression update failed: file=%s "
                    "expression=%s edit=%d",
                    pending.fileName.c_str(),
                    expression.name.c_str(),
                    expressionResult);
                DisplayMessage(m);
            }
        }

        // Save without closing, then close the exact plugin-owned path.  The
        // old cvxFileSave(1) + cvxFileClose() sequence closed twice: after the
        // copied part closed, the second call targeted the user's assembly.
        const int saveResult = fileSuccessfulEdits > 0
            ? cvxFileSave3(0, 1, 0)
            : 0;
        const bool editedPartClosed =
            ClosePluginDocument(editedPath.c_str());
        if (editContext.valid)
            RestoreContext(editContext);

        if (!editedPartClosed)
        {
            DisplayMessage(
                "[AsmParamOut] edited sub-component remained open.");
            g_asmOutError = "close-failed";
            return -1;
        }

        if (saveResult == 0)
        {
            successfulEdits += fileSuccessfulEdits;
            failedEdits += fileFailedEdits;
        }
        else
        {
            failedEdits += fileSuccessfulEdits + fileFailedEdits;
            sprintf_s(
                m,
                "[AsmParamOut] save sub-component failed: file=%s save=%d",
                pending.fileName.c_str(),
                saveResult);
            DisplayMessage(m);
        }
    }

    // 4. Restore the original context and insert the copied assembly.
    const int restoreResult =
        RestoreContext(context);

    if (restoreResult != 0)
    {
        g_asmOutError = "restore-failed";
        return restoreResult;
    }

    // Parameter mismatches are warnings now: the copied assembly is still
    // inserted, only without the un-matched parameter edits.
    if (hasSpec && successfulEdits == 0 && failedEdits != 0)
    {
        char m[256] = {};
        sprintf_s(
            m,
            "[AsmParamOut] no parameter matched any component "
            "(failed=%d), inserting unedited copy",
            failedEdits);
        DisplayMessage(m);
        g_asmOutError = "no-param-match";
    }
    else if (hasSpec && failedEdits != 0)
    {
        char m[256] = {};
        sprintf_s(
            m,
            "[AsmParamOut] warning: applied %d edit(s), %d failed",
            successfulEdits,
            failedEdits);
        DisplayMessage(m);
        g_asmOutError = "partial-param-match";
    }

    // A remote assembly is deliberately downloaded under a unique file name
    // to avoid ZW3D's session-wide same-name collision.  The root object name
    // inside the file does not change, so use the root queried after opening
    // instead of deriving it from the temporary file name.
    const std::string partName = sourceRootName.empty()
        ? FileNameWithoutExtension(sourceFile)
        : sourceRootName;

    // Match part stock-out behavior: return control to the model viewport and
    // let the user choose the assembly placement. Esc cancels without insert.
    svxPoint insertionPoint = {};
    const int pickResult = PickInsertionPoint(&insertionPoint);
    if (pickResult != 0)
    {
        g_asmOutError = "cancelled";
        return pickResult;
    }

    const int insertRet = asShape
        ? InsertAssemblyShape(
              instanceDir.c_str(),
              sourceFile.c_str(),
              partName.c_str(),
              &insertionPoint)
        : InsertAssemblyComponent(
              instanceDir.c_str(),
              sourceFile.c_str(),
              partName.c_str(),
              0,
              &insertionPoint);

    char done[512] = {};
    sprintf_s(done, "[AsmParamOut] DONE mode=%s ret=%d instance=%s",
              asShape ? "shape" : "component",
              insertRet,
              instanceAsmPath.c_str());
    DisplayMessage(done);

    if (insertRet != 0)
        g_asmOutError = "insert-failed";

    return insertRet;
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

struct StockInDependency
{
    std::string rootName;
    std::string path;
    std::string fileName;
    std::string base64;
};

static bool PickModelFile(
    const char* filter,
    const char* title,
    char* path,
    DWORD pathCapacity)
{
    if (!path || pathCapacity == 0)
        return false;

    path[0] = '\0';
    OPENFILENAMEA dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_hwnd;
    dialog.lpstrFilter = filter;
    dialog.lpstrTitle = title;
    dialog.lpstrFile = path;
    dialog.nMaxFile = pathCapacity;
    dialog.Flags = OFN_FILEMUSTEXIST |
                   OFN_PATHMUSTEXIST |
                   OFN_HIDEREADONLY;
    return GetOpenFileNameA(&dialog) == TRUE;
}

static std::vector<StockInDependency> CollectAssemblyDependencies(
    const char* assemblyPath)
{
    std::vector<StockInDependency> dependencies;
    if (!assemblyPath || !assemblyPath[0])
        return dependencies;

    const ZwDocumentContext originalContext = CaptureActiveContext();
    const bool alreadyActive =
        originalContext.valid &&
        SameLocalFilePath(originalContext.file, assemblyPath);

    if (!alreadyActive && cvxFileOpen(assemblyPath) != 0)
        return dependencies;

    char root[256] = {};
    cvxRootInqActive(root, static_cast<int>(sizeof(root)));

    int count = 0;
    vxLongPath* paths = nullptr;
    vxRootName* names = nullptr;
    const int result = root[0]
        ? cvxPartInqCompsInfoByLongPath(
              assemblyPath, root, &count, &paths, &names)
        : -1;

    if (result == 0 && paths && names)
    {
        for (int index = 0; index < count; ++index)
        {
            bool duplicate = false;
            for (const StockInDependency& existing : dependencies)
            {
                if (_stricmp(existing.path.c_str(), paths[index]) == 0)
                {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate)
                continue;

            const std::vector<unsigned char> bytes =
                ReadBinaryFile(paths[index]);
            if (bytes.empty())
                continue;

            StockInDependency dependency;
            dependency.rootName = names[index];
            dependency.path = paths[index];
            std::string directory;
            SplitFilePath(
                dependency.path,
                &directory,
                &dependency.fileName);
            dependency.base64 = EncodeBase64(bytes);
            dependencies.push_back(dependency);
        }
    }

    if (paths) cvxMemFree(reinterpret_cast<void**>(&paths));
    if (names) cvxMemFree(reinterpret_cast<void**>(&names));

    if (!alreadyActive)
    {
        cvxFileClose();
        if (originalContext.valid)
            RestoreContext(originalContext);
    }

    return dependencies;
}

static bool PostAssemblyStockInResult(const char* assemblyPath)
{
    if (!FileExists(assemblyPath))
        return false;

    const std::vector<unsigned char> modelBytes =
        ReadBinaryFile(assemblyPath);
    if (modelBytes.empty())
        return false;

    std::string directory;
    std::string fileName;
    if (!SplitFilePath(assemblyPath, &directory, &fileName))
        return false;

    std::string basePath = assemblyPath;
    const size_t extension = basePath.find_last_of('.');
    if (extension != std::string::npos)
        basePath.resize(extension);

    const std::string pngPath = basePath + ".png";
    const bool hasPng = FileExists(pngPath.c_str());
    const std::string pngBase64 = hasPng
        ? EncodeBase64(ReadBinaryFile(pngPath.c_str()))
        : "";
    const std::vector<StockInDependency> dependencies =
        CollectAssemblyDependencies(assemblyPath);

    std::string json =
        "{\"action\":\"assemblyStockInReady\","
        "\"success\":true,\"kind\":\"assembly\"";
    auto addText = [&](const char* key, const std::string& value) {
        json += ",\"" + std::string(key) + "\":\"" +
                EscapeJsonString(value) + "\"";
    };
    addText("path", assemblyPath);
    addText("root", FileNameWithoutExtension(fileName));
    addText("modelFileName", fileName);
    json += ",\"modelBase64\":\"";
    json += EncodeBase64(modelBytes);
    json += "\",\"hasPng\":";
    json += hasPng && !pngBase64.empty() ? "true" : "false";

    if (hasPng && !pngBase64.empty())
    {
        addText("thumbFileName", FileNameWithoutExtension(fileName) + ".png");
        json += ",\"pngBase64\":\"" + pngBase64 + "\"";
    }

    // Export a fresh STEP so the front end can upload it to
    // POST /3d-assemblies/{id}/stp for the GLB preview pipeline.
    std::string stepPath;
    if (ExportAssemblyStepFile(assemblyPath, &stepPath))
    {
        const std::string stepBase64 =
            EncodeBase64(ReadBinaryFile(stepPath.c_str()));
        if (!stepBase64.empty())
        {
            addText(
                "stepFileName",
                FileNameWithoutExtension(fileName) + ".stp");
            json += ",\"stepBase64\":\"" + stepBase64 + "\"";
        }
        DeleteFileA(stepPath.c_str());
    }
    else
    {
        DisplayMessage(
            "[AsmStockIn] STEP export failed; stock-in continues "
            "without a preview STEP.");
    }

    json += ",\"dependencies\":[";
    for (size_t index = 0; index < dependencies.size(); ++index)
    {
        if (index > 0)
            json += ',';
        json += "{\"rootName\":\"" +
                EscapeJsonString(dependencies[index].rootName) +
                "\",\"sourcePath\":\"" +
                EscapeJsonString(dependencies[index].path) +
                "\",\"fileName\":\"" +
                EscapeJsonString(dependencies[index].fileName) +
                "\",\"base64\":\"" +
                dependencies[index].base64 + "\"}";
    }
    json += "]}";

    PostWebJson(json);
    return true;
}

// ============================================================
// Check-in: save file, export STP, notify front-end
// ============================================================
static bool DoCheckinAndNotify(
    const char* filePath = nullptr,
    const char* resultAction = "checkinReady")
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
        const int saveRet = cvxFileSave(1);
        char m[512]; sprintf_s(m, "[Checkin] 1.path(active)=%s", z3prtPath); DisplayMessage(m);
        if (saveRet != 0)
        {
            sprintf_s(m, "[Checkin] ABORT: save failed ret=%d", saveRet);
            DisplayMessage(m);
            return false;
        }
    }

    if (z3prtPath[0] == '\0') { DisplayMessage("[Checkin] ABORT: no path"); return false; }

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

    bool stpExists = FileExists(stpPath);
    int stpRet = -1;
    char m2[512]; sprintf_s(m2, "[Checkin] 3.STP exists=%d path=%s", stpExists, stpPath); DisplayMessage(m2);

    // Always regenerate STEP so check-in cannot upload a stale sidecar left
    // by an earlier version of the Z3PRT.  Keep the user's active document
    // open when it is already the file being exported.
    const ZwDocumentContext originalContext =
        CaptureActiveContext();
    const bool exportFromActiveFile =
        originalContext.valid &&
        SameLocalFilePath(originalContext.file, z3prtPath);
    bool openedForExport = false;

    if (!exportFromActiveFile)
    {
        DisplayMessage("[Checkin] 4.cvxFileOpen...");
        int openRet = cvxFileOpen(z3prtPath);
        char m3[128]; sprintf_s(m3, "[Checkin] 4.open ret=%d", openRet); DisplayMessage(m3);
        openedForExport = openRet == 0;
    }

    if (exportFromActiveFile || openedForExport)
    {
        svxSTEPData sd = {};
        const int initRet =
            cvxFileExportInit(VX_EXPORT_TYPE_STEP, 0, &sd);

        if (initRet == 0)
        {
            sd.AppProtocol = 2; sd.OutPut = 0;
            DisplayMessage("[Checkin] 5.exporting STP...");
            stpRet = cvxFileExport(VX_EXPORT_TYPE_STEP, stpPath, &sd);
        }
        else
        {
            stpRet = initRet;
        }

        char m4[128]; sprintf_s(m4, "[Checkin] 5.export ret=%d", stpRet); DisplayMessage(m4);

        if (openedForExport)
        {
            DisplayMessage("[Checkin] 6.cvxFileClose...");
            cvxFileClose();
            DisplayMessage("[Checkin] 6.closed");
        }
    }

    if (originalContext.valid && !exportFromActiveFile)
        RestoreContext(originalContext);

    stpExists = stpRet == 0 && FileExists(stpPath);
    if (!stpExists)
    {
        DisplayMessage(
            "[Checkin] ABORT: the current STEP file could not be exported.");
        return false;
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

    const std::string z3B = EncodeBase64(ReadBinaryFile(z3prtPath));
    const std::string stB = EncodeBase64(ReadBinaryFile(stpPath));
    const std::string pnB = hasP ? EncodeBase64(ReadBinaryFile(pngPath)) : "";
    const std::string zlB = hasZ ? EncodeBase64(ReadBinaryFile(z3lPath)) : "";
    const std::string xlB = hasX ? EncodeBase64(ReadBinaryFile(xlsxPath)) : "";

    if (z3B.empty())
    {
        DisplayMessage("[Checkin] ABORT: cannot read the selected Z3PRT.");
        return false;
    }

    char m6[512]; sprintf_s(m6, "[Checkin] 8.files z3prt=%d stp=%d png=%d z3l=%d xlsx=%d",
        (int)z3B.size(), (int)stB.size(), (int)pnB.size(), (int)zlB.size(), (int)xlB.size());
    DisplayMessage(m6);

    std::string modelDirectory;
    std::string modelFileName;
    SplitFilePath(z3prtPath, &modelDirectory, &modelFileName);
    std::string stepDirectory;
    std::string stepFileName;
    SplitFilePath(stpPath, &stepDirectory, &stepFileName);

    std::string json = "{\"action\":\"";
    json += EscapeJsonString(resultAction ? resultAction : "checkinReady");
    json += "\",\"success\":true,\"kind\":\"part\"";
    auto addText = [&](const char* key, const std::string& value) {
        json += ",\"" + std::string(key) + "\":\"" +
                EscapeJsonString(value) + "\"";
    };
    auto addBase64 = [&](const char* key, const std::string& value) {
        json += ",\"" + std::string(key) + "\":\"" + value + "\"";
    };
    addText("path", z3prtPath);
    addText("root", rootName);
    addText("modelFileName", modelFileName);
    addText("stepFileName", stepFileName);
    addBase64("modelBase64", z3B);
    addBase64("stepBase64", stB);
    if (_stricmp(resultAction ? resultAction : "", "checkinReady") == 0)
    {
        addBase64("z3prtBase64", z3B);
        addBase64("stpBase64", stB);
    }
    json += ",\"hasPng\":" + std::string(hasP ? "true" : "false");
    json += ",\"hasZ3l\":" + std::string(hasZ ? "true" : "false");
    json += ",\"hasExcel\":" + std::string(hasX ? "true" : "false");
    if (hasP) { addText("thumbFileName", FileNameWithoutExtension(pngPath) + ".png"); addBase64("pngBase64", pnB); }
    if (hasZ) { addText("configFileName", FileNameWithoutExtension(z3lPath) + ".z3l"); addBase64("z3lBase64", zlB); }
    if (hasX) { addText("dataFileName", FileNameWithoutExtension(xlsxPath) + ".xlsx"); addBase64("xlsxBase64", xlB); }
    json += "}";

    char m7[256]; sprintf_s(m7, "[Checkin] 9.JSON %d bytes webview=%p", (int)json.size(), (void*)g_webview.Get()); DisplayMessage(m7);

    PostWebJson(json);
    DisplayMessage("[Checkin] === DONE ===");
    return true;
}

// Toolbar button wrapper
void OnCheckin()
{
    DisplayMessage("[Checkin] Preparing part for upload...");
    (void)DoCheckinAndNotify();
}

// ============================================================
// Assembly stock-out from a remote URL
// ============================================================
//
// The library stores .Z3ASM/.Z3PRT objects in MinIO. For stock-out the
// front end sends presigned URLs: the assembly itself plus a manifest of
// its component files. Everything is downloaded into one staging directory
// so ZW3D same-directory reference resolution works, then the regular
// AsmParametricWorkflow runs against the local copy.

struct AsmManifestEntry
{
    std::string fileName;
    std::string url;
};

static std::string ExtractJsonStringField(
    const std::string& object,
    const char* key)
{
    const std::string needle = std::string("\"") + key + "\"";
    size_t position = object.find(needle);
    if (position == std::string::npos)
        return "";

    position = object.find(':', position + needle.size());
    if (position == std::string::npos)
        return "";
    ++position;

    while (position < object.size() &&
           isspace(static_cast<unsigned char>(object[position])))
    {
        ++position;
    }

    if (position >= object.size() || object[position] != '"')
        return "";
    ++position;

    std::string value;
    for (; position < object.size() && object[position] != '"'; ++position)
    {
        if (object[position] == '\\' && position + 1 < object.size())
        {
            const char next = object[position + 1];
            if (next == '"' || next == '\\' || next == '/')
            {
                value.push_back(next);
                ++position;
            }
            else if (next == 'n')
            {
                value.push_back('\n');
                ++position;
            }
            else if (next == 't')
            {
                value.push_back('\t');
                ++position;
            }
            else
            {
                value.push_back(object[position]);
            }
        }
        else
        {
            value.push_back(object[position]);
        }
    }

    return value;
}

static std::vector<AsmManifestEntry> ParseAsmManifest(
    const std::string& manifest)
{
    std::vector<AsmManifestEntry> entries;

    size_t objectStart = 0;
    while ((objectStart = manifest.find('{', objectStart)) !=
           std::string::npos)
    {
        const size_t objectEnd = manifest.find('}', objectStart);
        if (objectEnd == std::string::npos)
            break;

        const std::string object =
            manifest.substr(objectStart, objectEnd - objectStart + 1);

        AsmManifestEntry entry;
        entry.fileName = ExtractJsonStringField(object, "fileName");
        entry.url = ExtractJsonStringField(object, "url");

        if (!entry.fileName.empty() && !entry.url.empty())
        {
            // Never allow path traversal from the manifest file names.
            for (char& character : entry.fileName)
            {
                if (character == '/' || character == '\\' ||
                    character == ':' || character == '<' ||
                    character == '>' || character == '"' ||
                    character == '|' || character == '?' ||
                    character == '*')
                {
                    character = '_';
                }
            }
            entries.push_back(entry);
        }

        objectStart = objectEnd + 1;
    }

    return entries;
}

// Downloads url into stagingDirectory\fileName using DownloadFile's temp
// location, then moves it to the exact target name required for ZW3D
// same-directory reference resolution.
static bool DownloadToExactPath(
    const char* url,
    const std::string& targetPath,
    const char* bearerToken)
{
    char tempPath[600] = {};
    if (!DownloadFile(url, tempPath, sizeof(tempPath), bearerToken))
        return false;

    if (!MoveFileExA(
            tempPath,
            targetPath.c_str(),
            MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileA(tempPath);
        return false;
    }

    return true;
}

static int AsmParametricUrlWorkflow(
    const char* url,
    const char* spec,
    int asShape,
    const char* bearerToken,
    const char* manifestJson)
{
    if (url == nullptr || url[0] == '\0')
    {
        DisplayMessage("[AsmParamOut] empty assembly url.");
        return -1;
    }

    const std::string originalAssemblyFileName = UrlFileName(url);

    const size_t extensionPosition =
        originalAssemblyFileName.find_last_of('.');
    const std::string assemblyExtension =
        extensionPosition == std::string::npos
            ? ".Z3ASM"
            : originalAssemblyFileName.substr(extensionPosition);

    char uniqueFileName[256] = {};
    sprintf_s(
        uniqueFileName,
        "MyFirstPlugin_AsmStockOut_%lu_%llu%s",
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long long>(GetTickCount64()),
        assemblyExtension.c_str());
    const std::string assemblyFileName = uniqueFileName;

    const std::string stagingDirectory =
        CreateUniqueAssemblyInstanceDirectory(
            "download_" +
            SanitizeFileToken(
                FileNameWithoutExtension(originalAssemblyFileName)));

    if (stagingDirectory.empty() ||
        !EnsureDirectory(stagingDirectory.c_str()))
    {
        DisplayMessage("[AsmParamOut] cannot create staging directory.");
        return -1;
    }

    std::vector<std::string> stagedFiles;
    bool failed = false;

    const std::string assemblyPath =
        stagingDirectory + "\\" + assemblyFileName;

    if (!DownloadToExactPath(url, assemblyPath, bearerToken))
    {
        DisplayMessage("[AsmParamOut] assembly download failed.");
        failed = true;
    }
    else
    {
        stagedFiles.push_back(assemblyPath);
    }

    if (!failed)
    {
        const std::vector<AsmManifestEntry> manifest =
            ParseAsmManifest(manifestJson ? manifestJson : "");

        char countMessage[256] = {};
        sprintf_s(
            countMessage,
            "[AsmParamOut] manifest entries: %d",
            static_cast<int>(manifest.size()));
        DisplayMessage(countMessage);

        for (const AsmManifestEntry& entry : manifest)
        {
            const std::string targetPath =
                stagingDirectory + "\\" + entry.fileName;

            if (!DownloadToExactPath(
                    entry.url.c_str(), targetPath, bearerToken))
            {
                char message[1024] = {};
                sprintf_s(
                    message,
                    "[AsmParamOut] component download failed: %s",
                    entry.fileName.c_str());
                DisplayMessage(message);
                failed = true;
                break;
            }

            stagedFiles.push_back(targetPath);
        }
    }

    int result = -1;

    if (!failed)
    {
        const std::string sourceRootHint =
            FileNameWithoutExtension(originalAssemblyFileName);
        result = AsmParametricWorkflow(
            assemblyPath.c_str(), spec, asShape, sourceRootHint.c_str());
    }

    for (const std::string& stagedFile : stagedFiles)
        DeleteFileA(stagedFile.c_str());
    RemoveDirectoryA(stagingDirectory.c_str());

    return result;
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
            "previewZ3prt") == 0)
    {
        const std::string requestId =
            fields.size() >= 2 ? Trim(fields[1]) : "preview";
        const std::string path =
            fields.size() >= 3 ? Trim(fields[2]) : "";
        const std::string bearerToken =
            fields.size() >= 4 ? Trim(fields[3]) : "";

        ExtractZ3prtPreview(
            requestId,
            path,
            bearerToken);
        return;
    }

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

        const std::string bearerToken =
            fields.size() >= 4 ? Trim(fields[3]) : "";

        // Download the file first (if URL)
        char localPath[600] = {};
        const char* filePath = path.c_str();
        bool downloadedSource = false;
        if (strncmp(filePath, "http://", 7) == 0 ||
            strncmp(filePath, "https://", 8) == 0)
        {
            if (DownloadFile(
                    filePath,
                    localPath,
                    sizeof(localPath),
                    bearerToken.c_str()))
            {
                filePath = localPath;
                downloadedSource = true;
            }
            else
                return;
        }

        // Copy → edit expressions → save → close → insert
        const int exportResult = ParametricImportWorkflow(
            filePath,
            assignments.c_str(),
            asShape ? 1 : 0,
            bearerToken.c_str());

        // The downloaded Z3PRT is only an input cache.  The workflow has
        // already created its parameter-specific instance copy by this point.
        if (downloadedSource)
            DeleteFileA(localPath);

        if (exportResult != 0)
            DisplayMessage("[PartOut] Export failed.");

        PostPartExportResult(asShape, exportResult);

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

    if (_stricmp(action.c_str(), "pickPartForStockIn") == 0)
    {
        char pathBuf[1024] = {};
        if (!PickModelFile(
                "ZW3D Part (*.Z3PRT)\0*.Z3PRT\0\0",
                "Select a ZW3D part for stock-in",
                pathBuf,
                static_cast<DWORD>(sizeof(pathBuf))))
        {
            PostStockInFailure(
                "partStockInReady",
                "Part selection was cancelled",
                true);
            return;
        }

        if (!DoCheckinAndNotify(pathBuf, "partStockInReady"))
        {
            PostStockInFailure(
                "partStockInReady",
                "Unable to prepare the selected Z3PRT",
                false);
        }
        return;
    }

    if (_stricmp(action.c_str(), "pickAssemblyForStockIn") == 0)
    {
        char pathBuf[1024] = {};
        if (!PickModelFile(
                "ZW3D Assembly (*.Z3ASM)\0*.Z3ASM\0\0",
                "Select a ZW3D assembly for stock-in",
                pathBuf,
                static_cast<DWORD>(sizeof(pathBuf))))
        {
            PostStockInFailure(
                "assemblyStockInReady",
                "Assembly selection was cancelled",
                true);
            return;
        }

        if (!PostAssemblyStockInResult(pathBuf))
        {
            PostStockInFailure(
                "assemblyStockInReady",
                "Unable to prepare the selected Z3ASM",
                false);
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
        bool downloadedSource = false;
        if (strncmp(fPath, "http://", 7) == 0 || strncmp(fPath, "https://", 8) == 0)
        {
            if (DownloadFile(fPath, localPath, sizeof(localPath)))
            {
                fPath = localPath;
                downloadedSource = true;
            }
            else
                return;
        }

        // Import STP as new shape into current part
        svxImportData imp = {};
        imp.type = VX_IMPORT_TYPE_STEP;
        strcpy_s(imp.filePath, fPath);
        imp.importTo = 0;  // current object
        int ret = cvxFileImport(&imp);
        if (downloadedSource)
            DeleteFileA(localPath);
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

    if (_stricmp(action.c_str(), "asmOpen") == 0)
    {
        const std::string path =
            fields.size() >= 2 ? Trim(fields[1]) : "";
        AsmOpenWorkflow(path.c_str());
        return;
    }

    if (_stricmp(action.c_str(), "asmImport") == 0 ||
        _stricmp(action.c_str(), "asmImportRef") == 0)
    {
        const bool asReference =
            (_stricmp(action.c_str(), "asmImportRef") == 0);

        const std::string path =
            fields.size() >= 2 ? Trim(fields[1]) : "";

        AsmComponentInsertWorkflow(
            path.c_str(),
            asReference ? 0 : 1);
        return;
    }

    if (_stricmp(action.c_str(), "asmShape") == 0)
    {
        const std::string path =
            fields.size() >= 2 ? Trim(fields[1]) : "";
        AsmShapeInsertWorkflow(path.c_str());
        return;
    }

    if (_stricmp(action.c_str(), "asmParametricImport") == 0 ||
        _stricmp(action.c_str(), "asmParametricShape") == 0)
    {
        const bool asShape =
            (_stricmp(action.c_str(), "asmParametricShape") == 0);

        const std::string path =
            fields.size() >= 2 ? Trim(fields[1]) : "";

        const std::string spec =
            fields.size() >= 3 ? Trim(fields[2]) : "";

        const std::string bearerToken =
            fields.size() >= 4 ? Trim(fields[3]) : "";

        const std::string manifest =
            fields.size() >= 5 ? Trim(fields[4]) : "";

        const bool isUrl =
            path.compare(0, 7, "http://") == 0 ||
            path.compare(0, 8, "https://") == 0;

        g_asmOutError.clear();

        const int exportResult = isUrl
            ? AsmParametricUrlWorkflow(
                  path.c_str(),
                  spec.c_str(),
                  asShape ? 1 : 0,
                  bearerToken.c_str(),
                  manifest.c_str())
            : AsmParametricWorkflow(
                  path.c_str(),
                  spec.c_str(),
                  asShape ? 1 : 0);

        PostAsmExportResult(asShape, exportResult, g_asmOutError);
        return;
    }

    if (_stricmp(action.c_str(), "asmExportStep") == 0)
    {
        // asmExportStep|requestId|pathOrUrl|token — export the assembly as
        // STEP and return it base64-encoded so the front end can upload it
        // to POST /3d-assemblies/{id}/stp for GLB preview conversion.
        const std::string requestId =
            fields.size() >= 2 && !Trim(fields[1]).empty()
                ? Trim(fields[1])
                : "asmstep";

        std::string source =
            fields.size() >= 3 ? Trim(fields[2]) : "";

        const std::string bearerToken =
            fields.size() >= 4 ? Trim(fields[3]) : "";

        if (source.empty())
        {
            // Fall back to the active ZW3D document.
            char activeFile[600] = {};
            cvxFileInqActive(activeFile, sizeof(activeFile));
            source = activeFile;
        }

        if (source.empty())
        {
            PostAsmStepExportResult(
                requestId, "", "",
                "No assembly specified and no active document");
            return;
        }

        const bool isUrl =
            source.compare(0, 7, "http://") == 0 ||
            source.compare(0, 8, "https://") == 0;

        std::string localAssemblyPath = source;
        bool downloadedSource = false;

        if (isUrl)
        {
            char tempPath[600] = {};
            if (!DownloadFile(
                    source.c_str(),
                    tempPath,
                    sizeof(tempPath),
                    bearerToken.c_str()))
            {
                PostAsmStepExportResult(
                    requestId, "", "",
                    "Assembly model download failed");
                return;
            }
            localAssemblyPath = tempPath;
            downloadedSource = true;
        }

        const std::string assemblyFileName = isUrl
            ? UrlFileName(source)
            : localAssemblyPath;

        std::string stepPath;
        const bool exported =
            ExportAssemblyStepFile(localAssemblyPath.c_str(), &stepPath);

        if (downloadedSource)
            DeleteFileA(localAssemblyPath.c_str());

        if (!exported)
        {
            PostAsmStepExportResult(
                requestId, "", "",
                "STEP export failed, check the ZW3D command line log");
            return;
        }

        const std::string stepBase64 =
            EncodeBase64(ReadBinaryFile(stepPath.c_str()));
        DeleteFileA(stepPath.c_str());

        if (stepBase64.empty())
        {
            PostAsmStepExportResult(
                requestId, "", "", "Exported STEP file is empty");
            return;
        }

        PostAsmStepExportResult(
            requestId,
            FileNameWithoutExtension(assemblyFileName) + ".stp",
            stepBase64,
            nullptr);
        return;
    }

    if (_stricmp(action.c_str(), "readPartParams") == 0)
    {
        // readPartParams|requestId|pathOrUrl|token — opens a part and
        // returns its expression list. The front end falls back to this
        // when the part library has no specs (e.g. directory-imported
        // parts without an xlsx companion).
        const std::string requestId =
            fields.size() >= 2 && !Trim(fields[1]).empty()
                ? Trim(fields[1])
                : "partparams";

        std::string source =
            fields.size() >= 3 ? Trim(fields[2]) : "";

        const std::string bearerToken =
            fields.size() >= 4 ? Trim(fields[3]) : "";

        if (source.empty())
        {
            char activeFile[600] = {};
            cvxFileInqActive(activeFile, sizeof(activeFile));
            source = activeFile;
        }

        if (source.empty())
        {
            PostPartParamsResult(
                requestId, "", nullptr,
                "No part specified and no active document");
            return;
        }

        const bool isUrl =
            source.compare(0, 7, "http://") == 0 ||
            source.compare(0, 8, "https://") == 0;

        std::string localPartPath = source;
        bool downloadedSource = false;

        if (isUrl)
        {
            char tempPath[600] = {};
            if (!DownloadFile(
                    source.c_str(),
                    tempPath,
                    sizeof(tempPath),
                    bearerToken.c_str()))
            {
                PostPartParamsResult(
                    requestId, "", nullptr,
                    "Part model download failed");
                return;
            }
            localPartPath = tempPath;
            downloadedSource = true;
        }

        if (!FileExists(localPartPath.c_str()))
        {
            if (downloadedSource)
                DeleteFileA(localPartPath.c_str());
            PostPartParamsResult(
                requestId, "", nullptr, "Part file not found");
            return;
        }

        const ZwDocumentContext originalContext = CaptureActiveContext();
        const bool alreadyActive =
            originalContext.valid &&
            SameLocalFilePath(originalContext.file, localPartPath.c_str());
        bool opened = alreadyActive;

        if (!alreadyActive)
            opened = cvxFileOpen(localPartPath.c_str()) == 0;

        if (!opened)
        {
            if (originalContext.valid && !alreadyActive)
                RestoreContext(originalContext);
            if (downloadedSource)
                DeleteFileA(localPartPath.c_str());
            PostPartParamsResult(
                requestId, "", nullptr, "Unable to open the part file");
            return;
        }

        char root[256] = {};
        cvxRootInqActive(root, sizeof(root));
        if (root[0] == '\0')
        {
            strcpy_s(
                root,
                FileNameWithoutExtension(localPartPath).c_str());
        }

        int variableCount = 0;
        svxVariable* variables = nullptr;
        const int inquiryResult = cvxPartInqVars(
            localPartPath.c_str(), root, &variableCount, &variables);

        std::vector<svxVariable> collected;
        if (inquiryResult == 0 && variables != nullptr)
        {
            for (int index = 0; index < variableCount; ++index)
            {
                if (variables[index].Name[0] != '\0')
                    collected.push_back(variables[index]);
            }
            cvxMemFree(reinterpret_cast<void**>(&variables));
        }

        if (!alreadyActive)
            ClosePluginDocument(localPartPath.c_str());
        if (originalContext.valid && !alreadyActive)
            RestoreContext(originalContext);
        if (downloadedSource)
            DeleteFileA(localPartPath.c_str());

        if (inquiryResult != 0)
        {
            char message[256] = {};
            sprintf_s(
                message,
                "cvxPartInqVars failed: ret=%d",
                inquiryResult);
            PostPartParamsResult(requestId, root, nullptr, message);
            return;
        }

        PostPartParamsResult(requestId, root, &collected, nullptr);
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
        36,
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
        36,
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
        36,
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
        36,
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
        36,
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
        36,
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
        36,
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
        36,
        hwnd,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(
                BTN_BEARING_SHAPE3)),
        hInst,
        nullptr);

    CreateWindowExW(0, L"BUTTON", L"Check In",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        1182, 6, 100, 36,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BTN_CHECKIN)),
        hInst, nullptr);

    // --- Second row: assembly buttons ---
    CreateWindowExW(0, L"BUTTON", L"Asm NewFile",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        8,  46, 130, 36,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BTN_ASM_NEWFILE)),
        hInst, nullptr);

    CreateWindowExW(0, L"BUTTON", L"Asm Shape",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        146, 46, 110, 36,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BTN_ASM_SHAPE)),
        hInst, nullptr);

    CreateWindowExW(0, L"BUTTON", L"Asm Parametric",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        264, 46, 140, 36,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BTN_ASM_PARAMETRIC)),
        hInst, nullptr);

    CreateWindowExW(0, L"BUTTON", L"AsmPmt Shape",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        412, 46, 130, 36,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BTN_ASM_PARAMETRIC_SHAPE)),
        hInst, nullptr);

    CreateWindowExW(0, L"BUTTON", L"Asm Open",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        550, 46, 100, 36,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BTN_ASM_OPEN)),
        hInst, nullptr);

    CreateWindowExW(0, L"BUTTON", L"AsmParam2",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        658, 46, 120, 36,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BTN_ASM_PARAMETRIC2)),
        hInst, nullptr);

    CreateWindowExW(0, L"BUTTON", L"Open Takeout",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        786, 46, 120, 36,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BTN_TAKEOUT_OPEN)),
        hInst, nullptr);

    CreateWindowExW(0, L"BUTTON", L"Insert Takeout",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        914, 46, 130, 36,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BTN_TAKEOUT_INSERT)),
        hInst, nullptr);

    CreateWindowExW(0, L"BUTTON", L"Takeout Shape",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        1052, 46, 130, 36,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BTN_TAKEOUT_SHAPE)),
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

        case BTN_ASM_PARAMETRIC_SHAPE:
            OnAsmParametricShape();
            return 0;

        case BTN_ASM_PARAMETRIC2:
            OnAsmParametric2();
            return 0;

        case BTN_TAKEOUT_OPEN:
            OnTakeoutOpen();
            return 0;

        case BTN_TAKEOUT_INSERT:
            OnTakeoutInsert();
            return 0;

        case BTN_TAKEOUT_SHAPE:
            OnTakeoutShape();
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
                                            L"previewZ3prt:function(path,requestId,token){"
                                            L"window.chrome.webview.postMessage("
                                            L"'previewZ3prt|'+(requestId||'preview')+'|'+"
                                            L"(path||'')+'|'+(token||''));"
                                            L"},"
                                            L"directImport:function(path){"
                                            L"window.chrome.webview.postMessage("
                                            L"'directImport|'+(path||''));"
                                            L"},"
                                            L"parametricImport:function(path,params,token){"
                                            L"window.chrome.webview.postMessage("
                                            L"'parametricImport|'+(path||'')+'|'+"
                                            L"(params||'')+'|'+(token||''));"
                                            L"},"
                                            L"parametricShape:function(path,params,token){"
                                            L"window.chrome.webview.postMessage("
                                            L"'parametricShape|'+(path||'')+'|'+"
                                            L"(params||'')+'|'+(token||''));"
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
                                            L"pickPartForStockIn:function(){"
                                            L"window.chrome.webview.postMessage("
                                            L"'pickPartForStockIn');"
                                            L"},"
                                            L"pickAssemblyForStockIn:function(){"
                                            L"window.chrome.webview.postMessage("
                                            L"'pickAssemblyForStockIn');"
                                            L"},"
                                            L"asmOpen:function(path){"
                                            L"window.chrome.webview.postMessage("
                                            L"'asmOpen|'+(path||''));"
                                            L"},"
                                            L"asmImport:function(path){"
                                            L"window.chrome.webview.postMessage("
                                            L"'asmImport|'+(path||''));"
                                            L"},"
                                            L"asmImportRef:function(path){"
                                            L"window.chrome.webview.postMessage("
                                            L"'asmImportRef|'+(path||''));"
                                            L"},"
                                            L"asmShape:function(path){"
                                            L"window.chrome.webview.postMessage("
                                            L"'asmShape|'+(path||''));"
                                            L"},"
                                            L"asmParametricImport:function(path,spec,token,manifest){"
                                            L"window.chrome.webview.postMessage("
                                            L"'asmParametricImport|'+(path||'')+'|'+"
                                            L"(spec||'')+'|'+(token||'')+'|'+"
                                            L"(manifest||''));"
                                            L"},"
                                            L"asmParametricShape:function(path,spec,token,manifest){"
                                            L"window.chrome.webview.postMessage("
                                            L"'asmParametricShape|'+(path||'')+'|'+"
                                            L"(spec||'')+'|'+(token||'')+'|'+"
                                            L"(manifest||''));"
                                            L"},"
                                            L"asmExportStep:function(requestId,path,token){"
                                            L"window.chrome.webview.postMessage("
                                            L"'asmExportStep|'+(requestId||'')+'|'+"
                                            L"(path||'')+'|'+(token||''));"
                                            L"},"
                                            L"readPartParams:function(requestId,path,token){"
                                            L"window.chrome.webview.postMessage("
                                            L"'readPartParams|'+(requestId||'')+'|'+"
                                            L"(path||'')+'|'+(token||''));"
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
