#pragma once

#include "zwapi_cmd.h"
#include "zwapi_memory.h"
#include "zwapi_message.h"

/* Function declaration */
int RegisterCustomCommand(void);
int UnloadCustomCommand(void);
int RegisterTemplateCommand(void);
int UnloadTemplateCommand(void);
int RegisterFormCommand(void);
int UnloadFormCommand(void);
int RegisterPartInsertCommand(void);
int UnloadPartInsertCommand(void);
int InsertTestBolt(void);
int InsertBoltShape(void);
int InsertBearingSmall(void);
int InsertBearingLarge(void);
int InsertPartWithParams(const char* dir, const char* file, const char* part,
                         const char* params, int asShape, int asNewFile);
int RegisterPartExpressionCommands(void);
int UnloadPartExpressionCommands(void);
