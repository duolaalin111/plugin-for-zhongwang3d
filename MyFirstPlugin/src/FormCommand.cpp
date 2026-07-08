#include "zwapi_ui_form.h"
#include "zwapi_ui_tablewidget.h"
#include "zwapi_ui_treeview.h"
#include "..\inc\MyFirstPluginPr.h"

const char gFormName[] = "FormCmd";
const int gTableWidgetField = 101;
const int gTreeViewField = 102;

static int ShowForm(void);
static int FormCmd(int idIn, int* idOut);
static int FormCmdCb(char* form, int idField, int item);
static int TableWidgetCb(char* form, int idField, int idItem);
static int TreeViewCb(char* form, int idField, int idItem);

int RegisterFormCommand()
/*
DESCRIPTION:
   Register callback function of form command.
*/
   {
   /* Show form by entering command string "~ShowForm" */
   cvxCmdFunc("ShowForm", (void*)ShowForm, VX_CODE_GENERAL);

   /* Register callback function of form */
   cvxFormFunc(const_cast<char*>(gFormName), (void*)FormCmd, VX_CODE_GENERAL);
   cvxCmdCallback("FormCmdCb", (void*)FormCmdCb);
   return 0;
   }

int UnloadFormCommand()
/*
DESCRIPTION:
   Unload callback function of form command.
*/
   {
   cvxCmdFuncUnload("ShowForm");
   cvxCmdFuncUnload("FormCmd");
   cvxCmdFuncUnload("FormCmdCb");
   return 0;
   }

int ShowForm(void)
/*
DESCRIPTION:
   Custom command to show form.
*/
   {
   /* Create form if it has not already been created */
   int formState = cvxFormState(const_cast<char*>(gFormName));
   if (formState == 0 && cvxFormCreate(const_cast<char*>(gFormName), 0))
         return 1;

   /* Show form */
   cvxFormShow(const_cast<char*>(gFormName));
   return 0;
   }

int FormCmd
(
   int idIn,    /* I: index of data container */
   int* idOut   /* O: index of data container */
)
/*
DESCRIPTION:
   Execution function of command form.
*/
   {
   switch (idIn)
      {
      case VX_FORM_INIT:
      {
      // TODO: Do something

      cvxMsgDisp("Initialize form");
      }
      break;
      case VX_FORM_CANCEL:
      {
      // TODO: Do something

      cvxMsgDisp("Exit form");
      }
      break;
      case VX_FORM_RESET:
      {
      // TODO: Do something

      cvxMsgDisp("Reset form");
      }
      break;
      case VX_FORM_APPLY:
      {
      // TODO: Do something

      cvxMsgDisp("Apply form");
      }
      break;
      case VX_FORM_OKAY:
      {
      // TODO: Do something

      cvxMsgDisp("Exit form");
      }
      break;
      case VX_FORM_DEFAULT:
      {
      // TODO: Do something

      cvxMsgDisp("Default settings");
      }
      break;
      default:
      break;
      }
   return 0;
   }

int FormCmdCb
(
   char* form,   /* I: form name */
   int idField,  /* I: index of field */
   int idItem    /* I: index of item */
)
/*
DESCRIPTION:
   Callback function of command form.
*/
   {
   switch (idField)
      {
      case gTableWidgetField:
      {
      /* Table widget control event handling */
      TableWidgetCb(form, idField, idItem);
      }
      break;
      case gTreeViewField:
      {
      /* Tree view control event handling */
      TreeViewCb(form, idField, idItem);
      }
      break;
      default: break;
      }
   return 0;
   }

int TableWidgetCb
(
   char* form,   /* I: form name */
   int idField,  /* I: index of field */
   int idItem    /* I: index of item */
)
/*
DESCRIPTION:
   Table widget control event handling function.
*/
   {
   svxTableAt at = {};
   svxTableCallBackParam vsData = {};
   char szText[128] = "\0";

   cvxTableCallBackParamGet(form, idField, &vsData);
   /* if press ctrl, shift or alt key, do nothing */
   if (vsData.key.modifier != 0)
      goto F_END;

   switch (vsData.evt)
      {
      case VX_LEFT_CLICK:
      {
      // TODO: Do something

      cvxMsgDisp("Left-click");
      }
      break;
      case VX_RIGHT_CLICK:
      {
      // TODO: Do something

      cvxMsgDisp("Right-click");
      }
      break;
      case VX_LEFT_DOUBLE_CLICK:
      {
      // TODO: Do something

      cvxMsgDisp("Left-double-click");
      }
      break;
      case VX_KEY_UP_DOWN:
      {
      // TODO: Do something

      cvxMsgDisp("Key up and down");
      }
      break;
      case VX_HOT_KEY:
      {
      // TODO: Do something

      cvxMsgDisp("Hot-key");
      }
      break;
      case VX_SORT:
      {
      // TODO: Do something

      cvxMsgDisp("Sort");
      }
      break;
      case VX_DATA_CHANGED:
      {
      // TODO: Do something

      cvxMsgDisp("Data changed");
      }
      break;
      case VX_CELL_BUTTON_CLICKED:
      {
      // TODO: Do something

      cvxMsgDisp("Button in table cell clicked");
      }
      break;
      case VX_MOUSE_MOVE:
      {
      // TODO: Do something

      cvxMsgDisp("Mouse move");
      }
      break;
      case VX_CELL_CLOSE_EDITOR:
      {
      // TODO: Do something

      cvxMsgDisp("Table cell close editor");
      }
      break;
      default: break;
      }

F_END:
   cvxTableCallBackParamFree(&vsData);
   return 0;
   }

int TreeViewCb
(
   char* form,   /* I: form name */
   int idField,  /* I: index of field */
   int idItem    /* I: index of item */
)
/*
DESCRIPTION:
   Tree view control event handling function.
*/
   {
   char szText[256] = "\0";

   svxTreeCbParam treeCbData;
   if (cvxTreeGetCbParam(form, idField, idItem, &treeCbData)) return 0;

   evxTreeHitPos hitPos = treeCbData.uvxu.svxhit.pos;
   if (treeCbData.mode == VX_HOVER)
      return 0;
   switch (treeCbData.mode)
      {
      case VX_LEFT_DOWN:
      {
      // TODO: Do something

      cvxMsgDisp("Left-click down");
      }
      break;
      case VX_LEFT_UP:
      {
      // TODO: Do something

      cvxMsgDisp("Left-click up");
      }
      break;
      case VX_MIDDLE_DOWN: 
      {
      // TODO: Do something

      cvxMsgDisp("Middle-click down");
      }
      break;
      case VX_MIDDLE_UP:
      {
      // TODO: Do something

      cvxMsgDisp("Middle-click up");
      }
      break;
      case VX_RIGHT_DOWN: 
      {
      // TODO: Do something

      cvxMsgDisp("Right-click down");
      }
      break;
      case VX_RIGHT_UP:
      {
      // TODO: Do something

      cvxMsgDisp("Right-click up");
      }
      break;
      case VX_BOX_SELECTION_DONE:
      {
      // TODO: Do something

      cvxMsgDisp("Box selection over");
      }
      break;
      case VX_LABEL_CHANGED:
      {
      // TODO: Do something

      cvxMsgDisp("Label changed");
      }
      break;
      case VX_EDITOR_CLOSED:
      {
      // TODO: Do something

      cvxMsgDisp("Item editor closed");
      }
      break;
      case VX_DBL_LEFT:
      {
      // TODO: Do something

      cvxMsgDisp("Left-double-click");
      }
      break;
      case VX_DROP:
      {
      // TODO: Do something

      cvxMsgDisp("Drop after dragging");
      }
      break;
      case VX_KEY_PRESS: 
      {
      // TODO: Do something

      cvxMsgDisp("Key press");
      }
      break;
      case VX_HOVER: 
      {
      // TODO: Do something

      cvxMsgDisp("Normal hover");
      }
      break;
      case VX_HOVER_TIMEOUT: 
      {
      // TODO: Do something

      cvxMsgDisp("Hovered timeout");
      }
      break;
      case VX_ENTER: 
      {
      // TODO: Do something

      cvxMsgDisp("Enter");
      }
      break;
      case VX_LEAVE: 
      {
      // TODO: Do something

      cvxMsgDisp("Leave");
      }
      break;
      default: break;
      }
   return 0;
   }