// PartInsertCommand.cpp
// ZW3D Reuse Library parameterized part insertion.
//
// Verified parameter format for ANSI Radial Ball Bearing:
//   Use the <TAG> descriptions from Radial Ball Bearing.xlsx, for example:
//   "I.D. (In.),3/8;O.D. (In.),7/8;Width (In.),.25"
//
// Do not insert the default part first and then call cvxLibPartAdjust().
// Pass ValStr directly to cvxLibPartIns() when creating the instance.

#include <windows.h>
#include <cstdio>

#include "zwapi_cmd_assembly.h"
#include "zwapi_asm_reuselibrary.h"
#include "zwapi_materiallibrary.h"
#include "..\inc\MyFirstPluginPr.h"

// -----------------------------------------------------------------------------
// Fixed reuse-library paths used by the command examples.
// -----------------------------------------------------------------------------
static const char* const HEX_CAP_BOLT_DIR =
    "D:\\BaiduNetdiskDownload\\ZWSOFT\\ZW3D WuKong 2027"
    "\\Reuse Library\\Standard Parts\\ANSI\\Fasteners"
    "\\Hex Cap Bolt";

static const char* const RADIAL_BEARING_DIR =
    "D:\\BaiduNetdiskDownload\\ZWSOFT\\ZW3D WuKong 2027"
    "\\Reuse Library\\Standard Parts\\ANSI\\Bearings"
    "\\Radial Ball Bearing";

// -----------------------------------------------------------------------------
// Safely copy text into fixed-size character arrays in ZW3D structures.
// -----------------------------------------------------------------------------
template <size_t N>
static void CopyText(char (&destination)[N], const char* source)
{
    if (source == nullptr)
    {
        destination[0] = '\0';
        return;
    }

    lstrcpynA(destination, source, static_cast<int>(N));
    destination[N - 1] = '\0';
}

// -----------------------------------------------------------------------------
// Some reuse-library parts reference these materials. If a material already
// exists, the returned error is intentionally ignored because it does not stop
// the insertion operation.
// -----------------------------------------------------------------------------
static void EnsureMaterial(const char* materialName)
{
    if (materialName == nullptr || materialName[0] == '\0')
        return;

    szwMaterialNameGroup material = {};

    CopyText(material.libraryName,  "MyPlugin Materials");
    CopyText(material.categoryName, "Steel");
    CopyText(material.materialName, materialName);

    ZwMaterialLibraryMaterialCreate(
        material,
        7800.0,
        ZW_UNIT_DENSITY_DEN_KG_M3);
}

// -----------------------------------------------------------------------------
// Show one compact result message after insertion.
// -----------------------------------------------------------------------------
static void DisplayInsertResult(
    int returnCode,
    int objectId,
    int fileType,
    int insertAsShape,
    int copyPart,
    const char* file,
    const char* part,
    const char* instanceName,
    const char* valStr)
{
    char message[1600] = { 0 };

    _snprintf_s(
        message,
        _countof(message),
        _TRUNCATE,
        "cvxLibPartIns result\n"
        "ret=%d, idOut=%d\n"
        "fileType=%d, asShape=%d, CopyPart=%d\n"
        "File=%s\n"
        "Part=%s\n"
        "InstanceName=%s\n"
        "ValStr=%s",
        returnCode,
        objectId,
        fileType,
        insertAsShape,
        copyPart,
        file != nullptr ? file : "(null)",
        part != nullptr ? part : "(null)",
        (instanceName != nullptr && instanceName[0] != '\0')
            ? instanceName
            : "(automatic)",
        (valStr != nullptr && valStr[0] != '\0')
            ? valStr
            : "(default)");

    cvxMsgDisp(message);
}

// =============================================================================
// InsertPartWithSelection
//
// dir:
//   Directory containing the reuse-library .Z3PRT template.
//
// file:
//   Template filename, for example "Radial Ball Bearing.Z3PRT".
//
// part:
//   Root name inside the template, for example "Radial Ball Bearing".
//
// instanceName:
//   Optional generated instance name. Pass nullptr to let the reuse library
//   generate the instance name from the selected Excel row.
//
// valStr:
//   Reuse-library parameter selection string. Format:
//       parameter,value;parameter,value;parameter,value
//
//   For Radial Ball Bearing, the verified working format uses the Excel <TAG>
//   descriptions, for example:
//       I.D. (In.),3/8;O.D. (In.),7/8;Width (In.),.25
//
// insertAsShape:
//   0 = insert as assembly component
//   1 = insert as shape/feature in the current part
//
// createNewFile:
//   0 = create in the current root
//   1 = create a separate part file
// =============================================================================
static int InsertPartWithSelection(
    const char* dir,
    const char* file,
    const char* part,
    const char* instanceName,
    const char* valStr,
    int insertAsShape,
    int createNewFile,
    const svxPoint* insertionPoint = nullptr)
{
    if (dir == nullptr || dir[0] == '\0' ||
        file == nullptr || file[0] == '\0' ||
        part == nullptr || part[0] == '\0')
    {
        cvxMsgDisp(
            "InsertPartWithSelection failed: "
            "dir, file and part must not be empty.");
        return -1;
    }

    EnsureMaterial("Low Carbon Steel");
    EnsureMaterial("Carbon Steel");

    svxCompData componentData = {};
    const int initResult = cvxCompInsInit(&componentData);
    if (initResult != 0)
    {
        char message[256] = {};
        sprintf_s(
            message,
            "cvxCompInsInit failed: ret=%d",
            initResult);
        cvxMsgDisp(message);
        return initResult;
    }

    CopyText(componentData.Dir, dir);
    CopyText(componentData.File, file);
    CopyText(componentData.Part, part);

    if (insertionPoint != nullptr)
    {
        // A translated identity frame places the generated part at the point
        // selected by the user in the active ZW3D model.
        componentData.Frame.identity = 0;
        componentData.Frame.xx = 1.0;
        componentData.Frame.yy = 1.0;
        componentData.Frame.zz = 1.0;
        componentData.Frame.xt = insertionPoint->x;
        componentData.Frame.yt = insertionPoint->y;
        componentData.Frame.zt = insertionPoint->z;
    }
    else
    {
        componentData.Frame.identity = 1;
    }

    // Activate the inserted component automatically.
    componentData.SettingsData.AutoActivated = 1;

    // A separate standard-part file should use an independent copied part.
    componentData.InstanceData.CopyPart = createNewFile ? 1 : 0;

    const int fileType = createNewFile ? 1 : 0;
    const int asShape = insertAsShape ? 1 : 0;

    const char* actualInstanceName =
        (instanceName != nullptr && instanceName[0] != '\0')
            ? instanceName
            : nullptr;

    const char* actualValStr =
        (valStr != nullptr && valStr[0] != '\0')
            ? valStr
            : "";

    int idOut = 0;

    // Parameterization is performed here in one step. Do not first insert the
    // default part and then call cvxLibPartAdjust().
    const int returnCode = cvxLibPartIns(
        &componentData,
        actualInstanceName,
        actualValStr,
        fileType,
        asShape,
        &idOut);

    DisplayInsertResult(
        returnCode,
        idOut,
        fileType,
        asShape,
        componentData.InstanceData.CopyPart,
        componentData.File,
        componentData.Part,
        actualInstanceName,
        actualValStr);

    return returnCode;
}

// =============================================================================
// Public helper retained for compatibility with MyFirstPluginPr.h and other
// project source files. Instance name is generated automatically.
// =============================================================================
int InsertPartWithParams(
    const char* dir,
    const char* file,
    const char* part,
    const char* params,
    int asShape,
    int asNewFile)
{
    return InsertPartWithSelection(
        dir,
        file,
        part,
        nullptr,
        params,
        asShape,
        asNewFile,
        nullptr);
}

int InsertPartWithParamsAt(
    const char* dir,
    const char* file,
    const char* part,
    const char* params,
    int asShape,
    int asNewFile,
    double x,
    double y,
    double z)
{
    const svxPoint insertionPoint = { x, y, z };
    return InsertPartWithSelection(
        dir,
        file,
        part,
        nullptr,
        params,
        asShape,
        asNewFile,
        &insertionPoint);
}

// -----------------------------------------------------------------------------
// Command registration.
// -----------------------------------------------------------------------------
int RegisterPartInsertCommand(void)
{
    cvxCmdFunc(
        "InsertTestBolt",
        (void*)InsertTestBolt,
        VX_CODE_GENERAL);

    cvxCmdFunc(
        "InsertBoltShape",
        (void*)InsertBoltShape,
        VX_CODE_GENERAL);

    cvxCmdFunc(
        "InsertBearingSmall",
        (void*)InsertBearingSmall,
        VX_CODE_GENERAL);

    cvxCmdFunc(
        "InsertBearingLarge",
        (void*)InsertBearingLarge,
        VX_CODE_GENERAL);

    return 0;
}

int UnloadPartInsertCommand(void)
{
    cvxCmdFuncUnload("InsertTestBolt");
    cvxCmdFuncUnload("InsertBoltShape");
    cvxCmdFuncUnload("InsertBearingSmall");
    cvxCmdFuncUnload("InsertBearingLarge");

    return 0;
}

// -----------------------------------------------------------------------------
// Insert the default Hex Cap Bolt as an assembly component in the current root.
// This command still uses the template default because no bolt ValStr has been
// configured yet.
// -----------------------------------------------------------------------------
int InsertTestBolt(void)
{
    return InsertPartWithParams(
        HEX_CAP_BOLT_DIR,
        "Hex Cap Bolt.Z3PRT",
        "Hex Cap Bolt",
        "",
        0,  // component
        0); // current root
}

// -----------------------------------------------------------------------------
// Insert the default Hex Cap Bolt as a shape in the current part.
// -----------------------------------------------------------------------------
int InsertBoltShape(void)
{
    return InsertPartWithParams(
        HEX_CAP_BOLT_DIR,
        "Hex Cap Bolt.Z3PRT",
        "Hex Cap Bolt",
        "",
        1,  // shape
        0); // ignored for shape insertion
}

// -----------------------------------------------------------------------------
// Insert ANSI radial ball bearing:
//   I.D.   = 3/16 in
//   O.D.   = 11/16 in
//   Width  = .25 in
//
// Expected generated name:
//   3_16 11_16 Bearing
// -----------------------------------------------------------------------------
int InsertBearingSmall(void)
{
    return InsertPartWithParams(
        RADIAL_BEARING_DIR,
        "Radial Ball Bearing.Z3PRT",
        "Radial Ball Bearing",
        "I.D. (In.),3/16;"
        "O.D. (In.),11/16;"
        "Width (In.),.25",
        0,  // component
        1); // separate new file
}

// -----------------------------------------------------------------------------
// Insert ANSI radial ball bearing:
//   I.D.   = 3/8 in
//   O.D.   = 7/8 in
//   Width  = .25 in
//
// Expected generated name:
//   3_8 7_8 Bearing
// -----------------------------------------------------------------------------
int InsertBearingLarge(void)
{
    return InsertPartWithParams(
        RADIAL_BEARING_DIR,
        "Radial Ball Bearing.Z3PRT",
        "Radial Ball Bearing",
        "I.D. (In.),3/8;"
        "O.D. (In.),7/8;"
        "Width (In.),.25",
        0,  // component
        1); // separate new file
}
