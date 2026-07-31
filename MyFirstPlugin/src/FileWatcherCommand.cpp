// FileWatcherCommand.cpp
// Backend task processor: watch task_input\ for {name}.Z3PRT + {name}.json
// pairs. Open the Z3PRT, apply expression params from JSON, export STP,
// write result JSON to task_output\.
//
// Task JSON format (placed alongside .Z3PRT):
// {
//   "taskId": "xxx",
//   "partId": 15,
//   "specId": 101,
//   "params": {"Diameter":"10","Length":"30"},
//   "outputFileName": "Leader Pin-10x30.stp"
// }
//
// Result JSON (written to task_output\):
// {
//   "taskId": "xxx",
//   "success": true,
//   "stpPath": "D:\\...\\task_output\\Leader Pin-10x30.stp",
//   "error": ""
// }

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <vector>
#include <string>

#include "zwapi_cmd.h"
#include "zwapi_message.h"
#include "zwapi_memory.h"
#include "zwapi_file.h"
#include "zwapi_root.h"
#include "zwapi_part_var.h"
#include "..\inc\MyFirstPluginPr.h"

// From PartExpressionCommand_Fixed.cpp
extern int SetCurrentPartExpression(const char* name, const char* expr);

// ============================================================
// Paths (adjust for backend deployment)
// ============================================================
static const char INPUT_DIR[]  = "D:\\ZW3D_Plugins\\task_input";
static const char OUTPUT_DIR[] = "D:\\ZW3D_Plugins\\task_output";
static const char ARCHIVE_DIR[] = "D:\\ZW3D_Plugins\\task_done";
static const int  POLL_INTERVAL_MS = 3000;

static bool g_watching  = false;
static bool g_stopFlag  = false;

static int ProcessAllTasks();  // forward

// ============================================================
// Tiny helpers
// ============================================================
static void Log(const char* text)
{
    if (text) { cvxMsgDisp(text); OutputDebugStringA(text); OutputDebugStringA("\n"); }
}

static bool DirExists(const char* p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static void EnsureDir(const char* p) { if (!DirExists(p)) CreateDirectoryA(p, nullptr); }

static std::string SlurpFile(const char* path)
{
    std::string out;
    FILE* f = fopen(path, "rb");
    if (!f) return out;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz > 0) { out.resize(sz); fread(&out[0], 1, sz, f); }
    fclose(f);
    return out;
}

static void WriteFile(const char* path, const std::string& content)
{
    FILE* f = fopen(path, "wb");
    if (f) { fwrite(content.data(), 1, content.size(), f); fclose(f); }
}

static std::string StripExt(const char* path)
{
    const char* dot = strrchr(path, '.');
    return dot ? std::string(path, dot - path) : std::string(path);
}

// ============================================================
// Crude JSON extractor (no library dependency)
// ============================================================
static std::string JsonStr(const std::string& json, const char* key)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return "";
    // skip whitespace
    while (++pos < json.size() && isspace((unsigned char)json[pos])) {}
    if (pos >= json.size()) return "";
    if (json[pos] != '"') return "";  // not a string value
    size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

// Extract simple key:value pairs from sub-object like {"A":"1","B":"2"}
static void ExtractParams(const std::string& json, const char* objKey,
                          std::vector<std::pair<std::string,std::string>>& out)
{
    out.clear();
    std::string pat = std::string("\"") + objKey + "\"";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return;
    pos = json.find('{', pos + pat.size());
    if (pos == std::string::npos) return;
    size_t end = json.find('}', pos);
    if (end == std::string::npos) return;

    std::string block = json.substr(pos + 1, end - pos - 1);
    // parse "key":"val"
    size_t i = 0;
    while (i < block.size())
    {
        while (i < block.size() && isspace((unsigned char)block[i])) ++i;
        if (i >= block.size() || block[i] != '"') break;
        size_t ke = block.find('"', i + 1);
        if (ke == std::string::npos) break;
        std::string k = block.substr(i + 1, ke - i - 1);
        i = block.find(':', ke + 1);
        if (i == std::string::npos) break;
        ++i; while (i < block.size() && isspace((unsigned char)block[i])) ++i;
        if (i >= block.size() || block[i] != '"') break;
        size_t ve = block.find('"', i + 1);
        if (ve == std::string::npos) break;
        std::string v = block.substr(i + 1, ve - i - 1);
        out.push_back({k, v});
        i = ve + 1;
    }
}

// ============================================================
// Process a single {name}.Z3PRT + {name}.json pair
// ============================================================

static void ProcessOneTask(const char* z3prtPath, const char* jsonPath, const char* baseName)
{
    char msg[512];

    // 1. Read and parse task JSON
    std::string json = SlurpFile(jsonPath);
    if (json.empty()) {
        sprintf_s(msg, "[TaskWorker] Cannot read JSON: %s", jsonPath); Log(msg); return;
    }

    std::string taskId     = JsonStr(json, "taskId");
    std::string outName    = JsonStr(json, "outputFileName");
    std::vector<std::pair<std::string,std::string>> params;
    ExtractParams(json, "params", params);

    if (taskId.empty() || outName.empty()) {
        sprintf_s(msg, "[TaskWorker] JSON missing taskId or outputFileName: %s", jsonPath);
        Log(msg); return;
    }

    sprintf_s(msg, "[TaskWorker] taskId=%s, file=%s, params=%d",
              taskId.c_str(), outName.c_str(), (int)params.size());
    Log(msg);

    // 2. Open the Z3PRT
    int ret = cvxFileOpen(z3prtPath);
    if (ret != 0) {
        sprintf_s(msg, "[TaskWorker] cvxFileOpen failed: ret=%d", ret); Log(msg);
        std::string err = "{\"taskId\":\"" + taskId + "\",\"success\":false,\"stpPath\":\"\",\"error\":\"cvxFileOpen failed\"}";
        WriteFile((std::string(OUTPUT_DIR) + "\\" + baseName + "_result.json").c_str(), err);
        return;
    }

    // 3. Apply expression parameters
    char activeRoot[256] = {};
    cvxRootInqActive(activeRoot, sizeof(activeRoot));

    int failedCount = 0;
    for (auto& p : params)
    {
        // Call SetCurrentPartExpression from PartExpressionCommand_Fixed.cpp
        int r = SetCurrentPartExpression(p.first.c_str(), p.second.c_str());
        if (r != 0)
        {
            sprintf_s(msg, "[TaskWorker] SetExpression %s=%s failed: ret=%d",
                      p.first.c_str(), p.second.c_str(), r);
            Log(msg);
            ++failedCount;
        }
    }
    sprintf_s(msg, "[TaskWorker] Expression update: success=%d failed=%d",
              (int)params.size() - failedCount, failedCount);
    Log(msg);

    // 4. Save the modified part
    ret = cvxFileSave(1);
    if (ret != 0)
    {
        sprintf_s(msg, "[TaskWorker] cvxFileSave failed: ret=%d", ret); Log(msg);
        cvxFileClose();
        std::string err = "{\"taskId\":\"" + taskId + "\",\"success\":false,\"stpPath\":\"\",\"error\":\"save failed\"}";
        WriteFile((std::string(OUTPUT_DIR) + "\\" + baseName + "_result.json").c_str(), err);
        return;
    }

    // 5. Export as STP
    char stpPath[512];
    sprintf_s(stpPath, "%s\\%s", OUTPUT_DIR, outName.c_str());

    svxSTEPData stepData = {};
    cvxFileExportInit(VX_EXPORT_TYPE_STEP, 0, &stepData);
    stepData.AppProtocol = 2;   // AP242
    stepData.OutPut = 0;        // PART

    ret = cvxFileExport(VX_EXPORT_TYPE_STEP, stpPath, &stepData);

    // 6. Close the file
    cvxFileClose();

    // 7. Write result JSON
    std::string resultJson;
    if (ret == 0)
    {
        resultJson = "{\"taskId\":\"" + taskId + "\",\"success\":true,\"stpPath\":\"" + stpPath + "\",\"error\":\"\"}";
    }
    else
    {
        char errBuf[128];
        sprintf_s(errBuf, "export failed: ret=%d", ret);
        resultJson = "{\"taskId\":\"" + taskId + "\",\"success\":false,\"stpPath\":\"\",\"error\":\"" + std::string(errBuf) + "\"}";
    }

    WriteFile((std::string(OUTPUT_DIR) + "\\" + baseName + "_result.json").c_str(), resultJson);

    sprintf_s(msg, "[TaskWorker] Done: taskId=%s, success=%d", taskId.c_str(), ret == 0);
    Log(msg);

    // 8. Archive task files
    MoveFileA(z3prtPath, (std::string(ARCHIVE_DIR) + "\\" + baseName + ".Z3PRT").c_str());
    MoveFileA(jsonPath,  (std::string(ARCHIVE_DIR) + "\\" + baseName + ".json").c_str());
}

// ============================================================
// Scan and process all pending {name}.Z3PRT + {name}.json pairs
// ============================================================

static int ProcessAllTasks()
{
    char search[512];
    sprintf_s(search, "%s\\*.Z3PRT", INPUT_DIR);

    WIN32_FIND_DATAA fd = {};
    HANDLE h = FindFirstFileA(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    int done = 0;
    do
    {
        std::string z3prtFile = fd.cFileName;
        std::string base = StripExt(fd.cFileName);
        std::string jsonFile = base + ".json";

        char z3prtPath[512], jsonPath[512];
        sprintf_s(z3prtPath, "%s\\%s", INPUT_DIR, z3prtFile.c_str());
        sprintf_s(jsonPath,  "%s\\%s", INPUT_DIR, jsonFile.c_str());

        // Only process when the JSON companion exists
        if (GetFileAttributesA(jsonPath) == INVALID_FILE_ATTRIBUTES)
        {
            char msg[512];
            sprintf_s(msg, "[TaskWorker] Skipping %s (no matching .json)", z3prtFile.c_str());
            Log(msg);
            continue;
        }

        ProcessOneTask(z3prtPath, jsonPath, base.c_str());
        ++done;
    }
    while (FindNextFileA(h, &fd));

    FindClose(h);
    return done;
}

// ============================================================
// Commands
// ============================================================

int ProcessTaskFolder(void)
{
    EnsureDir(INPUT_DIR); EnsureDir(OUTPUT_DIR); EnsureDir(ARCHIVE_DIR);
    Log("[TaskWorker] Processing task folder...");
    int n = ProcessAllTasks();
    char msg[128]; sprintf_s(msg, "[TaskWorker] Done: %d task(s)", n); Log(msg);
    return 0;
}

int StartTaskWatcher(void)
{
    if (g_watching) { Log("[TaskWorker] Already watching."); return 0; }
    g_watching = true; g_stopFlag = false;
    EnsureDir(INPUT_DIR); EnsureDir(OUTPUT_DIR); EnsureDir(ARCHIVE_DIR);
    Log("[TaskWorker] Watcher started (main thread with message pump).");
    while (!g_stopFlag)
    {
        ProcessAllTasks();
        // Pump Windows messages so ZW3D stays responsive
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(POLL_INTERVAL_MS);
    }
    g_watching = false;
    Log("[TaskWorker] Watcher stopped.");
    return 0;
}

int StopTaskWatcher(void) { g_stopFlag = true; return 0; }

// ============================================================
// Registration
// ============================================================

int RegisterFileWatcherCommands(void)
{
    cvxCmdFunc("ProcessTaskFolder", (void*)ProcessTaskFolder, VX_CODE_GENERAL);
    cvxCmdFunc("StartTaskWatcher",  (void*)StartTaskWatcher,  VX_CODE_GENERAL);
    cvxCmdFunc("StopTaskWatcher",   (void*)StopTaskWatcher,   VX_CODE_GENERAL);
    return 0;
}

int UnloadFileWatcherCommands(void)
{
    StopTaskWatcher();
    cvxCmdFuncUnload("ProcessTaskFolder");
    cvxCmdFuncUnload("StartTaskWatcher");
    cvxCmdFuncUnload("StopTaskWatcher");
    return 0;
}
