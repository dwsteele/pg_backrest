/***********************************************************************************************************************************
Archive Info Handler
***********************************************************************************************************************************/
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "common/debug.h"
#include "common/log.h"
#include "common/memContext.h"
#include "common/ini.h"
#include "info/infoArchive.h"
#include "info/infoPg.h"
#include "postgres/interface.h"
#include "storage/helper.h"

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
struct InfoArchive
{
    MemContext *memContext;                                         // Context that contains the InfoArchive
    InfoPg *infoPg;                                                 // Contents of the DB data
    String *archiveId;                                              // Archive id for the current PG version
};

/***********************************************************************************************************************************
Create a new InfoArchive object
// ??? Need loadFile parameter
***********************************************************************************************************************************/
InfoArchive *
infoArchiveNew(const Storage *storage, const String *fileName, bool ignoreMissing, CipherType cipherType, const String *cipherPass)
{
    FUNCTION_DEBUG_BEGIN(logLevelDebug);
        FUNCTION_DEBUG_PARAM(STORAGE, storage);
        FUNCTION_DEBUG_PARAM(STRING, fileName);
        FUNCTION_DEBUG_PARAM(BOOL, ignoreMissing);
        FUNCTION_DEBUG_PARAM(ENUM, cipherType);
        // cipherPass omitted for security

        FUNCTION_DEBUG_ASSERT(fileName != NULL);
    FUNCTION_DEBUG_END();

    InfoArchive *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("infoArchive")
    {
        // Create object
        this = memNew(sizeof(InfoArchive));
        this->memContext = MEM_CONTEXT_NEW();

        // Catch file missing error and add archive-specific hints before rethrowing
        TRY_BEGIN()
        {
            this->infoPg = infoPgNew(storage, fileName, infoPgArchive, cipherType, cipherPass);
        }
        CATCH_ANY()
        {
            THROWP_FMT(
                errorType(),
                "%s\n"
                "HINT: archive.info cannot be opened but is required to push/get WAL segments.\n"
                "HINT: is archive_command configured correctly in postgresql.conf?\n"
                "HINT: has a stanza-create been performed?\n"
                "HINT: use --no-archive-check to disable archive checks during backup if you have an alternate archiving scheme.",
                errorMessage());
        }
        TRY_END();

        // Store the archiveId for the current PG db-version db-id
        this->archiveId = infoPgArchiveId(this->infoPg, 0);
    }
    MEM_CONTEXT_NEW_END();

    // Return buffer
    FUNCTION_DEBUG_RESULT(INFO_ARCHIVE, this);
}

/***********************************************************************************************************************************
Checks the archive info file's DB section against the PG version and system id passed in
// ??? Should we still check that the file exists if it is required?
***********************************************************************************************************************************/
void
infoArchiveCheckPg(const InfoArchive *this, unsigned int pgVersion, uint64_t pgSystemId)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(INFO_ARCHIVE, this);
        FUNCTION_DEBUG_PARAM(UINT, pgVersion);
        FUNCTION_DEBUG_PARAM(UINT64, pgSystemId);

        FUNCTION_DEBUG_ASSERT(this != NULL);
    FUNCTION_DEBUG_END();

    String *errorMsg = NULL;

    InfoPgData archivePg = infoPgDataCurrent(this->infoPg);

    if (archivePg.version != pgVersion)
    {
        errorMsg = strNewFmt(
            "WAL segment version %s does not match archive version %s", strPtr(pgVersionToStr(pgVersion)),
            strPtr(pgVersionToStr(archivePg.version)));
    }

    if (archivePg.systemId != pgSystemId)
    {
        errorMsg = errorMsg != NULL ? strCat(errorMsg, "\n") : strNew("");
        strCatFmt(errorMsg,
                "WAL segment system-id %" PRIu64 " does not match archive system-id %" PRIu64,
                pgSystemId, archivePg.systemId);
    }

    if (errorMsg != NULL)
    {
        errorMsg = strCatFmt(errorMsg, "\nHINT: are you archiving to the correct stanza?");
        THROW(ArchiveMismatchError, strPtr(errorMsg));
    }

    FUNCTION_DEBUG_RESULT_VOID();
}

/***********************************************************************************************************************************
Given a backrest history id and postgres systemId and version, return the archiveId of the best match
***********************************************************************************************************************************/
const String *
infoArchiveIdHistoryMatch(
    const InfoArchive *this, const unsigned int historyId, const unsigned int pgVersion, const uint64_t pgSystemId)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(INFO_ARCHIVE, this);
        FUNCTION_DEBUG_PARAM(UINT, historyId);
        FUNCTION_DEBUG_PARAM(UINT, pgVersion);
        FUNCTION_DEBUG_PARAM(UINT64, pgSystemId);

        FUNCTION_DEBUG_ASSERT(this != NULL);
    FUNCTION_DEBUG_END();

    String *archiveId = NULL;
    InfoPg *infoPg = infoArchivePg(this);

    // Search the history list, from newest to oldest
    for (unsigned int pgIdx = 0; pgIdx < infoPgDataTotal(infoPg); pgIdx++)
    {
        InfoPgData pgDataArchive = infoPgData(infoPg, pgIdx);

        // If there is an exact match with the history, system and version then get the archiveId and stop
        if (historyId == pgDataArchive.id && pgSystemId == pgDataArchive.systemId && pgVersion == pgDataArchive.version)
        {
            archiveId = infoPgArchiveId(infoPg, pgIdx);
            break;
        }
    }

    // If there was not an exact match, then search for the first matching database system-id and version
    if (archiveId == NULL)
    {
        for (unsigned int pgIdx = 0; pgIdx < infoPgDataTotal(infoPg); pgIdx++)
        {
            InfoPgData pgDataArchive = infoPgData(infoPg, pgIdx);

            if (pgSystemId == pgDataArchive.systemId && pgVersion == pgDataArchive.version)
            {
                archiveId = infoPgArchiveId(infoPg, pgIdx);
                break;
            }
        }
    }

    // If the archive id has not been found, then error
    if (archiveId == NULL)
    {
        THROW_FMT(
            ArchiveMismatchError,
            "unable to retrieve the archive id for database version '%s' and system-id '%" PRIu64 "'",
            strPtr(pgVersionToStr(pgVersion)), pgSystemId);
    }

    FUNCTION_TEST_RESULT(STRING, archiveId);
}

/***********************************************************************************************************************************
Get the current archive id
***********************************************************************************************************************************/
const String *
infoArchiveId(const InfoArchive *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INFO_ARCHIVE, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(STRING, this->archiveId);
}

/***********************************************************************************************************************************
Return the cipher passphrase
***********************************************************************************************************************************/
const String *
infoArchiveCipherPass(const InfoArchive *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INFO_ARCHIVE, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(CONST_STRING, infoPgCipherPass(this->infoPg));
}

/***********************************************************************************************************************************
Get PostgreSQL info
***********************************************************************************************************************************/
InfoPg *
infoArchivePg(const InfoArchive *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INFO_ARCHIVE, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(INFO_PG, this->infoPg);
}

/***********************************************************************************************************************************
Free the info
***********************************************************************************************************************************/
void
infoArchiveFree(InfoArchive *this)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(INFO_ARCHIVE, this);
    FUNCTION_DEBUG_END();

    if (this != NULL)
        memContextFree(this->memContext);

    FUNCTION_DEBUG_RESULT_VOID();
}
