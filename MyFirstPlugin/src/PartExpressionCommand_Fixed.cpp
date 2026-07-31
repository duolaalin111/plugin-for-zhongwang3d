// PartExpressionCommand_Fixed.cpp
// ------------------------------------------------------------
// Independent ZW3D command module for reading and changing
// Revision: accepts command arguments both with and without
// surrounding quotation marks.
// expressions/variables in the currently active Z3PRT root.
//
// This file does NOT modify or depend on PartInsertCommand.cpp.
//
// Commands registered by this file:
//   ListPartExpressions
//   GetPartExpression
//   SetPartExpression
//   SetPartExpressions
//
// Examples in the ZW3D command line:
//   ~ListPartExpressions()
//   ~GetPartExpression("Length")
//   ~SetPartExpression("Length=120")
//   ~SetPartExpression("Length=120mm")
//   ~SetPartExpressions("Length=120;Width=60;Height=Length/2")
//
// Before running the commands, open the target .Z3PRT and activate
// the part/root whose expressions you want to inspect or modify.
// ------------------------------------------------------------

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

// cvxCmdFunc / cvxCmdFuncUnload / cvxMsgDisp / cvxMemFree
#include "zwapi_cmd.h"
#include "zwapi_message.h"
#include "zwapi_memory.h"
#include "zwapi_cmd_assembly.h"

// Expression, active file/root and part-variable APIs.
// These headers are part of the ZW3D C/C++ SDK.
#include "zwapi_root.h"
#include "zwapi_file.h"
#include "zwapi_asm_comp.h"
#include "zwapi_part_var.h"

// ------------------------------------------------------------
// Settings
// ------------------------------------------------------------

// cvxPartVarSet Working argument.
// 1 means modify the current working root in the active session.
static const int kWorkingRoot = 1;

// Maximum text accepted by the command wrappers.
static const size_t kCommandTextCapacity = 4096;

// ------------------------------------------------------------
// Small utilities
// ------------------------------------------------------------

template <size_t N>
static void copyText(char (&destination)[N], const char* source)
{
    destination[0] = '\0';

    if (source == nullptr)
        return;

    strcpy_s(destination, N, source);
}

static void displayMessage(const char* text)
{
    if (text == nullptr)
        return;

    cvxMsgDisp(text);
    OutputDebugStringA(text);
    OutputDebugStringA("\n");
}

static char* trimInPlace(char* text)
{
    if (text == nullptr)
        return nullptr;

    while (*text != '\0' &&
           std::isspace(static_cast<unsigned char>(*text)))
    {
        ++text;
    }

    char* end = text + std::strlen(text);

    while (end > text &&
           std::isspace(static_cast<unsigned char>(end[-1])))
    {
        --end;
    }

    *end = '\0';
    return text;
}

// ZW3D may pass the quotation marks from a command argument into
// the registered C function. For example:
//
//   ~SetPartExpressions("InDia=0.375;OutDia=0.875")
//
// may arrive as:
//
//   "InDia=0.375;OutDia=0.875"
//
// Strip exactly one matching pair around the entire argument.
// Do not strip quotes inside an expression because quoted string
// expressions are valid ZW3D expressions.
static char* stripOuterCommandQuotesInPlace(char* text)
{
    char* trimmed = trimInPlace(text);

    if (trimmed == nullptr)
        return nullptr;

    const size_t length = std::strlen(trimmed);

    if (length >= 2)
    {
        const char first = trimmed[0];
        const char last = trimmed[length - 1];

        if ((first == '"' && last == '"') ||
            (first == '\'' && last == '\''))
        {
            trimmed[length - 1] = '\0';
            ++trimmed;
            trimmed = trimInPlace(trimmed);
        }
    }

    return trimmed;
}

static bool textEqualsIgnoreCase(const char* left, const char* right)
{
    if (left == nullptr || right == nullptr)
        return false;

    return _stricmp(left, right) == 0;
}

static bool isPureNumber(const char* text, double* valueOut)
{
    if (text == nullptr)
        return false;

    while (*text != '\0' &&
           std::isspace(static_cast<unsigned char>(*text)))
    {
        ++text;
    }

    if (*text == '\0')
        return false;

    char* end = nullptr;
    const double value = std::strtod(text, &end);

    if (end == text)
        return false;

    while (*end != '\0' &&
           std::isspace(static_cast<unsigned char>(*end)))
    {
        ++end;
    }

    if (*end != '\0')
        return false;

    if (valueOut != nullptr)
        *valueOut = value;

    return true;
}

// ------------------------------------------------------------
// Active file/root information
// ------------------------------------------------------------

static bool getActiveFileAndRoot(
    char* fileName,
    int fileNameCapacity,
    char* rootName,
    int rootNameCapacity)
{
    if (fileName == nullptr || fileNameCapacity <= 0 ||
        rootName == nullptr || rootNameCapacity <= 0)
    {
        return false;
    }

    fileName[0] = '\0';
    rootName[0] = '\0';

    cvxFileInqActive(fileName, fileNameCapacity);
    cvxRootInqActive(rootName, rootNameCapacity);

    if (fileName[0] == '\0' || rootName[0] == '\0')
    {
        displayMessage(
            "Expression command failed: no active file/root. "
            "Open a Z3PRT file and activate its part first.");
        return false;
    }

    return true;
}

// ------------------------------------------------------------
// Read all expressions from the active part/root
//
// cvxPartInqVars allocates the returned array. The caller must
// release it with cvxMemFree((void**)&variables).
// ------------------------------------------------------------

static int inquireActivePartVariables(
    int* countOut,
    svxVariable** variablesOut,
    char* activeFile,
    int activeFileCapacity,
    char* activeRoot,
    int activeRootCapacity)
{
    if (countOut == nullptr || variablesOut == nullptr)
        return -1;

    *countOut = 0;
    *variablesOut = nullptr;

    if (!getActiveFileAndRoot(
            activeFile,
            activeFileCapacity,
            activeRoot,
            activeRootCapacity))
    {
        return -1;
    }

    const int ret = cvxPartInqVars(
        activeFile,
        activeRoot,
        countOut,
        variablesOut);

    if (ret != 0)
    {
        char message[512] = {};
        sprintf_s(
            message,
            "cvxPartInqVars failed: ret=%d, file=%s, root=%s",
            ret,
            activeFile,
            activeRoot);
        displayMessage(message);
        return ret;
    }

    return 0;
}

static void freeVariables(svxVariable** variables)
{
    if (variables == nullptr || *variables == nullptr)
        return;

    cvxMemFree(reinterpret_cast<void**>(variables));
}

// ------------------------------------------------------------
// Find an expression by its real name first, then by description.
//
// This supports both:
//   - the actual expression name shown by the expression manager;
//   - the description/tag text shown in configuration-related UI.
// ------------------------------------------------------------

static bool findVariableByNameOrDescription(
    const char* nameOrDescription,
    svxVariable* variableOut)
{
    if (nameOrDescription == nullptr ||
        nameOrDescription[0] == '\0' ||
        variableOut == nullptr)
    {
        return false;
    }

    // Fast path: query directly by real expression name.
    svxVariable directVariable = {};
    copyText(directVariable.Name, nameOrDescription);

    if (cvxPartVarGet(&directVariable) == 0)
    {
        *variableOut = directVariable;
        return true;
    }

    // Fallback: enumerate variables and compare both Name/description.
    int count = 0;
    svxVariable* variables = nullptr;
    char activeFile[600] = {};
    char activeRoot[256] = {};

    const int ret = inquireActivePartVariables(
        &count,
        &variables,
        activeFile,
        static_cast<int>(sizeof(activeFile)),
        activeRoot,
        static_cast<int>(sizeof(activeRoot)));

    if (ret != 0)
        return false;

    bool found = false;

    // First pass: exact real-name match.
    for (int index = 0; index < count; ++index)
    {
        if (textEqualsIgnoreCase(
                variables[index].Name,
                nameOrDescription))
        {
            *variableOut = variables[index];
            found = true;
            break;
        }
    }

    // Second pass: description match.
    if (!found)
    {
        for (int index = 0; index < count; ++index)
        {
            if (variables[index].description[0] != '\0' &&
                textEqualsIgnoreCase(
                    variables[index].description,
                    nameOrDescription))
            {
                *variableOut = variables[index];
                found = true;
                break;
            }
        }
    }

    freeVariables(&variables);
    return found;
}

// ------------------------------------------------------------
// Public helper for future UI code.
//
// nameOrDescription:
//   Expression's actual Name or its description.
//
// newExpression:
//   New expression text, for example:
//     "120"
//     "120mm"
//     "Width*2"
//     "Length/2+5"
//
// The function preserves the expression's original metadata and
// changes only Expression. If the new text is a plain number, Value
// is synchronized as well.
// ------------------------------------------------------------

int SetCurrentPartExpression(
    const char* nameOrDescription,
    const char* newExpression)
{
    if (nameOrDescription == nullptr ||
        nameOrDescription[0] == '\0' ||
        newExpression == nullptr ||
        newExpression[0] == '\0')
    {
        displayMessage(
            "SetCurrentPartExpression failed: "
            "name or expression is empty.");
        return -1;
    }

    svxVariable variable = {};

    if (!findVariableByNameOrDescription(
            nameOrDescription,
            &variable))
    {
        char message[512] = {};
        sprintf_s(
            message,
            "Expression not found: [%s]. "
            "Run ListPartExpressions first and use the exact Name "
            "or Description shown in the output.",
            nameOrDescription);
        displayMessage(message);
        return -1;
    }

    char oldExpression[sizeof(variable.Expression)] = {};
    copyText(oldExpression, variable.Expression);

    copyText(variable.Expression, newExpression);

    // For a simple numeric expression, keep Value synchronized.
    // Formulas and values containing units are evaluated by ZW3D.
    double numericValue = 0.0;
    if (isPureNumber(newExpression, &numericValue))
        variable.Value = numericValue;

    const int ret = cvxPartVarSet(
        1,
        &variable,
        kWorkingRoot);

    char message[1024] = {};

    if (ret != 0)
    {
        sprintf_s(
            message,
            "cvxPartVarSet failed: ret=%d, Name=%s, "
            "old Expression=%s, requested Expression=%s",
            ret,
            variable.Name,
            oldExpression[0] ? oldExpression : "(empty)",
            newExpression);
        displayMessage(message);
        return ret;
    }

    // Read it back to verify what ZW3D accepted.
    svxVariable verified = {};
    copyText(verified.Name, variable.Name);
    const int verifyRet = cvxPartVarGet(&verified);

    if (verifyRet == 0)
    {
        sprintf_s(
            message,
            "Expression updated: Name=%s, Description=%s, "
            "old=%s, new=%s, evaluated Value=%.15g",
            verified.Name,
            verified.description[0]
                ? verified.description
                : "(empty)",
            oldExpression[0]
                ? oldExpression
                : "(empty)",
            verified.Expression[0]
                ? verified.Expression
                : "(empty)",
            verified.Value);
    }
    else
    {
        sprintf_s(
            message,
            "Expression update returned success, but read-back failed: "
            "verifyRet=%d, Name=%s, requested Expression=%s",
            verifyRet,
            variable.Name,
            newExpression);
    }

    displayMessage(message);
    return 0;
}

// ------------------------------------------------------------
// Parse one assignment:
//
//   Name=Expression
//
// Only the first '=' is treated as the separator, so the right side
// may contain other operators or function text.
// ------------------------------------------------------------

static bool parseAssignment(
    char* assignment,
    char** nameOut,
    char** expressionOut)
{
    if (assignment == nullptr ||
        nameOut == nullptr ||
        expressionOut == nullptr)
    {
        return false;
    }

    char* separator = std::strchr(assignment, '=');

    if (separator == nullptr)
        return false;

    *separator = '\0';

    char* name = trimInPlace(assignment);
    char* expression = trimInPlace(separator + 1);

    if (name == nullptr || expression == nullptr ||
        name[0] == '\0' || expression[0] == '\0')
    {
        return false;
    }

    *nameOut = name;
    *expressionOut = expression;
    return true;
}

// ------------------------------------------------------------
// Command 1: list all expressions in the active part/root.
// ------------------------------------------------------------

int ListPartExpressions(void)
{
    int count = 0;
    svxVariable* variables = nullptr;
    char activeFile[600] = {};
    char activeRoot[256] = {};

    const int ret = inquireActivePartVariables(
        &count,
        &variables,
        activeFile,
        static_cast<int>(sizeof(activeFile)),
        activeRoot,
        static_cast<int>(sizeof(activeRoot)));

    if (ret != 0)
        return ret;

    char header[768] = {};
    sprintf_s(
        header,
        "Part expressions: file=%s, root=%s, count=%d",
        activeFile,
        activeRoot,
        count);
    displayMessage(header);

    for (int index = 0; index < count; ++index)
    {
        const svxVariable& variable = variables[index];

        char message[1536] = {};
        sprintf_s(
            message,
            "[%d/%d] Name=%s | Description=%s | "
            "Expression=%s | Value=%.15g",
            index + 1,
            count,
            variable.Name[0]
                ? variable.Name
                : "(empty)",
            variable.description[0]
                ? variable.description
                : "(empty)",
            variable.Expression[0]
                ? variable.Expression
                : "(empty)",
            variable.Value);

        displayMessage(message);
    }

    freeVariables(&variables);

    char footer[512] = {};
    sprintf_s(
        footer,
        "ListPartExpressions finished: %d expression(s). "
        "Use the exact Name or Description with SetPartExpression.",
        count);
    displayMessage(footer);

    return 0;
}

// ------------------------------------------------------------
// Command 2: query one expression.
//
// Example:
//   ~GetPartExpression("Length")
// ------------------------------------------------------------

int GetPartExpression(char* nameOrDescription)
{
    if (nameOrDescription == nullptr)
    {
        displayMessage(
            "Usage: ~GetPartExpression(\"ExpressionName\")");
        return -1;
    }

    char input[1024] = {};
    copyText(input, nameOrDescription);
    char* name = stripOuterCommandQuotesInPlace(input);

    if (name == nullptr || name[0] == '\0')
    {
        displayMessage(
            "Usage: ~GetPartExpression(\"ExpressionName\")");
        return -1;
    }

    svxVariable variable = {};

    if (!findVariableByNameOrDescription(name, &variable))
    {
        char message[512] = {};
        sprintf_s(
            message,
            "Expression not found: [%s]. "
            "Run ListPartExpressions first.",
            name);
        displayMessage(message);
        return -1;
    }

    char message[1536] = {};
    sprintf_s(
        message,
        "Expression: Name=%s | Description=%s | "
        "Expression=%s | Value=%.15g",
        variable.Name,
        variable.description[0]
            ? variable.description
            : "(empty)",
        variable.Expression[0]
            ? variable.Expression
            : "(empty)",
        variable.Value);
    displayMessage(message);

    return 0;
}

// ------------------------------------------------------------
// Command 3: modify one expression.
//
// Input format:
//   Name=Expression
//
// Examples:
//   ~SetPartExpression("Length=120")
//   ~SetPartExpression("Length=120mm")
//   ~SetPartExpression("Height=Width/2")
// ------------------------------------------------------------

int SetPartExpression(char* assignment)
{
    if (assignment == nullptr)
    {
        displayMessage(
            "Usage: ~SetPartExpression("
            "\"ExpressionName=NewExpression\")");
        return -1;
    }

    char input[kCommandTextCapacity] = {};
    copyText(input, assignment);

    char* normalizedInput =
        stripOuterCommandQuotesInPlace(input);

    char* name = nullptr;
    char* expression = nullptr;

    if (!parseAssignment(normalizedInput, &name, &expression))
    {
        displayMessage(
            "Invalid assignment. Use: "
            "~SetPartExpression(\"Name=Expression\")");
        return -1;
    }

    return SetCurrentPartExpression(name, expression);
}

// ------------------------------------------------------------
// Command 4: modify several expressions in one call.
//
// Input format:
//   Name1=Expression1;Name2=Expression2;...
//
// Example:
//   ~SetPartExpressions(
//       "Length=120;Width=60;Height=Length/2")
//
// Changes are performed one by one. The command reports how many
// succeeded and how many failed.
// ------------------------------------------------------------

int SetPartExpressions(char* assignments)
{
    if (assignments == nullptr)
    {
        displayMessage(
            "Usage: ~SetPartExpressions("
            "\"Name1=Expression1;Name2=Expression2\")");
        return -1;
    }

    char input[kCommandTextCapacity] = {};
    copyText(input, assignments);

    char* normalizedInput =
        stripOuterCommandQuotesInPlace(input);

    if (normalizedInput == nullptr ||
        normalizedInput[0] == '\0')
    {
        displayMessage(
            "SetPartExpressions failed: empty assignment text.");
        return -1;
    }

    int successCount = 0;
    int failureCount = 0;

    char* context = nullptr;
    char* token = strtok_s(
        normalizedInput,
        ";",
        &context);

    while (token != nullptr)
    {
        char* trimmedToken = trimInPlace(token);

        if (trimmedToken != nullptr && trimmedToken[0] != '\0')
        {
            char* name = nullptr;
            char* expression = nullptr;

            if (parseAssignment(
                    trimmedToken,
                    &name,
                    &expression))
            {
                const int ret =
                    SetCurrentPartExpression(name, expression);

                if (ret == 0)
                    ++successCount;
                else
                    ++failureCount;
            }
            else
            {
                char message[768] = {};
                sprintf_s(
                    message,
                    "Skipped invalid assignment: [%s]. "
                    "Required format: Name=Expression",
                    trimmedToken);
                displayMessage(message);
                ++failureCount;
            }
        }

        token = strtok_s(nullptr, ";", &context);
    }

    char summary[512] = {};
    sprintf_s(
        summary,
        "SetPartExpressions finished: success=%d, failed=%d",
        successCount,
        failureCount);
    displayMessage(summary);

    return failureCount == 0 ? 0 : -1;
}

// ============================================================
// ListAsmExpressions — list expressions of all sub-components
// in the currently open assembly.
// ============================================================
int ListAsmExpressions(void)
{
    char file[600] = {}, root[256] = {};
    cvxFileInqActive(file, sizeof(file));
    cvxRootInqActive(root, sizeof(root));

    char msg[768];
    sprintf_s(msg, "[AsmExp] file=%s root=%s", file, root);
    displayMessage(msg);

    int count = 0; vxLongPath* paths = nullptr; vxRootName* names = nullptr;
    int ret = cvxPartInqCompsInfoByLongPath(file, root, &count, &paths, &names);
    sprintf_s(msg, "[AsmExp] comps ret=%d count=%d", ret, count);
    displayMessage(msg);

    if (ret == 0 && paths && names)
    {
        for (int i = 0; i < count; ++i)
        {
            sprintf_s(msg, "[AsmExp] comp[%d] path=%s name=%s", i, paths[i], names[i]);
            displayMessage(msg);

            int vc = 0; svxVariable* vars = nullptr;
            int vr = cvxPartInqVars(paths[i], names[i], &vc, &vars);
            if (vr == 0 && vars)
            {
                for (int j = 0; j < vc; ++j)
                {
                    if (vars[j].Name[0])
                    {
                        sprintf_s(msg, "[AsmExp]   expr: %s = %.6g (desc:%s)",
                                  vars[j].Name, vars[j].Value,
                                  vars[j].description[0] ? vars[j].description : "");
                        displayMessage(msg);
                    }
                }
                cvxMemFree((void**)&vars);
            }
        }
        cvxMemFree((void**)&paths);
        cvxMemFree((void**)&names);
    }

    displayMessage("[AsmExp] Done.");
    return 0;
}

// ------------------------------------------------------------
// Registration
//
// Add this call to your existing plug-in initialization function:
//   RegisterPartExpressionCommands();
//
// Add this call to your existing plug-in unload function:
//   UnloadPartExpressionCommands();
// ------------------------------------------------------------

int RegisterPartExpressionCommands(void)
{
    int firstError = 0;

    int ret = cvxCmdFunc(
        "ListPartExpressions",
        reinterpret_cast<void*>(ListPartExpressions),
        VX_CODE_GENERAL);
    if (ret != 0 && firstError == 0)
        firstError = ret;

    ret = cvxCmdFunc(
        "GetPartExpression",
        reinterpret_cast<void*>(GetPartExpression),
        VX_CODE_GENERAL);
    if (ret != 0 && firstError == 0)
        firstError = ret;

    ret = cvxCmdFunc(
        "SetPartExpression",
        reinterpret_cast<void*>(SetPartExpression),
        VX_CODE_GENERAL);
    if (ret != 0 && firstError == 0)
        firstError = ret;

    ret = cvxCmdFunc(
        "SetPartExpressions",
        reinterpret_cast<void*>(SetPartExpressions),
        VX_CODE_GENERAL);
    if (ret != 0 && firstError == 0)
        firstError = ret;

    ret = cvxCmdFunc(
        "ListAsmExpressions",
        reinterpret_cast<void*>(ListAsmExpressions),
        VX_CODE_GENERAL);
    if (ret != 0 && firstError == 0) firstError = ret;

    if (firstError == 0)
    {
        displayMessage(
            "Part expression commands registered: "
            "ListPartExpressions, GetPartExpression, "
            "SetPartExpression, SetPartExpressions, ListAsmExpressions.");
    }
    else
    {
        char message[256] = {};
        sprintf_s(
            message,
            "RegisterPartExpressionCommands finished "
            "with error=%d",
            firstError);
        displayMessage(message);
    }

    return firstError;
}

int UnloadPartExpressionCommands(void)
{
    cvxCmdFuncUnload("ListPartExpressions");
    cvxCmdFuncUnload("GetPartExpression");
    cvxCmdFuncUnload("SetPartExpression");
    cvxCmdFuncUnload("SetPartExpressions");
    cvxCmdFuncUnload("ListAsmExpressions");
    return 0;
}
