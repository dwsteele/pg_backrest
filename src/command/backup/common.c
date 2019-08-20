/***********************************************************************************************************************************
Common Functions and Definitions for Backup and Expire Commands
***********************************************************************************************************************************/
#include "build.auto.h"

#include "command/backup/common.h"
#include "common/debug.h"
#include "common/log.h"

/***********************************************************************************************************************************
Constants
***********************************************************************************************************************************/
#define DATE_TIME_REGEX                                             "[0-9]{8}\\-[0-9]{6}"

STRING_EXTERN(BACKUP_TYPE_FULL_STR,                                 BACKUP_TYPE_FULL);
STRING_EXTERN(BACKUP_TYPE_DIFF_STR,                                 BACKUP_TYPE_DIFF);
STRING_EXTERN(BACKUP_TYPE_INCR_STR,                                 BACKUP_TYPE_INCR);

/***********************************************************************************************************************************
Returns an anchored regex string for filtering backups based on the type (at least one type is required to be true)
***********************************************************************************************************************************/
String *
backupRegExp(BackupRegExpParam param)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(BOOL, param.full);
        FUNCTION_LOG_PARAM(BOOL, param.differential);
        FUNCTION_LOG_PARAM(BOOL, param.incremental);
    FUNCTION_LOG_END();

    ASSERT(param.full || param.differential || param.incremental);

    String *result = NULL;

    // Start the expression with the anchor, date/time regexp and full backup indicator
    result = strNew("^" DATE_TIME_REGEX "F");

    // Add the diff and/or incr expressions if requested
    if (param.differential || param.incremental)
    {
        // If full requested then diff/incr is optional
        if (param.full)
        {
            strCat(result, "(\\_");
        }
        // Else diff/incr is required
        else
        {
            strCat(result, "\\_");
        }

        // Append date/time regexp for diff/incr
        strCat(result, DATE_TIME_REGEX);

        // Filter on both diff/incr
        if (param.differential && param.incremental)
        {
            strCat(result, "(D|I)");
        }
        // Else just diff
        else if (param.differential)
        {
            strCatChr(result, 'D');
        }
        // Else just incr
        else
        {
            strCatChr(result, 'I');
        }

        // If full requested then diff/incr is optional
        if (param.full)
        {
            strCat(result, "){0,1}");
        }
    }

    // Append the end anchor
    strCat(result, "$");

    FUNCTION_LOG_RETURN(STRING, result);
}

/***********************************************************************************************************************************
Convert text backup type to an enum and back
***********************************************************************************************************************************/
BackupType
backupType(const String *type)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, type);
    FUNCTION_TEST_END();

    ASSERT(type != NULL);

    BackupType result;

    if (strEq(type, BACKUP_TYPE_FULL_STR))
        result = backupTypeFull;
    else if (strEq(type, BACKUP_TYPE_DIFF_STR))
        result = backupTypeDiff;
    else if (strEq(type, BACKUP_TYPE_INCR_STR))
        result = backupTypeIncr;
    else
        THROW_FMT(AssertError, "invalid backup type '%s'", strPtr(type));

    FUNCTION_TEST_RETURN(result);
}

const String *backupTypeStr(BackupType type)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, type);
    FUNCTION_TEST_END();

    ASSERT(type <= backupTypeIncr);

    const String *result = NULL;

    switch (type)
    {
        case backupTypeFull:
        {
            result = BACKUP_TYPE_FULL_STR;
            break;
        }

        case backupTypeDiff:
        {
            result = BACKUP_TYPE_DIFF_STR;
            break;
        }

        case backupTypeIncr:
        {
            result = BACKUP_TYPE_INCR_STR;
            break;
        }
    }

    FUNCTION_TEST_RETURN(result);
}
