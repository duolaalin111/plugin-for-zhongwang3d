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
