/***********************************************************************************************************************************
PostgreSQL Info Handler
***********************************************************************************************************************************/
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "common/assert.h"
#include "common/debug.h"
#include "common/log.h"
#include "common/memContext.h"
#include "common/ini.h"
#include "common/memContext.h"
#include "common/type/json.h"
#include "common/type/list.h"
#include "info/info.h"
#include "info/infoPg.h"
#include "postgres/interface.h"
#include "postgres/version.h"
#include "storage/helper.h"

/***********************************************************************************************************************************
Internal constants
***********************************************************************************************************************************/
#define INFO_SECTION_DB                                             "db"
#define INFO_SECTION_DB_HISTORY                                     INFO_SECTION_DB ":history"
#define INFO_SECTION_DB_MANIFEST                                    "backup:" INFO_SECTION_DB

#define INFO_KEY_DB_ID                                              "db-id"
#define INFO_KEY_DB_CATALOG_VERSION                                 "db-catalog-version"
#define INFO_KEY_DB_CONTROL_VERSION                                 "db-control-version"
#define INFO_KEY_DB_SYSTEM_ID                                       "db-system-id"
#define INFO_KEY_DB_VERSION                                         "db-version"

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
struct InfoPg
{
    MemContext *memContext;                                         // Context that contains the infoPg
    List *history;                                                  // A list of InfoPgData
    Info *info;                                                     // Info contents
};

/***********************************************************************************************************************************
Load an InfoPg object
??? Need to consider adding the following parameters in order to throw errors
        $bRequired,                                 # Is archive info required?  --- may not need this if ignoreMissing is enough
        $bLoad,                                     # Should the file attempt to be loaded?
        $strCipherPassSub,                          # Passphrase to encrypt the subsequent archive files if repo is encrypted
??? Currently this assumes the file exists and loads data from it
***********************************************************************************************************************************/
InfoPg *
infoPgNew(const Storage *storage, const String *fileName, InfoPgType type)
{
    FUNCTION_DEBUG_BEGIN(logLevelDebug);
        FUNCTION_DEBUG_PARAM(STRING, fileName);

        FUNCTION_DEBUG_ASSERT(fileName != NULL);
    FUNCTION_DEBUG_END();

    InfoPg *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("infoPg")
    {
        // Create object
        this = memNew(sizeof(InfoPg));
        this->memContext = MEM_CONTEXT_NEW();
        this->info = infoNew(storage, fileName);

        // Get the pg history list
        this->history = lstNew(sizeof(InfoPgData));

        MEM_CONTEXT_TEMP_BEGIN()
        {
            const Ini *infoPgIni = infoIni(this->info);
            const String *pgHistorySection = strNew(INFO_SECTION_DB_HISTORY);
            const StringList *pgHistoryKey = iniSectionKeyList(infoPgIni, pgHistorySection);
            const Variant *idKey = varNewStr(strNew(INFO_KEY_DB_ID));
            const Variant *systemIdKey = varNewStr(strNew(INFO_KEY_DB_SYSTEM_ID));
            const Variant *versionKey = varNewStr(strNew(INFO_KEY_DB_VERSION));

            // History must include at least one item or the file is corrupt
            ASSERT(strLstSize(pgHistoryKey) > 0);

            // Iterate in reverse because we would like the most recent pg history to be in position 0.  If we need to look at the
            // history list at all we'll be iterating from newest to oldest and putting newest in position 0 makes for more natural
            // looping.
            for (unsigned int pgHistoryIdx = strLstSize(pgHistoryKey) - 1; pgHistoryIdx < strLstSize(pgHistoryKey); pgHistoryIdx--)
            {
                // Load JSON data into a KeyValue
                const KeyValue *pgDataKv = jsonToKv(
                    varStr(iniGet(infoPgIni, pgHistorySection, strLstGet(pgHistoryKey, pgHistoryIdx))));

                // Get db values that are common to all info files
                InfoPgData infoPgData =
                {
                    .id = cvtZToUInt(strPtr(strLstGet(pgHistoryKey, pgHistoryIdx))),
                    .version = pgVersionFromStr(varStr(kvGet(pgDataKv, versionKey))),

                    // This is different in archive.info due to a typo that can't be fixed without a format version bump
                    .systemId = varUInt64Force(kvGet(pgDataKv, type == infoPgArchive ? idKey : systemIdKey)),
                };

                // Get values that are only in backup and manifest files.  These are really vestigial since stanza-create verifies
                // the control and catalog versions so there is no good reason to store them.  However, for backward compatability
                // we must write them at least, even if we give up reading them.
                if (type == infoPgBackup || type == infoPgManifest)
                {
                    const Variant *catalogVersionKey = varNewStr(strNew(INFO_KEY_DB_CATALOG_VERSION));
                    const Variant *controlVersionKey = varNewStr(strNew(INFO_KEY_DB_CONTROL_VERSION));

                    infoPgData.catalogVersion = (unsigned int)varUInt64Force(kvGet(pgDataKv, catalogVersionKey));
                    infoPgData.controlVersion = (unsigned int)varUInt64Force(kvGet(pgDataKv, controlVersionKey));
                }
                else if (type != infoPgArchive)
                    THROW_FMT(AssertError, "invalid InfoPg type %u", type);

                lstAdd(this->history, &infoPgData);
            }
        }
        MEM_CONTEXT_TEMP_END();
    }
    MEM_CONTEXT_NEW_END();

    // Return buffer
    FUNCTION_DEBUG_RESULT(INFO_PG, this);
}

/***********************************************************************************************************************************
Add Postgres data to the history list at position 0 to ensure the latest history is always first in the list
***********************************************************************************************************************************/
void
infoPgAdd(InfoPg *this, const InfoPgData *infoPgData)
{
    FUNCTION_DEBUG_BEGIN(logLevelDebug);
        FUNCTION_DEBUG_PARAM(INFO_PG, this);
        FUNCTION_DEBUG_PARAM(INFO_PG_DATAP, infoPgData);

        FUNCTION_DEBUG_ASSERT(this != NULL);
        FUNCTION_DEBUG_ASSERT(infoPgData != NULL);
    FUNCTION_DEBUG_END();

    lstInsert(this->history, 0, infoPgData);

    FUNCTION_DEBUG_RESULT_VOID();
}

/***********************************************************************************************************************************
Construct archive id
***********************************************************************************************************************************/
String *
infoPgArchiveId(const InfoPg *this, unsigned int pgDataIdx)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(INFO_PG, this);
        FUNCTION_DEBUG_PARAM(UINT, pgDataIdx);

        FUNCTION_DEBUG_ASSERT(this != NULL);
    FUNCTION_DEBUG_END();

    InfoPgData pgData = infoPgData(this, pgDataIdx);

    FUNCTION_DEBUG_RESULT(STRING, strNewFmt("%s-%u", strPtr(pgVersionToStr(pgData.version)), pgData.id));
}

/***********************************************************************************************************************************
Return a structure of the Postgres data from a specific index
***********************************************************************************************************************************/
InfoPgData
infoPgData(const InfoPg *this, unsigned int pgDataIdx)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(INFO_PG, this);
        FUNCTION_DEBUG_PARAM(UINT, pgDataIdx);

        FUNCTION_DEBUG_ASSERT(this != NULL);
    FUNCTION_DEBUG_END();

    FUNCTION_DEBUG_RESULT(INFO_PG_DATA, *((InfoPgData *)lstGet(this->history, pgDataIdx)));
}

/***********************************************************************************************************************************
Return a structure of the current Postgres data
***********************************************************************************************************************************/
InfoPgData
infoPgDataCurrent(const InfoPg *this)
{
    FUNCTION_DEBUG_BEGIN(logLevelDebug);
        FUNCTION_DEBUG_PARAM(INFO_PG, this);

        FUNCTION_DEBUG_ASSERT(this != NULL);
    FUNCTION_DEBUG_END();

    FUNCTION_DEBUG_RESULT(INFO_PG_DATA, infoPgData(this, 0));
}

/***********************************************************************************************************************************
Return total Postgres data in the history
***********************************************************************************************************************************/
unsigned int
infoPgDataTotal(const InfoPg *this)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(INFO_PG, this);

        FUNCTION_DEBUG_ASSERT(this != NULL);
    FUNCTION_DEBUG_END();

    FUNCTION_DEBUG_RESULT(UINT, lstSize(this->history));
}

/***********************************************************************************************************************************
Render as string for logging
***********************************************************************************************************************************/
String *
infoPgDataToLog(const InfoPgData *this)
{
    return strNewFmt(
        "{id: %u, version: %u, systemId: %" PRIu64 ", catalogVersion: %" PRIu32 ", controlVersion: %" PRIu32 "}",
        this->id, this->version, this->systemId, this->catalogVersion, this->controlVersion);
}

/***********************************************************************************************************************************
Free the info
***********************************************************************************************************************************/
void
infoPgFree(InfoPg *this)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(INFO_PG, this);
    FUNCTION_DEBUG_END();

    if (this != NULL)
        memContextFree(this->memContext);

    FUNCTION_DEBUG_RESULT_VOID();
}
