/***********************************************************************************************************************************
Command and Option Configuration
***********************************************************************************************************************************/
#include "build.auto.h"

#include <string.h>

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

    bool internal:1;
    bool lockRequired:1;
    bool lockRemoteRequired:1;
    unsigned int lockType:2;

    bool logFile:1;
    unsigned int logLevelDefault:4;

    bool parameterAllowed:1;
} ConfigCommandData;

#define CONFIG_COMMAND_LIST(...)                                                                                                   \
    {__VA_ARGS__};

#define CONFIG_COMMAND(...)                                                                                                        \
    {__VA_ARGS__},

#define CONFIG_COMMAND_INTERNAL(internalParam)                                                                                     \
    .internal = internalParam,
#define CONFIG_COMMAND_LOCK_REQUIRED(lockRequiredParam)                                                                            \
    .lockRequired = lockRequiredParam,
#define CONFIG_COMMAND_LOCK_REMOTE_REQUIRED(lockRemoteRequiredParam)                                                               \
    .lockRemoteRequired = lockRemoteRequiredParam,
#define CONFIG_COMMAND_LOCK_TYPE(lockTypeParam)                                                                                    \
    .lockType = lockTypeParam,
#define CONFIG_COMMAND_LOG_FILE(logFileParam)                                                                                      \
    .logFile = logFileParam,
#define CONFIG_COMMAND_LOG_LEVEL_DEFAULT(logLevelDefaultParam)                                                                     \
    .logLevelDefault = logLevelDefaultParam,
#define CONFIG_COMMAND_NAME(nameParam)                                                                                             \
    .name = nameParam,
#define CONFIG_COMMAND_PARAMETER_ALLOWED(parameterAllowedParam)                                                                    \
    .parameterAllowed = parameterAllowedParam,

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
Static data for the currently loaded configuration
***********************************************************************************************************************************/
static struct ConfigStatic
{
    MemContext *memContext;                                         // Mem context for config data (child of top context)

    // Generally set by the command parser but can also be set by during execute to change commands, i.e. backup -> expire
    ConfigCommand command;                                          // Current command
    ConfigCommandRole commandRole;                                  // Current command role

    String *exe;                                                    // Location of the executable
    bool help;                                                      // Was help requested for the command?
    StringList *paramList;                                          // Parameters passed to the command (if any)

    // Map options names and indexes to option definitions
    struct
    {
        bool valid:1;                                               // Is option valid for current command?
        bool negate:1;                                              // Is the option negated?
        bool reset:1;                                               // Is the option reset?
        unsigned int source:2;                                      // Where the option came from, i.e. ConfigSource enum

        Variant *value;                                             // Value
        Variant *defaultValue;                                      // Default value
    } option[CFG_OPTION_TOTAL];
} configStatic;

/***********************************************************************************************************************************
Initialize or reinitialize the configuration data
***********************************************************************************************************************************/
void
cfgInit(void)
{
    FUNCTION_TEST_VOID();

    // Free the old context
    if (configStatic.memContext != NULL)
        memContextFree(configStatic.memContext);

    // Initialize config data
    configStatic = (struct ConfigStatic){.command = cfgCmdNone};

    // Allocate configuration context as a child of the top context
    MEM_CONTEXT_BEGIN(memContextTop())
    {
        MEM_CONTEXT_NEW_BEGIN("configuration")
        {
            configStatic.memContext = MEM_CONTEXT_NEW();
        }
        MEM_CONTEXT_NEW_END();
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Get/set the current command
***********************************************************************************************************************************/
ConfigCommand
cfgCommand(void)
{
    FUNCTION_TEST_VOID();
    FUNCTION_TEST_RETURN(configStatic.command);
}

ConfigCommandRole
cfgCommandRole(void)
{
    FUNCTION_TEST_VOID();
    FUNCTION_TEST_RETURN(configStatic.commandRole);
}

void
cfgCommandSet(ConfigCommand commandId, ConfigCommandRole commandRoleId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, commandId);
        FUNCTION_TEST_PARAM(ENUM, commandRoleId);
    FUNCTION_TEST_END();

    ASSERT(commandId <= cfgCmdNone);

    configStatic.command = commandId;
    configStatic.commandRole = commandRoleId;

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Was help requested?
***********************************************************************************************************************************/
bool
cfgCommandHelp(void)
{
    FUNCTION_TEST_VOID();
    FUNCTION_TEST_RETURN(configStatic.help);
}

void
cfgCommandHelpSet(bool help)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(BOOL, help);
    FUNCTION_TEST_END();

    configStatic.help = help;

    FUNCTION_TEST_RETURN_VOID();
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
    FUNCTION_TEST_END();

    ASSERT(commandId < cfgCmdNone);

    FUNCTION_TEST_RETURN((ConfigDefineCommand)commandId);
}

/**********************************************************************************************************************************/
ConfigCommand
cfgCommandId(const char *commandName, bool error)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRINGZ, commandName);
        FUNCTION_TEST_PARAM(BOOL, error);
    FUNCTION_TEST_END();

    ASSERT(commandName != NULL);

    ConfigCommand commandId;

    for (commandId = 0; commandId < cfgCmdNone; commandId++)
        if (strcmp(commandName, configCommandData[commandId].name) == 0)
            break;

    if (commandId == cfgCmdNone && error)
        THROW_FMT(AssertError, "invalid command '%s'", commandName);

    FUNCTION_TEST_RETURN(commandId);
}

/***********************************************************************************************************************************
Get command name by id
***********************************************************************************************************************************/
const char *
cfgCommandName(ConfigCommand commandId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, commandId);
    FUNCTION_TEST_END();

    ASSERT(commandId < cfgCmdNone);

    FUNCTION_TEST_RETURN(configCommandData[commandId].name);
}

String *
cfgCommandRoleNameParam(ConfigCommand commandId, ConfigCommandRole commandRoleId, const String *separator)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, commandId);
        FUNCTION_TEST_PARAM(ENUM, commandRoleId);
        FUNCTION_TEST_PARAM(STRING, separator);
    FUNCTION_TEST_END();

    String *result = strNew(cfgCommandName(commandId));

    if (commandRoleId != cfgCmdRoleDefault)
        strCatFmt(result, "%s%s", strPtr(separator), strPtr(cfgCommandRoleStr(commandRoleId)));

    FUNCTION_TEST_RETURN(result);
}

String *
cfgCommandRoleName(void)
{
    FUNCTION_TEST_VOID();

    FUNCTION_TEST_RETURN(cfgCommandRoleNameParam(cfgCommand(), cfgCommandRole(), COLON_STR));
}

/***********************************************************************************************************************************
Command parameters, if any
***********************************************************************************************************************************/
const StringList *
cfgCommandParam(void)
{
    FUNCTION_TEST_VOID();

    if (configStatic.paramList == NULL)
    {
        MEM_CONTEXT_BEGIN(configStatic.memContext)
        {
            configStatic.paramList = strLstNew();
        }
        MEM_CONTEXT_END();
    }

    FUNCTION_TEST_RETURN(configStatic.paramList);
}

void
cfgCommandParamSet(const StringList *param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING_LIST, param);
    FUNCTION_TEST_END();

    ASSERT(param != NULL);

    MEM_CONTEXT_BEGIN(configStatic.memContext)
    {
        configStatic.paramList = strLstDup(param);
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RETURN_VOID();
}

/**********************************************************************************************************************************/
STRING_STATIC(CONFIG_COMMAND_ROLE_ASYNC_STR,                        CONFIG_COMMAND_ROLE_ASYNC);
STRING_STATIC(CONFIG_COMMAND_ROLE_LOCAL_STR,                        CONFIG_COMMAND_ROLE_LOCAL);
STRING_STATIC(CONFIG_COMMAND_ROLE_REMOTE_STR,                       CONFIG_COMMAND_ROLE_REMOTE);

ConfigCommandRole
cfgCommandRoleEnum(const String *commandRole)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, commandRole);
    FUNCTION_TEST_END();

    if (commandRole == NULL)
        FUNCTION_TEST_RETURN(cfgCmdRoleDefault);
    else if (strEq(commandRole, CONFIG_COMMAND_ROLE_ASYNC_STR))
        FUNCTION_TEST_RETURN(cfgCmdRoleAsync);
    else if (strEq(commandRole, CONFIG_COMMAND_ROLE_LOCAL_STR))
        FUNCTION_TEST_RETURN(cfgCmdRoleLocal);
    else if (strEq(commandRole, CONFIG_COMMAND_ROLE_REMOTE_STR))
        FUNCTION_TEST_RETURN(cfgCmdRoleRemote);

    THROW_FMT(CommandInvalidError, "invalid command role '%s'", strPtr(commandRole));
}

const String *
cfgCommandRoleStr(ConfigCommandRole commandRole)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, commandRole);
    FUNCTION_TEST_END();

    const String *result = NULL;

    switch (commandRole)
    {
        case cfgCmdRoleDefault:
            break;

        case cfgCmdRoleAsync:
        {
            result = CONFIG_COMMAND_ROLE_ASYNC_STR;
            break;
        }

        case cfgCmdRoleLocal:
        {
            result = CONFIG_COMMAND_ROLE_LOCAL_STR;
            break;
        }

        case cfgCmdRoleRemote:
        {
            result = CONFIG_COMMAND_ROLE_REMOTE_STR;
            break;
        }
    }

    FUNCTION_TEST_RETURN(result);
}

/***********************************************************************************************************************************
Command parameters, if any
***********************************************************************************************************************************/
const String *
cfgExe(void)
{
    FUNCTION_TEST_VOID();
    FUNCTION_TEST_RETURN(configStatic.exe);
}

void
cfgExeSet(const String *exe)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, exe);
    FUNCTION_TEST_END();

    ASSERT(exe != NULL);

    MEM_CONTEXT_BEGIN(configStatic.memContext)
    {
        configStatic.exe = strDup(exe);
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Is this command internal-only?
***********************************************************************************************************************************/
bool
cfgCommandInternal(ConfigCommand commandId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, commandId);
    FUNCTION_TEST_END();

    ASSERT(commandId < cfgCmdNone);

    FUNCTION_TEST_RETURN(configCommandData[commandId].internal);
}

/***********************************************************************************************************************************
Does this command require an immediate lock?
***********************************************************************************************************************************/
bool
cfgLockRequired(void)
{
    FUNCTION_TEST_VOID();

    ASSERT(configStatic.command != cfgCmdNone);

    // Local roles never take a lock and the remote role has special logic for locking
    FUNCTION_TEST_RETURN(
        // If a lock is required for the command and the role is default
        (configCommandData[cfgCommand()].lockRequired && cfgCommandRole() == cfgCmdRoleDefault) ||
        // Or any command when the role is async
        cfgCommandRole() == cfgCmdRoleAsync);
}

/***********************************************************************************************************************************
Does the command require an immediate lock?
***********************************************************************************************************************************/
bool
cfgLockRemoteRequired(void)
{
    FUNCTION_TEST_VOID();

    FUNCTION_TEST_RETURN(configCommandData[cfgCommand()].lockRemoteRequired);
}

/***********************************************************************************************************************************
Get the lock type required for this command
***********************************************************************************************************************************/
LockType
cfgLockType(void)
{
    FUNCTION_TEST_VOID();

    ASSERT(configStatic.command != cfgCmdNone);

    FUNCTION_TEST_RETURN((LockType)configCommandData[cfgCommand()].lockType);
}

/***********************************************************************************************************************************
Does this command log to a file?
***********************************************************************************************************************************/
bool
cfgLogFile(void)
{
    FUNCTION_TEST_VOID();

    ASSERT(configStatic.command != cfgCmdNone);

    FUNCTION_TEST_RETURN(
        // If the command always logs to a file
        configCommandData[cfgCommand()].logFile ||
        // Or log-level-file was explicitly set as a param/env var
        cfgOptionSource(cfgOptLogLevelFile) == cfgSourceParam ||
        // Or the role is async
        cfgCommandRole() == cfgCmdRoleAsync);
}

/***********************************************************************************************************************************
Get default log level -- used for log messages that are common to all commands
***********************************************************************************************************************************/
LogLevel
cfgLogLevelDefault(void)
{
    FUNCTION_TEST_VOID();

    ASSERT(configStatic.command != cfgCmdNone);

    FUNCTION_TEST_RETURN((LogLevel)configCommandData[cfgCommand()].logLevelDefault);
}

/***********************************************************************************************************************************
Does this command allow parameters?
***********************************************************************************************************************************/
bool
cfgParameterAllowed(void)
{
    FUNCTION_TEST_VOID();

    ASSERT(configStatic.command != cfgCmdNone);

    FUNCTION_TEST_RETURN(configCommandData[cfgCommand()].parameterAllowed);
}

/***********************************************************************************************************************************
Get the option define for this option
***********************************************************************************************************************************/
ConfigDefineOption
cfgOptionDefIdFromId(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
    FUNCTION_TEST_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);

    FUNCTION_TEST_RETURN(configOptionData[optionId].defineId);
}

/***********************************************************************************************************************************
Get/set option default

Option defaults are generally not set in advance because the vast majority of them are never used.  It is more efficient to generate
them when they are requested.

Some defaults are (e.g. the exe path) are set at runtime.
***********************************************************************************************************************************/
static Variant *
cfgOptionDefaultValue(ConfigDefineOption optionDefId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionDefId);
    FUNCTION_TEST_END();

    Variant *result;
    Variant *defaultValue = varNewStrZ(cfgDefOptionDefault(cfgCommandDefIdFromId(cfgCommand()), optionDefId));

    switch (cfgDefOptionType(optionDefId))
    {
        case cfgDefOptTypeBoolean:
        {
            result = varNewBool(varBoolForce(defaultValue));
            break;
        }

        case cfgDefOptTypeFloat:
        {
            result = varNewDbl(varDblForce(defaultValue));
            break;
        }

        case cfgDefOptTypeInteger:
        case cfgDefOptTypeSize:
        {
            result = varNewInt64(varInt64Force(defaultValue));
            break;
        }

        case cfgDefOptTypePath:
        case cfgDefOptTypeString:
            result = varDup(defaultValue);
            break;

        default:
            THROW_FMT(AssertError, "default value not available for option type %d", cfgDefOptionType(optionDefId));
    }

    FUNCTION_TEST_RETURN(result);
}

const Variant *
cfgOptionDefault(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
    FUNCTION_TEST_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);

    if (configStatic.option[optionId].defaultValue == NULL)
    {
        ConfigDefineOption optionDefId = cfgOptionDefIdFromId(optionId);

        if (cfgDefOptionDefault(cfgCommandDefIdFromId(cfgCommand()), optionDefId) != NULL)
        {
            MEM_CONTEXT_BEGIN(configStatic.memContext)
            {
                configStatic.option[optionId].defaultValue = cfgOptionDefaultValue(optionDefId);
            }
            MEM_CONTEXT_END();
        }
    }

    FUNCTION_TEST_RETURN(configStatic.option[optionId].defaultValue);
}

void
cfgOptionDefaultSet(ConfigOption optionId, const Variant *defaultValue)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
        FUNCTION_TEST_PARAM(VARIANT, defaultValue);
    FUNCTION_TEST_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);

    MEM_CONTEXT_BEGIN(configStatic.memContext)
    {
        if (configStatic.option[optionId].defaultValue != NULL)
            varFree(configStatic.option[optionId].defaultValue);

        configStatic.option[optionId].defaultValue = varDup(defaultValue);

        if (configStatic.option[optionId].source == cfgSourceDefault)
        {
            if (configStatic.option[optionId].value != NULL)
                varFree(configStatic.option[optionId].value);

            configStatic.option[optionId].value = varDup(defaultValue);
        }
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Parse a host option and extract the host and port (if it exists)
***********************************************************************************************************************************/
String *
cfgOptionHostPort(ConfigOption optionId, unsigned int *port)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
        FUNCTION_TEST_PARAM_P(UINT, port);
    FUNCTION_TEST_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);
    ASSERT(port != NULL);

    String *result = NULL;

    // Proceed if option is valid and has a value
    if (cfgOptionTest(optionId))
    {
        MEM_CONTEXT_TEMP_BEGIN()
        {
            const String *host = cfgOptionStr(optionId);

            // If the host contains a colon then it has a port appended
            if (strChr(host, ':') != -1)
            {
                const StringList *hostPart = strLstNewSplitZ(host, ":");

                // More than one colon is invalid
                if (strLstSize(hostPart) > 2)
                {
                    THROW_FMT(
                        OptionInvalidError,
                        "'%s' is not valid for option '%s'"
                            "\nHINT: is more than one port specified?",
                        strPtr(host), cfgOptionName(optionId));
                }

                // Set the host
                MEM_CONTEXT_PRIOR_BEGIN()
                {
                    result = strDup(strLstGet(hostPart, 0));
                }
                MEM_CONTEXT_PRIOR_END();

                // Set the port and error if it is not a positive integer
                TRY_BEGIN()
                {
                    *port = cvtZToUInt(strPtr(strLstGet(hostPart, 1)));
                }
                CATCH(FormatError)
                {
                    THROW_FMT(
                        OptionInvalidError,
                        "'%s' is not valid for option '%s'"
                            "\nHINT: port is not a positive integer.",
                        strPtr(host), cfgOptionName(optionId));
                }
                TRY_END();
            }
            // Else there is no port and just copy the host
            else
            {
                MEM_CONTEXT_PRIOR_BEGIN()
                {
                    result = strDup(host);
                }
                MEM_CONTEXT_PRIOR_END();
            }
        }
        MEM_CONTEXT_TEMP_END();
    }

    FUNCTION_TEST_RETURN(result);
}

/***********************************************************************************************************************************
Get index for option
***********************************************************************************************************************************/
unsigned int
cfgOptionIndex(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
    FUNCTION_TEST_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);

    FUNCTION_TEST_RETURN(configOptionData[optionId].index);
}

/***********************************************************************************************************************************
Get option id by name
***********************************************************************************************************************************/
int
cfgOptionId(const char *optionName)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRINGZ, optionName);
    FUNCTION_TEST_END();

    ASSERT(optionName != NULL);

    int result = -1;

    for (ConfigOption optionId = 0; optionId < CFG_OPTION_TOTAL; optionId++)
        if (strcmp(optionName, configOptionData[optionId].name) == 0)
            result = optionId;

    FUNCTION_TEST_RETURN(result);
}

/***********************************************************************************************************************************
Get total indexed values for option
***********************************************************************************************************************************/
unsigned int
cfgOptionIndexTotal(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
    FUNCTION_TEST_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);

    FUNCTION_TEST_RETURN(cfgDefOptionIndexTotal(configOptionData[optionId].defineId));
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
    FUNCTION_TEST_END();

    // Search for the option
    ConfigOption optionId;

    for (optionId = 0; optionId < CFG_OPTION_TOTAL; optionId++)
    {
        if (configOptionData[optionId].defineId == optionDefId)
            break;
    }

    // If the mapping is not found then there is a bug in the code generator
    ASSERT(optionId != CFG_OPTION_TOTAL);

    // Make sure the index is valid
    ASSERT(index < cfgDefOptionIndexTotal(optionDefId));

    // Return with original index
    FUNCTION_TEST_RETURN(optionId + index);
}

/***********************************************************************************************************************************
Get option name by id
***********************************************************************************************************************************/
const char *
cfgOptionName(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
    FUNCTION_TEST_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);

    FUNCTION_TEST_RETURN(configOptionData[optionId].name);
}

/***********************************************************************************************************************************
Was the option negated?
***********************************************************************************************************************************/
bool
cfgOptionNegate(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
    FUNCTION_TEST_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);

    FUNCTION_TEST_RETURN(configStatic.option[optionId].negate);
}

void
cfgOptionNegateSet(ConfigOption optionId, bool negate)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
        FUNCTION_TEST_PARAM(BOOL, negate);
    FUNCTION_TEST_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);

    configStatic.option[optionId].negate = negate;

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Was the option reset?
***********************************************************************************************************************************/
bool
cfgOptionReset(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
    FUNCTION_TEST_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);

    FUNCTION_TEST_RETURN(configStatic.option[optionId].reset);
}

void
cfgOptionResetSet(ConfigOption optionId, bool reset)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
        FUNCTION_TEST_PARAM(BOOL, reset);
    FUNCTION_TEST_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);

    configStatic.option[optionId].reset = reset;

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Get and set config options
***********************************************************************************************************************************/
const Variant *
cfgOption(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
    FUNCTION_TEST_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);

    FUNCTION_TEST_RETURN(configStatic.option[optionId].value);
}

bool
cfgOptionBool(ConfigOption optionId)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(ENUM, optionId);
    FUNCTION_LOG_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);
    ASSERT(varType(configStatic.option[optionId].value) == varTypeBool);

    FUNCTION_LOG_RETURN(BOOL, varBool(configStatic.option[optionId].value));
}

double
cfgOptionDbl(ConfigOption optionId)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(ENUM, optionId);
    FUNCTION_LOG_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);
    ASSERT(varType(configStatic.option[optionId].value) == varTypeDouble);

    FUNCTION_LOG_RETURN(DOUBLE, varDbl(configStatic.option[optionId].value));
}

int
cfgOptionInt(ConfigOption optionId)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(ENUM, optionId);
    FUNCTION_LOG_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);
    ASSERT(varType(configStatic.option[optionId].value) == varTypeInt64);

    FUNCTION_LOG_RETURN(INT, varIntForce(configStatic.option[optionId].value));
}

int64_t
cfgOptionInt64(ConfigOption optionId)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(ENUM, optionId);
    FUNCTION_LOG_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);
    ASSERT(varType(configStatic.option[optionId].value) == varTypeInt64);

    FUNCTION_LOG_RETURN(INT64, varInt64(configStatic.option[optionId].value));
}

unsigned int
cfgOptionUInt(ConfigOption optionId)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(ENUM, optionId);
    FUNCTION_LOG_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);
    ASSERT(varType(configStatic.option[optionId].value) == varTypeInt64);

    FUNCTION_LOG_RETURN(UINT, varUIntForce(configStatic.option[optionId].value));
}

uint64_t
cfgOptionUInt64(ConfigOption optionId)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(ENUM, optionId);
    FUNCTION_LOG_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);
    ASSERT(varType(configStatic.option[optionId].value) == varTypeInt64);

    FUNCTION_LOG_RETURN(UINT64, varUInt64Force(configStatic.option[optionId].value));
}

const KeyValue *
cfgOptionKv(ConfigOption optionId)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(ENUM, optionId);
    FUNCTION_LOG_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);
    ASSERT(varType(configStatic.option[optionId].value) == varTypeKeyValue);

    FUNCTION_LOG_RETURN(KEY_VALUE, varKv(configStatic.option[optionId].value));
}

const VariantList *
cfgOptionLst(ConfigOption optionId)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(ENUM, optionId);
    FUNCTION_LOG_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);
    ASSERT(configStatic.option[optionId].value == NULL || varType(configStatic.option[optionId].value) == varTypeVariantList);

    if (configStatic.option[optionId].value == NULL)
    {
        MEM_CONTEXT_BEGIN(configStatic.memContext)
        {
            configStatic.option[optionId].value = varNewVarLst(varLstNew());
        }
        MEM_CONTEXT_END();
    }

    FUNCTION_LOG_RETURN(VARIANT_LIST, varVarLst(configStatic.option[optionId].value));
}

const String *
cfgOptionStr(ConfigOption optionId)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(ENUM, optionId);
    FUNCTION_LOG_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);
    ASSERT(configStatic.option[optionId].value == NULL || varType(configStatic.option[optionId].value) == varTypeString);

    const String *result = NULL;

    if (configStatic.option[optionId].value != NULL)
        result = varStr(configStatic.option[optionId].value);

    FUNCTION_LOG_RETURN_CONST(STRING, result);
}

void
cfgOptionSet(ConfigOption optionId, ConfigSource source, const Variant *value)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
        FUNCTION_TEST_PARAM(ENUM, source);
        FUNCTION_TEST_PARAM(VARIANT, value);
    FUNCTION_TEST_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);

    MEM_CONTEXT_BEGIN(configStatic.memContext)
    {
        // Set the source
        configStatic.option[optionId].source = source;

        // Store old value
        Variant *valueOld = configStatic.option[optionId].value;

        // Only set value if it is not null
        if (value != NULL)
        {
            switch (cfgDefOptionType(cfgOptionDefIdFromId(optionId)))
            {
                case cfgDefOptTypeBoolean:
                {
                    if (varType(value) == varTypeBool)
                        configStatic.option[optionId].value = varDup(value);
                    else
                        configStatic.option[optionId].value = varNewBool(varBoolForce(value));

                    break;
                }

                case cfgDefOptTypeFloat:
                {
                    if (varType(value) == varTypeDouble)
                        configStatic.option[optionId].value = varDup(value);
                    else
                        configStatic.option[optionId].value = varNewDbl(varDblForce(value));

                    break;
                }

                case cfgDefOptTypeInteger:
                case cfgDefOptTypeSize:
                {
                    if (varType(value) == varTypeInt64)
                        configStatic.option[optionId].value = varDup(value);
                    else
                        configStatic.option[optionId].value = varNewInt64(varInt64Force(value));

                    break;
                }

                case cfgDefOptTypeHash:
                {
                    if (varType(value) == varTypeKeyValue)
                        configStatic.option[optionId].value = varDup(value);
                    else
                        THROW_FMT(AssertError, "option '%s' must be set with KeyValue variant", cfgOptionName(optionId));

                    break;
                }

                case cfgDefOptTypeList:
                {
                    if (varType(value) == varTypeVariantList)
                        configStatic.option[optionId].value = varDup(value);
                    else
                        THROW_FMT(AssertError, "option '%s' must be set with VariantList variant", cfgOptionName(optionId));

                    break;
                }

                case cfgDefOptTypePath:
                case cfgDefOptTypeString:
                {
                    if (varType(value) == varTypeString)
                        configStatic.option[optionId].value = varDup(value);
                    else
                        THROW_FMT(AssertError, "option '%s' must be set with String variant", cfgOptionName(optionId));

                    break;
                }
            }
        }
        else
            configStatic.option[optionId].value = NULL;

        // Free old value
        if (valueOld != NULL)
            varFree(valueOld);
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
How was the option set (default, param, config)?
***********************************************************************************************************************************/
ConfigSource
cfgOptionSource(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
    FUNCTION_TEST_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);

    FUNCTION_TEST_RETURN(configStatic.option[optionId].source);
}

/***********************************************************************************************************************************
Is the option valid for the command and set?
***********************************************************************************************************************************/
bool
cfgOptionTest(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
    FUNCTION_TEST_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);

    FUNCTION_TEST_RETURN(cfgOptionValid(optionId) && configStatic.option[optionId].value != NULL);
}

/***********************************************************************************************************************************
Is the option valid for this command?
***********************************************************************************************************************************/
bool
cfgOptionValid(ConfigOption optionId)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
    FUNCTION_TEST_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);

    FUNCTION_TEST_RETURN(configStatic.option[optionId].valid);
}

void
cfgOptionValidSet(ConfigOption optionId, bool valid)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, optionId);
        FUNCTION_TEST_PARAM(BOOL, valid);
    FUNCTION_TEST_END();

    ASSERT(optionId < CFG_OPTION_TOTAL);

    configStatic.option[optionId].valid = valid;

    FUNCTION_TEST_RETURN_VOID();
}
