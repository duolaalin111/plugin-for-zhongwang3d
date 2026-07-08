#include "zwapi_cmd_paramdefine_param.h"
#include "zwapi_cmd_paramdefine_tpl.h"
#include "..\inc\MyFirstPluginPr.h"

#define Field_Entity 1

static int TemplateCmd(int idData);
static void TemplateCmdInit(int idData);
static int TemplateCmdInitA(int idData);
static void TemplateCmdEcho(int idData, void* ohEcho);
static void TemplateCmdTerm(void);
static int TemplateCmdCb(char* form, int idField, int item);

int RegisterTemplateCommand(void)
/*
DESCRIPTION:
   Register callback function of template command.
*/
   {
   /* Start the command by entering command string "!TemplateCmd" */
   cvxCmdFunc("TemplateCmd", (void*)TemplateCmd, VX_CODE_GENERAL);
   cvxCmdCallback("TemplateCmdInit", (void*)TemplateCmdInit);
   cvxCmdCallback("TemplateCmdInitA", (void*)TemplateCmdInitA);
   cvxCmdCallback("TemplateCmdEcho", (void*)TemplateCmdEcho);
   cvxCmdCallback("TemplateCmdTerm", (void*)TemplateCmdTerm);
   cvxCmdCallback("TemplateCmdCb", (void*)TemplateCmdCb);
   return 0;
   }

int UnloadTemplateCommand(void)
/*
DESCRIPTION:
   Unload callback function of template command.
*/
   {
   cvxCmdFuncUnload("TemplateCmd");
   cvxCmdFuncUnload("TemplateCmdInit");
   cvxCmdFuncUnload("TemplateCmdInitA");
   cvxCmdFuncUnload("TemplateCmdEcho");
   cvxCmdFuncUnload("TemplateCmdTerm");
   cvxCmdFuncUnload("TemplateCmdCb");
   return 0;
   }

int TemplateCmd
(
   int idData    /* I: index of data container */
)
/*
DESCRIPTION:
   Execution function of command. the function is called when the command
is clicked on the OK or APPLY button.
*/
   {
   // TODO: Do something

   cvxMsgDisp("TemplateCmd");

   /* Inquire data of data container */
   int nEnts = 0, * idEnts = 0;
   cvxDataGetEnts(idData, Field_Entity, &nEnts, &idEnts);
   cvxMemFree((void**)&idEnts);
   return 0;
   }

void TemplateCmdInit
(
   int idData    /* I: index of data container */
)
/*
DESCRIPTION:
   Initialize callback function of command. the function is called when the command
is initialized. In this callback function, you can initialize the data of command, 
but the command form has not yet been created at this time, so should not initialize
the command form in this callback function.
*/
   {
   // TODO: Do something

   cvxMsgDisp("TemplateCmdInit");
   }

int TemplateCmdInitA
(
   int idData    /* I: index of data container */
)
/*
DESCRIPTION:
   Init-after callback function of command. the function is called when the command
is initialized. In this callback function, you can initialize the command form.
*/
   {
   // TODO: Do something

   cvxMsgDisp("TemplateCmdInitA");
   return 0;
   }

void TemplateCmdEcho
(
   int idData,   /* I: index of data container */
   void* ohEcho  /* I: handle of preview object */
)
/*
DESCRIPTION:
   Preview callback function of command, the function is called when the parameters
of command change.
*/
   {
   cvxEchoStart();

   // TODO: Do something

   cvxMsgDisp("TemplateCmdEcho");

   cvxEchoEnd();
   }

void TemplateCmdTerm(void)
/*
DESCRIPTION:
   Terminate callback function of command form, the function is called when the command
is clicked on the CANCEL button to exit.
*/
   {
   // TODO: Do something

   cvxMsgDisp("TemplateCmdTerm");
   }

int TemplateCmdCb
(
   char* formName,    /* I: form name */
   int idField,       /* I: index of command parameter field */
   int idData         /* I: index of data container */
)
/*
DESCRIPTION:
   Callback function of command parameter field, the function is called when value
of the specify parameter field changes.
*/
   {
   // TODO: Do something

   cvxMsgDisp("TemplateCmdCb");
   return 0;
   }
