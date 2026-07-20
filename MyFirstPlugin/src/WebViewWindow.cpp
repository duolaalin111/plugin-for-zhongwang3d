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

#include "WebViewWindow.h"
#include "..\inc\MyFirstPluginPr.h"

#include "zwapi_cmd_assembly.h"
#include "zwapi_component.h"
#include "zwapi_matrix.h"
#include "zwapi_file.h"
#include "zwapi_root.h"
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
static const char TEST_SOURCE_PART[] =
    "C:\\Users\\zxcvb\\Documents\\ZW3D\\testparts\\testBearing.Z3PRT";

// Parameterized instances are stored here and must remain available,
// because the assembly component externally references these files.
static const char TEST_INSTANCE_DIRECTORY[] =
    "C:\\Users\\zxcvb\\Documents\\ZW3D\\testparts\\instances";

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

    return failedCount == 0
        ? 0
        : -1;
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

    const ZwDocumentContext assemblyContext =
        CaptureActiveContext();

    if (!assemblyContext.valid)
        return -1;

    if (!EnsureDirectory(
            TEST_INSTANCE_DIRECTORY))
    {
        char message[1024] = {};

        sprintf_s(
            message,
            "[PartOut] Cannot create instance directory: %s, "
            "Windows error=%lu",
            TEST_INSTANCE_DIRECTORY,
            static_cast<unsigned long>(
                GetLastError()));

        DisplayMessage(message);
        return -1;
    }

    const std::string instancePath =
        CreateUniqueInstancePath(
            sourcePartPath,
            assignmentText);

    if (!CopyFileA(
            sourcePartPath,
            instancePath.c_str(),
            TRUE))
    {
        char message[1536] = {};

        sprintf_s(
            message,
            "[PartOut] CopyFile failed: source=%s, "
            "destination=%s, Windows error=%lu",
            sourcePartPath,
            instancePath.c_str(),
            static_cast<unsigned long>(
                GetLastError()));

        DisplayMessage(message);
        return -1;
    }

    char copiedPartRoot[256] = {};

    int ret =
        OpenPartAndGetActiveRoot(
            instancePath.c_str(),
            copiedPartRoot,
            static_cast<int>(
                sizeof(copiedPartRoot)));

    if (ret != 0)
    {
        RestoreContext(
            assemblyContext);

        DeleteFileA(
            instancePath.c_str());

        return ret;
    }

    ret =
        ApplyExpressionAssignments(
            assignmentText);

    if (ret != 0)
    {
        DisplayMessage(
            "[PartOut] Expression update failed. "
            "The component will not be inserted.");

        // Close the failed instance without intentionally saving it.
        cvxFileClose();

        RestoreContext(
            assemblyContext);

        // Deletion can fail if ZW3D still holds the file; leave a log.
        if (!DeleteFileA(
                instancePath.c_str()))
        {
            char message[1024] = {};

            sprintf_s(
                message,
                "[PartOut] Failed instance was not deleted: "
                "%s, Windows error=%lu",
                instancePath.c_str(),
                static_cast<unsigned long>(
                    GetLastError()));

            DisplayMessage(message);
        }

        return ret;
    }

    // Save the active copied Z3PRT and close it.
    ret =
        cvxFileSave(1);

    if (ret != 0)
    {
        char message[1024] = {};

        sprintf_s(
            message,
            "[PartOut] cvxFileSave(1) failed: "
            "ret=%d, file=%s",
            ret,
            instancePath.c_str());

        DisplayMessage(message);

        RestoreContext(
            assemblyContext);

        return ret;
    }

    ret =
        RestoreContext(
            assemblyContext);

    if (ret != 0)
        return ret;

    if (asShape)
    {
        // Shape insertion: use InsertPartWithParams with asShape=1
        std::string dir, file;
        SplitFilePath(instancePath, &dir, &file);
        std::string partName = FileNameWithoutExtension(file);

        ret = InsertPartWithParams(
            dir.c_str(),
            file.c_str(),
            partName.c_str(),
            "",   // params already applied via expressions
            1,    // asShape
            0);   // current root
    }
    else
    {
        ret =
            InsertComponentFromFile(
                instancePath.c_str(),
                copiedPartRoot);
    }

    if (ret == 0)
    {
        char message[1536] = {};

        sprintf_s(
            message,
            "[PartOut] Parameterized import completed. "
            "Instance file=%s, mode=%s",
            instancePath.c_str(),
            asShape ? "shape" : "component");

        DisplayMessage(message);
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
        "C:\\Users\\zxcvb\\Documents\\ZW3D\\testparts",
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

void OnImportParametric()
{
    DisplayMessage(
        "[PartOut] Test button: parameterized import.");

    ParametricImportWorkflow(
        TEST_SOURCE_PART,
        TEST_EXPRESSION_ASSIGNMENTS);
}

void OnParametricShape()
{
    DisplayMessage(
        "[PartOut] Parametric Import 2: shape insertion.");

    // Run the same file-copy + expression-edit workflow,
    // but insert as shape instead of component at the end.
    ParametricImportWorkflow(
        TEST_SOURCE_PART,
        TEST_EXPRESSION_ASSIGNMENTS,
        1);  // asShape=1
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
            "parametricImport") == 0)
    {
        const std::string path =
            fields.size() >= 2 &&
            !Trim(fields[1]).empty()
                ? Trim(fields[1])
                : TEST_SOURCE_PART;

        const std::string assignments =
            fields.size() >= 3 &&
            !Trim(fields[2]).empty()
                ? Trim(fields[2])
                : TEST_EXPRESSION_ASSIGNMENTS;

        ParametricImportWorkflow(
            path.c_str(),
            assignments.c_str());

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
                                            L"}"
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
