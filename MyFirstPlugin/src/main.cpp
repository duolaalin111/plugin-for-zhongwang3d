#include "..\inc\MyFirstPluginPr.h"
#include "zwapi_global_apply.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>

int MyFirstPluginInit();

namespace
{
char g_profileResourcePath[512] = {};

void RegisterProfileResources()
   {
   HMODULE module = nullptr;
   if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCSTR>(&MyFirstPluginInit), &module))
      return;

   char modulePath[MAX_PATH] = {};
   if (GetModuleFileNameA(module, modulePath, MAX_PATH) == 0)
      return;

   char* separator = std::strrchr(modulePath, '\\');
   if (separator == nullptr)
      return;

   *separator = '\0';
   if (sprintf_s(g_profileResourcePath, "%s\\MyFirstPluginUI", modulePath) <= 0)
      return;

   // ZW3D API Introduction, chapter 3: the registered path must be the
   // directory immediately above Settings\\Default.
   ZwResourcePathAdd(g_profileResourcePath);
   ZwProfileModuleNameRegister("MyFirstPluginUI");
   }
}

// Dynamic library entry function, called when the dll is loaded
// The function name must be dll name + "Init"
int MyFirstPluginInit()
   {
   RegisterProfileResources();
   RegisterCustomCommand();
   RegisterTemplateCommand();
   RegisterFormCommand();
   RegisterPartInsertCommand();
   RegisterPartExpressionCommands();
   RegisterFileWatcherCommands();
   return 0;
   }

// Dynamic library entry function, called when the dll is unloaded
// The function name must be dll name + "Exit"
int MyFirstPluginExit()
   {
   UnloadCustomCommand();
   UnloadTemplateCommand();
   UnloadFormCommand();
   UnloadPartInsertCommand();
   UnloadPartExpressionCommands();
   UnloadFileWatcherCommands();
   if (g_profileResourcePath[0] != '\0')
      ZwResourcePathDelete(g_profileResourcePath);
   return 0;
   }
