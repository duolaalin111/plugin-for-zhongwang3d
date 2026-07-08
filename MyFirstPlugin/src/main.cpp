#include "..\inc\MyFirstPluginPr.h"

// Dynamic library entry function, called when the dll is loaded
// The function name must be dll name + "Init"
int MyFirstPluginInit()
   {
   RegisterCustomCommand();
   RegisterTemplateCommand();
   RegisterFormCommand();
   return 0;
   }

// Dynamic library entry function, called when the dll is unloaded
// The function name must be dll name + "Exit"
int MyFirstPluginExit()
   {
   UnloadCustomCommand();
   UnloadTemplateCommand();
   UnloadFormCommand();
   return 0;
   }