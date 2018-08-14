/***********************************************************************************************************************************
Command and Option Configuration
***********************************************************************************************************************************/
#include <string.h>

#include "common/assert.h"
#include "common/debug.h"
#include "common/error.h"
#include "common/memContext.h"
#include "config/config.h"

/***********************************************************************************************************************************
Map command names to ids and vice versa.
***********************************************************************************************************************************/
typedef struct ConfigCommandData
{
    const char *name;

    bool lockRequired:1;
    unsigned int lockType:2;

    bool logFile:1;
    unsigned int logLevelDefault:4;
    unsigned int logLevelStdErrMax:4;
} ConfigCommandData;

#define CONFIG_COMMAND_LIST(...)                                                                                                   \
    {__VA_ARGS__};

#define CONFIG_COMMAND(...)                                                                                                        \
    {__VA_ARGS__},

#define CONFIG_COMMAND_LOCK_REQUIRED(lockRequiredParam)                                                                            \
    .lockRequired = lockRequiredParam,
#define CONFIG_COMMAND_LOCK_TYPE(lockTypeParam)                                                                                    \
    .lockType = lockTypeParam,
#define CONFIG_COMMAND_LOG_FILE(logFileParam)                                                                                      \
    .logFile = logFileParam,
#define CONFIG_COMMAND_LOG_LEVEL_DEFAULT(logLevelDefaultParam)                                                                     \
    .logLevelDefault = logLevelDefaultParam,
#define CONFIG_COMMAND_LOG_LEVEL_STDERR_MAX(logLevelStdErrMaxParam)                                                                \
    .logLevelStdErrMax = logLevelStdErrMaxParam,
#define CONFIG_COMMAND_NAME(nameParam)                                                                                             \
    .name = nameParam,

/***********************************************************************************************************************************
Map options names and indexes to option definitions.
***********************************************************************************************************************************/
typedef struct ConfigOptionData
{
    const char *name;

    unsigned int index:5;
    unsigned int defineId:7;
} ConfigOptionData;

#define CONFIG_OPTION_LIST(...)                                                                                                    \
    {__VA_ARGS__};

#define CONFIG_OPTION(...)                                                                                                         \
    {__VA_ARGS__},

#define CONFIG_OPTION_INDEX(indexParam)                                                                                            \
    .index = indexParam,
#define CONFIG_OPTION_NAME(nameParam)                                                                                              \
    .name = nameParam,
#define CONFIG_OPTION_DEFINE_ID(defineIdParam)                                                                                     \
    .defineId = defineIdParam,

/***********************************************************************************************************************************
Include the automatically generated configuration data
***********************************************************************************************************************************/
#include "config/config.auto.c"

/***********************************************************************************************************************************
Store the config memory context
***********************************************************************************************************************************/
static MemContext *configMemContext = NULL;

/***********************************************************************************************************************************
Store the current command

This is generally set by the command parser but can also be set by during execute to change commands, i.e. backup -> expire.
***********************************************************************************************************************************/
static ConfigCommand command = cfgCmdNone;

/***********************************************************************************************************************************
Store the location of the executable
***********************************************************************************************************************************/
static String *exe = NULL;

/***********************************************************************************************************************************
Was help requested for the command?
***********************************************************************************************************************************/
static bool help = false;

/***********************************************************************************************************************************
Store the list of parameters passed to the command
***********************************************************************************************************************************/
static StringList *paramList = NULL;

/***********************************************************************************************************************************
Map options names and indexes to option definitions.
***********************************************************************************************************************************/
typedef struct ConfigOptionValue
{
    bool valid:1;
    bool negate:1;
    bool reset:1;
    unsigned int source:2;

    Variant *value;
    Variant *defaultValue;
} ConfigOptionValue;

static ConfigOptionValue configOptionValue[CFG_OPTION_TOTAL];

/***********************************************************************************************************************************
Initialize or reinitialize the configuration data
***********************************************************************************************************************************/
void
cfgInit(void)
{
    FUNCTION_TEST_VOID();

    // Reset configuration
    command = cfgCmdNone;
    exe = NULL;
    help = false;
    paramList = NULL;
    memset(&configOptionValue, 0, sizeof(configOptionValue));

    // Free the old context
    if (configMemContext != NULL)
    {
        memContextFree(configMemContext);
        configMemContext = NULL;
    }

    // Allocate configuration context as a child of the top context
    MEM_CONTEXT_BEGIN(memContextTop())
    {
        MEM_CONTEXT_NEW_BEGIN("configuration")
        {
            configMemContext = MEM_CONTEXT_NEW();
        }
        MEM_CONTEXT_NEW_END();
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RESULT_VOID();
}

/***********************************************************************************************************************************
Get/set the current command
***********************************************************************************************************************************/
ConfigCommand
cfgCommand(void)
{
    FUNCTION_TEST_VOID();
    FUNCTION_TEST_RESULT(ENUM, command);
}

void
cfgCommandSet(ConfigCommand commandParam)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, commandParam);

        FUNCTION_TEST_ASSERT(commandParam <= cfgCmdNone);
    FUNCTION_TEST_END();

    command = commandParam;

    FUNCTION_TEST_RESULT_VOID();
}

/***********************************************************************************************************************************
Was help requested?
***********************************************************************************************************************************/
bool
cfgCommandHelp(void)
{
    FUNCTION_TEST_VOID();
    FUNCTION_TEST_RESULT(BOOL, help);
}

void
cfgCommandHelpSet(bool helpParam)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(BOOL, helpParam);
    FUNCTION_TEST_END();

    help = helpParam;

    FUNCTION_TEST_RESULT_VOID();
}

/***********************************************************************************************************************************
Get the define id for this command

This can be done by just casting the id to the define id.  There may be a time when they are not one to one and this function can
be modified to do the mapping.
***********************************************************************************************************************************/
ConfigDefineCommand
cfgCommandDefIdFromId(ConfigCommand commandId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, commandId);

        FUNCTION_TEST_ASSERT(commandId < cfgCmdNone);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(ENUM, (ConfigDefineCommand)commandId);
}

/***********************************************************************************************************************************
Get command id by name
***********************************************************************************************************************************/
ConfigCommand
cfgCommandId(const char *commandName)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRINGZ, commandName);

        FUNCTION_TEST_ASSERT(commandName != NULL);
    FUNCTION_TEST_END();

    ConfigCommand commandId;

    for (commandId = 0; commandId < cfgCmdNone; commandId++)
        if (strcmp(commandName, configCommandData[commandId].name) == 0)
            break;

    if (commandId == cfgCmdNone)
        THROW_FMT(AssertError, "invalid command '%s'", commandName);

    FUNCTION_TEST_RESULT(ENUM, commandId);
}

/***********************************************************************************************************************************
Get command name by id
***********************************************************************************************************************************/
const char *
cfgCommandName(ConfigCommand commandId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, commandId);

        FUNCTION_TEST_ASSERT(commandId < cfgCmdNone);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(STRINGZ, configCommandData[commandId].name);
}

/***********************************************************************************************************************************
Command parameters, if any
***********************************************************************************************************************************/
const StringList *
cfgCommandParam(void)
{
    FUNCTION_TEST_VOID();

    if (paramList == NULL)
    {
        MEM_CONTEXT_BEGIN(configMemContext)
        {
            paramList = strLstNew();
        }
        MEM_CONTEXT_END();
    }

    FUNCTION_TEST_RESULT(STRING_LIST, paramList);
}

void
cfgCommandParamSet(const StringList *param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING_LIST, param);

        FUNCTION_TEST_ASSERT(param != NULL);
    FUNCTION_TEST_END();

    MEM_CONTEXT_BEGIN(configMemContext)
    {
        paramList = strLstDup(param);
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RESULT_VOID();
}

/***********************************************************************************************************************************
Command parameters, if any
***********************************************************************************************************************************/
const String *
cfgExe(void)
{
    FUNCTION_TEST_VOID();
    FUNCTION_TEST_RESULT(STRING, exe);
}

void
cfgExeSet(const String *exeParam)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, exeParam);

        FUNCTION_TEST_ASSERT(exeParam != NULL);
    FUNCTION_TEST_END();

    MEM_CONTEXT_BEGIN(configMemContext)
    {
        exe = strDup(exeParam);
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RESULT_VOID();
}

/***********************************************************************************************************************************
Does this command require an immediate lock?
***********************************************************************************************************************************/
bool
cfgLockRequired(void)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_ASSERT(command != cfgCmdNone);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(BOOL, configCommandData[cfgCommand()].lockRequired);
}

/***********************************************************************************************************************************
Get the lock type required for this command
***********************************************************************************************************************************/
LockType
cfgLockType(void)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_ASSERT(command != cfgCmdNone);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(ENUM, (LockType)configCommandData[cfgCommand()].lockType);
}

/***********************************************************************************************************************************
Does this command log to a file?
***********************************************************************************************************************************/
bool
cfgLogFile(void)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_ASSERT(command != cfgCmdNone);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(BOOL, configCommandData[cfgCommand()].logFile);
}

/***********************************************************************************************************************************
Get default log level -- used for log messages that are common to all commands
***********************************************************************************************************************************/
LogLevel
cfgLogLevelDefault(void)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_ASSERT(command != cfgCmdNone);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(ENUM, (LogLevel)configCommandData[cfgCommand()].logLevelDefault);
}

/***********************************************************************************************************************************
Get max stderr log level -- used to suppress error output for higher log levels, e.g. local and remote commands
***********************************************************************************************************************************/
LogLevel
cfgLogLevelStdErrMax(void)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_ASSERT(command != cfgCmdNone);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(ENUM, (LogLevel)configCommandData[cfgCommand()].logLevelStdErrMax);
}

/***********************************************************************************************************************************
Get the option define for this option
***********************************************************************************************************************************/
ConfigDefineOption
cfgOptionDefIdFromId(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);

        FUNCTION_TEST_ASSERT(optionId < CFG_OPTION_TOTAL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(ENUM, configOptionData[optionId].defineId);
}

/***********************************************************************************************************************************
Get/set option default
***********************************************************************************************************************************/
const Variant *
cfgOptionDefault(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);

        FUNCTION_TEST_ASSERT(optionId < CFG_OPTION_TOTAL);
    FUNCTION_TEST_END();

    if (configOptionValue[optionId].defaultValue == NULL)
    {
        ConfigDefineOption optionDefId = cfgOptionDefIdFromId(optionId);

        if (cfgDefOptionDefault(cfgCommandDefIdFromId(cfgCommand()), optionDefId) != NULL)
        {
            MEM_CONTEXT_TEMP_BEGIN()
            {
                Variant *defaultValue = varNewStrZ(cfgDefOptionDefault(cfgCommandDefIdFromId(cfgCommand()), optionDefId));

                MEM_CONTEXT_BEGIN(configMemContext)
                {
                    switch (cfgDefOptionType(optionDefId))
                    {
                        case cfgDefOptTypeBoolean:
                        {
                            configOptionValue[optionId].defaultValue = varNewBool(varBoolForce(defaultValue));
                            break;
                        }

                        case cfgDefOptTypeFloat:
                        {
                            configOptionValue[optionId].defaultValue = varNewDbl(varDblForce(defaultValue));
                            break;
                        }

                        case cfgDefOptTypeInteger:
                        case cfgDefOptTypeSize:
                        {
                            configOptionValue[optionId].defaultValue = varNewInt64(varInt64Force(defaultValue));
                            break;
                        }

                        case cfgDefOptTypeString:
                            configOptionValue[optionId].defaultValue = varDup(defaultValue);
                            break;

                        default:                                    // {uncoverable - other types do not have defaults yet}
                            THROW_FMT(                              // {+uncoverable}
                                AssertError, "type for option '%s' does not support defaults", cfgOptionName(optionId));
                    }
                }
                MEM_CONTEXT_END();
            }
            MEM_CONTEXT_TEMP_END();
        }
    }

    FUNCTION_TEST_RESULT(VARIANT, configOptionValue[optionId].defaultValue);
}

void
cfgOptionDefaultSet(ConfigOption optionId, const Variant *defaultValue)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
        FUNCTION_TEST_PARAM(VARIANT, defaultValue);

        FUNCTION_TEST_ASSERT(optionId < CFG_OPTION_TOTAL);
    FUNCTION_TEST_END();

    MEM_CONTEXT_BEGIN(configMemContext)
    {
        if (configOptionValue[optionId].defaultValue != NULL)
            varFree(configOptionValue[optionId].defaultValue);

        configOptionValue[optionId].defaultValue = varDup(defaultValue);

        if (configOptionValue[optionId].source == cfgSourceDefault)
        {
            if (configOptionValue[optionId].value != NULL)
                varFree(configOptionValue[optionId].value);

            configOptionValue[optionId].value = varDup(defaultValue);
        }
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RESULT_VOID();
}

/***********************************************************************************************************************************
Get index for option
***********************************************************************************************************************************/
unsigned int
cfgOptionIndex(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);

        FUNCTION_TEST_ASSERT(optionId < CFG_OPTION_TOTAL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(UINT, configOptionData[optionId].index);
}

/***********************************************************************************************************************************
Get option id by name
***********************************************************************************************************************************/
int
cfgOptionId(const char *optionName)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRINGZ, optionName);

        FUNCTION_TEST_ASSERT(optionName != NULL);
    FUNCTION_TEST_END();

    int result = -1;

    for (ConfigOption optionId = 0; optionId < CFG_OPTION_TOTAL; optionId++)
        if (strcmp(optionName, configOptionData[optionId].name) == 0)
            result = optionId;

    FUNCTION_TEST_RESULT(INT, result);
}

/***********************************************************************************************************************************
Get total indexed values for option
***********************************************************************************************************************************/
unsigned int
cfgOptionIndexTotal(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);

        FUNCTION_TEST_ASSERT(optionId < CFG_OPTION_TOTAL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(UINT, cfgDefOptionIndexTotal(configOptionData[optionId].defineId));
}

/***********************************************************************************************************************************
Get the id for this option define
***********************************************************************************************************************************/
ConfigOption
cfgOptionIdFromDefId(ConfigDefineOption optionDefId, unsigned int index)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionDefId);
        FUNCTION_TEST_PARAM(UINT, index);

        FUNCTION_TEST_ASSERT(optionDefId < cfgDefOptionTotal());
        FUNCTION_TEST_ASSERT(index < cfgDefOptionIndexTotal(optionDefId));
    FUNCTION_TEST_END();

    // Search for the option
    ConfigOption optionId;

    for (optionId = 0; optionId < CFG_OPTION_TOTAL; optionId++)     // {uncoverable - only a bug in code gen lets this loop end}
        if (configOptionData[optionId].defineId == optionDefId)
            break;

    // If the mapping is not found then there is a bug in the code generator
    ASSERT_DEBUG(optionId != CFG_OPTION_TOTAL);

    // Return with original index
    FUNCTION_TEST_RESULT(ENUM, optionId + index);
}

/***********************************************************************************************************************************
Get option name by id
***********************************************************************************************************************************/
const char *
cfgOptionName(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);

        FUNCTION_TEST_ASSERT(optionId < CFG_OPTION_TOTAL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(STRINGZ, configOptionData[optionId].name);
}

/***********************************************************************************************************************************
Was the option negated?
***********************************************************************************************************************************/
bool
cfgOptionNegate(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);

        FUNCTION_TEST_ASSERT(optionId < CFG_OPTION_TOTAL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(BOOL, configOptionValue[optionId].negate);
}

void
cfgOptionNegateSet(ConfigOption optionId, bool negate)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
        FUNCTION_TEST_PARAM(BOOL, negate);

        FUNCTION_TEST_ASSERT(optionId < CFG_OPTION_TOTAL);
    FUNCTION_TEST_END();

    configOptionValue[optionId].negate = negate;

    FUNCTION_TEST_RESULT_VOID();
}

/***********************************************************************************************************************************
Was the option reset?
***********************************************************************************************************************************/
bool
cfgOptionReset(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);

        FUNCTION_TEST_ASSERT(optionId < CFG_OPTION_TOTAL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(BOOL, configOptionValue[optionId].reset);
}

void
cfgOptionResetSet(ConfigOption optionId, bool reset)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
        FUNCTION_TEST_PARAM(BOOL, reset);

        FUNCTION_TEST_ASSERT(optionId < CFG_OPTION_TOTAL);
    FUNCTION_TEST_END();

    configOptionValue[optionId].reset = reset;

    FUNCTION_TEST_RESULT_VOID();
}

/***********************************************************************************************************************************
Get and set config options
***********************************************************************************************************************************/
const Variant *
cfgOption(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);

        FUNCTION_TEST_ASSERT(optionId < CFG_OPTION_TOTAL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(VARIANT, configOptionValue[optionId].value);
}

bool
cfgOptionBool(ConfigOption optionId)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(ENUM, optionId);

        FUNCTION_DEBUG_ASSERT(optionId < CFG_OPTION_TOTAL);
        FUNCTION_DEBUG_ASSERT(varType(configOptionValue[optionId].value) == varTypeBool);
    FUNCTION_DEBUG_END();

    FUNCTION_DEBUG_RESULT(BOOL, varBool(configOptionValue[optionId].value));
}

double
cfgOptionDbl(ConfigOption optionId)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(ENUM, optionId);

        FUNCTION_DEBUG_ASSERT(optionId < CFG_OPTION_TOTAL);
        FUNCTION_DEBUG_ASSERT(varType(configOptionValue[optionId].value) == varTypeDouble);
    FUNCTION_DEBUG_END();

    FUNCTION_DEBUG_RESULT(DOUBLE, varDbl(configOptionValue[optionId].value));
}

int
cfgOptionInt(ConfigOption optionId)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(ENUM, optionId);

        FUNCTION_DEBUG_ASSERT(optionId < CFG_OPTION_TOTAL);
        FUNCTION_DEBUG_ASSERT(varType(configOptionValue[optionId].value) == varTypeInt64);
    FUNCTION_DEBUG_END();

    FUNCTION_DEBUG_RESULT(INT, varIntForce(configOptionValue[optionId].value));
}

int64_t
cfgOptionInt64(ConfigOption optionId)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(ENUM, optionId);

        FUNCTION_DEBUG_ASSERT(optionId < CFG_OPTION_TOTAL);
        FUNCTION_DEBUG_ASSERT(varType(configOptionValue[optionId].value) == varTypeInt64);
    FUNCTION_DEBUG_END();

    FUNCTION_DEBUG_RESULT(INT64, varInt64(configOptionValue[optionId].value));
}

const KeyValue *
cfgOptionKv(ConfigOption optionId)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(ENUM, optionId);

        FUNCTION_DEBUG_ASSERT(optionId < CFG_OPTION_TOTAL);
        FUNCTION_DEBUG_ASSERT(varType(configOptionValue[optionId].value) == varTypeKeyValue);
    FUNCTION_DEBUG_END();

    FUNCTION_DEBUG_RESULT(KEY_VALUE, varKv(configOptionValue[optionId].value));
}

const VariantList *
cfgOptionLst(ConfigOption optionId)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(ENUM, optionId);

        FUNCTION_DEBUG_ASSERT(optionId < CFG_OPTION_TOTAL);
        FUNCTION_DEBUG_ASSERT(
            configOptionValue[optionId].value == NULL || varType(configOptionValue[optionId].value) == varTypeVariantList);
    FUNCTION_DEBUG_END();

    if (configOptionValue[optionId].value == NULL)
    {
        MEM_CONTEXT_BEGIN(configMemContext)
        {
            configOptionValue[optionId].value = varNewVarLst(varLstNew());
        }
        MEM_CONTEXT_END();
    }

    FUNCTION_DEBUG_RESULT(VARIANT_LIST, varVarLst(configOptionValue[optionId].value));
}

const String *
cfgOptionStr(ConfigOption optionId)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(ENUM, optionId);

        FUNCTION_DEBUG_ASSERT(optionId < CFG_OPTION_TOTAL);
        FUNCTION_DEBUG_ASSERT(
            configOptionValue[optionId].value == NULL || varType(configOptionValue[optionId].value) == varTypeString);
    FUNCTION_DEBUG_END();

    const String *result = NULL;

    if (configOptionValue[optionId].value != NULL)
        result = varStr(configOptionValue[optionId].value);

    FUNCTION_DEBUG_RESULT(CONST_STRING, result);
}

void
cfgOptionSet(ConfigOption optionId, ConfigSource source, const Variant *value)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
        FUNCTION_TEST_PARAM(ENUM, source);
        FUNCTION_TEST_PARAM(VARIANT, value);

        FUNCTION_TEST_ASSERT(optionId < CFG_OPTION_TOTAL);
    FUNCTION_TEST_END();

    MEM_CONTEXT_BEGIN(configMemContext)
    {
        // Set the source
        configOptionValue[optionId].source = source;

        // Store old value
        Variant *valueOld = configOptionValue[optionId].value;

        // Only set value if it is not null
        if (value != NULL)
        {
            switch (cfgDefOptionType(cfgOptionDefIdFromId(optionId)))
            {
                case cfgDefOptTypeBoolean:
                {
                    if (varType(value) == varTypeBool)
                        configOptionValue[optionId].value = varDup(value);
                    else
                        configOptionValue[optionId].value = varNewBool(varBoolForce(value));

                    break;
                }

                case cfgDefOptTypeFloat:
                {
                    if (varType(value) == varTypeDouble)
                        configOptionValue[optionId].value = varDup(value);
                    else
                        configOptionValue[optionId].value = varNewDbl(varDblForce(value));

                    break;
                }

                case cfgDefOptTypeInteger:
                case cfgDefOptTypeSize:
                {
                    if (varType(value) == varTypeInt64)
                        configOptionValue[optionId].value = varDup(value);
                    else
                        configOptionValue[optionId].value = varNewInt64(varInt64Force(value));

                    break;
                }

                case cfgDefOptTypeHash:
                {
                    if (varType(value) == varTypeKeyValue)
                        configOptionValue[optionId].value = varDup(value);
                    else
                        THROW_FMT(AssertError, "option '%s' must be set with KeyValue variant", cfgOptionName(optionId));

                    break;
                }

                case cfgDefOptTypeList:
                {
                    if (varType(value) == varTypeVariantList)
                        configOptionValue[optionId].value = varDup(value);
                    else
                        THROW_FMT(AssertError, "option '%s' must be set with VariantList variant", cfgOptionName(optionId));

                    break;
                }

                case cfgDefOptTypeString:
                    if (varType(value) == varTypeString)
                        configOptionValue[optionId].value = varDup(value);
                    else
                        THROW_FMT(AssertError, "option '%s' must be set with String variant", cfgOptionName(optionId));

                    break;
            }
        }
        else
            configOptionValue[optionId].value = NULL;

        // Free old value
        if (valueOld != NULL)
            varFree(valueOld);
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RESULT_VOID();
}

/***********************************************************************************************************************************
How was the option set (default, param, config)?
***********************************************************************************************************************************/
ConfigSource
cfgOptionSource(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);

        FUNCTION_TEST_ASSERT(optionId < CFG_OPTION_TOTAL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(ENUM, configOptionValue[optionId].source);
}

/***********************************************************************************************************************************
Is the option valid for the command and set?
***********************************************************************************************************************************/
bool
cfgOptionTest(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);

        FUNCTION_TEST_ASSERT(optionId < CFG_OPTION_TOTAL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(BOOL, cfgOptionValid(optionId) && configOptionValue[optionId].value != NULL);
}

/***********************************************************************************************************************************
Is the option valid for this command?
***********************************************************************************************************************************/
bool
cfgOptionValid(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);

        FUNCTION_TEST_ASSERT(optionId < CFG_OPTION_TOTAL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(BOOL, configOptionValue[optionId].valid);
}

void
cfgOptionValidSet(ConfigOption optionId, bool valid)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
        FUNCTION_TEST_PARAM(BOOL, valid);

        FUNCTION_TEST_ASSERT(optionId < CFG_OPTION_TOTAL);
    FUNCTION_TEST_END();

    configOptionValue[optionId].valid = valid;

    FUNCTION_TEST_RESULT_VOID();
}
