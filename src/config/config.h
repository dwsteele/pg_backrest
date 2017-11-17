/***********************************************************************************************************************************
Command and Option Configuration
***********************************************************************************************************************************/
#ifndef CONFIG_H
#define CONFIG_H

#include "common/type.h"
#include "config/define.h"

#include "config/config.auto.h"

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
int cfgCommandId(const char *commandName);
const char *cfgCommandName(ConfigCommand commandId);
ConfigDefineCommand cfgCommandDefIdFromId(ConfigCommand commandId);
unsigned int cfgCommandTotal();

int cfgOptionId(const char *optionName);
ConfigOption cfgOptionIdFromDefId(ConfigDefineOption optionDefId, int index);
int cfgOptionIndex(ConfigOption optionId);
int cfgOptionIndexTotal(ConfigOption optionDefId);
const char *cfgOptionName(ConfigOption optionId);
ConfigDefineOption cfgOptionDefIdFromId(ConfigOption optionId);
unsigned int cfgOptionTotal();

#endif
