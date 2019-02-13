/***********************************************************************************************************************************
Info Command
***********************************************************************************************************************************/
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "command/archive/common.h"
#include "command/info/info.h"
#include "common/debug.h"
#include "common/io/handleWrite.h"
#include "common/log.h"
#include "common/memContext.h"
#include "common/type/json.h"
#include "config/config.h"
#include "crypto/crypto.h"
#include "info/info.h"
#include "info/infoArchive.h"
#include "info/infoBackup.h"
#include "info/infoPg.h"
#include "perl/exec.h"
#include "postgres/interface.h"
#include "storage/helper.h"

/***********************************************************************************************************************************
Constants
***********************************************************************************************************************************/
STRING_STATIC(CFGOPTVAL_INFO_OUTPUT_TEXT_STR,                       "text");

// Naming convention: <sectionname>_KEY_<keyname>_STR. If the key exists in multiple sections, then <sectionname>_ is omitted.
STRING_STATIC(ARCHIVE_KEY_MIN_STR,                                  "min");
STRING_STATIC(ARCHIVE_KEY_MAX_STR,                                  "max");
STRING_STATIC(BACKREST_KEY_FORMAT_STR,                              "format");
STRING_STATIC(BACKREST_KEY_VERSION_STR,                             "version");
STRING_STATIC(BACKUP_KEY_BACKREST_STR,                              "backrest");
STRING_STATIC(BACKUP_KEY_INFO_STR,                                  "info");
STRING_STATIC(BACKUP_KEY_LABEL_STR,                                 "label");
STRING_STATIC(BACKUP_KEY_PRIOR_STR,                                 "prior");
STRING_STATIC(BACKUP_KEY_REFERENCE_STR,                             "reference");
STRING_STATIC(BACKUP_KEY_TIMESTAMP_STR,                             "timestamp");
STRING_STATIC(BACKUP_KEY_TYPE_STR,                                  "type");
STRING_STATIC(DB_KEY_ID_STR,                                        "id");
STRING_STATIC(DB_KEY_SYSTEM_ID_STR,                                 "system-id");
STRING_STATIC(DB_KEY_VERSION_STR,                                   "version");
STRING_STATIC(INFO_KEY_REPOSITORY_STR,                              "repository");
STRING_STATIC(KEY_ARCHIVE_STR,                                      "archive");
STRING_STATIC(KEY_DATABASE_STR,                                     "database");
STRING_STATIC(KEY_DELTA_STR,                                        "delta");
STRING_STATIC(KEY_SIZE_STR,                                         "size");
STRING_STATIC(KEY_START_STR,                                        "start");
STRING_STATIC(KEY_STOP_STR,                                         "stop");
STRING_STATIC(STANZA_KEY_BACKUP_STR,                                "backup");
STRING_STATIC(STANZA_KEY_CIPHER_STR,                                "cipher");
STRING_STATIC(STANZA_VALUE_CIPHER_NONE_STR,                         "none");
STRING_STATIC(STANZA_KEY_NAME_STR,                                  "name");
STRING_STATIC(STANZA_KEY_STATUS_STR,                                "status");
STRING_STATIC(STANZA_KEY_DB_STR,                                    "db");
STRING_STATIC(STATUS_KEY_CODE_STR,                                  "code");
STRING_STATIC(STATUS_KEY_MESSAGE_STR,                               "message");

STRING_STATIC(INFO_STANZA_STATUS_OK,                                "ok");
STRING_STATIC(INFO_STANZA_STATUS_ERROR,                             "error");
#define INFO_STANZA_STATUS_CODE_OK                                  0
STRING_STATIC(INFO_STANZA_STATUS_MESSAGE_OK_STR,                    "ok");
#define INFO_STANZA_STATUS_CODE_MISSING_STANZA_PATH                 1
STRING_STATIC(INFO_STANZA_STATUS_MESSAGE_MISSING_STANZA_PATH_STR,   "missing stanza path");
#define INFO_STANZA_STATUS_CODE_NO_BACKUP                           2
STRING_STATIC(INFO_STANZA_STATUS_MESSAGE_NO_BACKUP_STR,             "no valid backups");
#define INFO_STANZA_STATUS_CODE_MISSING_STANZA_DATA                 3
STRING_STATIC(INFO_STANZA_STATUS_MESSAGE_MISSING_STANZA_DATA_STR,   "missing stanza data");
#define INFO_STANZA_STATUS_CODE_MISSING_WAL_SEG                     4
STRING_STATIC(INFO_STANZA_STATUS_MESSAGE_MISSING_WAL_SEG_STR,       "missing wal segment(s)");

/***********************************************************************************************************************************
Set error status code and message for the stanza to the code and message passed.
***********************************************************************************************************************************/
static void
stanzaStatus(const int code, const String *message, Variant *stanzaInfo)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INT, code);
        FUNCTION_TEST_PARAM(STRING, message);
        FUNCTION_TEST_PARAM(VARIANT, stanzaInfo);
    FUNCTION_TEST_END();

    ASSERT(code >= 0 && code <= 4);
    ASSERT(message != NULL);
    ASSERT(stanzaInfo != NULL);

    Variant *stanzaStatus = varNewStr(STANZA_KEY_STATUS_STR);
    KeyValue *statusKv = kvPutKv(varKv(stanzaInfo), stanzaStatus);

    kvAdd(statusKv, varNewStr(STATUS_KEY_CODE_STR), varNewInt(code));
    kvAdd(statusKv, varNewStr(STATUS_KEY_MESSAGE_STR), varNewStr(message));

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Set the data for the archive section of the stanza for the database info from the backup.info file.
***********************************************************************************************************************************/
static StringList *
archiveDbList(const String *stanza, const InfoPgData *pgData, VariantList *archiveSection, const InfoArchive *info, bool currentDb)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, stanza);
        FUNCTION_TEST_PARAM_P(INFO_PG_DATA, pgData);
        FUNCTION_TEST_PARAM(VARIANT, archiveSection);
        FUNCTION_TEST_PARAM(BOOL, currentDb);
    FUNCTION_TEST_END();

    ASSERT(stanza != NULL);
    ASSERT(pgData != NULL);
    ASSERT(archiveSection != NULL);

    // With multiple DB versions, the backup.info history-id may not be the same as archive.info history-id, so the
    // archive path must be built by retrieving the archive id given the db version and system id of the backup.info file.
    // If there is no match, an error will be thrown.
    const String *archiveId = infoArchiveIdHistoryMatch(info, pgData->id, pgData->version, pgData->systemId);

    String *archivePath = strNewFmt("%s/%s/%s", STORAGE_PATH_ARCHIVE, strPtr(stanza), strPtr(archiveId));
    String *archiveStart = NULL;
    String *archiveStop = NULL;
    Variant *archiveInfo = varNewKv();

    // Get a list of WAL directories in the archive repo from oldest to newest, if any exist
    StringList *walDir = storageListP(storageRepo(), archivePath, .expression = WAL_SEGMENT_DIR_REGEXP_STR);

    if (walDir != NULL)
    {
        unsigned int sizeWalDir = strLstSize(walDir);

        if (sizeWalDir > 1)
            walDir = strLstSort(walDir, sortOrderAsc);

        // Not every WAL dir has WAL files so check each
        for (unsigned int idx = 0; idx < sizeWalDir; idx++)
        {
            // Get a list of all WAL in this WAL dir
            StringList *list = storageListP(
                storageRepo(), strNewFmt("%s/%s", strPtr(archivePath), strPtr(strLstGet(walDir, idx))),
                .expression = WAL_SEGMENT_FILE_REGEXP_STR);

            // If wal segments are found, get the oldest one as the archive start
            if (strLstSize(list) > 0)
            {
                // Sort the list from oldest to newest to get the oldest starting WAL archived for this DB
                list = strLstSort(list, sortOrderAsc);
                archiveStart = strSubN(strLstGet(list, 0), 0, 24);
                break;
            }
        }

        // Iterate through the directory list in the reverse so processing newest first. Cast comparison to an int for readability.
        for (unsigned int idx = sizeWalDir - 1; (int)idx >= 0; idx--)
        {
            // Get a list of all WAL in this WAL dir
            StringList *list = storageListP(
                storageRepo(), strNewFmt("%s/%s", strPtr(archivePath), strPtr(strLstGet(walDir, idx))),
                .expression = WAL_SEGMENT_FILE_REGEXP_STR);

            // If wal segments are found, get the newest one as the archive stop
            if (strLstSize(list) > 0)
            {
                // Sort the list from newest to oldest to get the newest ending WAL archived for this DB
                list = strLstSort(list, sortOrderDesc);
                archiveStop = strSubN(strLstGet(list, 0), 0, 24);
                break;
            }
        }
    }

    // If there is an archive or the database is the current database then store it
    if (currentDb || archiveStart != NULL)
    {
        // Add empty database section to archiveInfo and then fill in database id from the backup.info
        KeyValue *databaseInfo = kvPutKv(varKv(archiveInfo), varNewStr(KEY_DATABASE_STR));

        kvAdd(databaseInfo, varNewStr(DB_KEY_ID_STR), varNewUInt64(pgData->id));

        kvPut(varKv(archiveInfo), varNewStr(DB_KEY_ID_STR), varNewStr(archiveId));
        kvPut(
            varKv(archiveInfo), varNewStr(ARCHIVE_KEY_MIN_STR), (archiveStart != NULL ? varNewStr(archiveStart) : (Variant *)NULL));
        kvPut(varKv(archiveInfo), varNewStr(ARCHIVE_KEY_MAX_STR), (archiveStop != NULL ? varNewStr(archiveStop) : (Variant *)NULL));

        varLstAdd(archiveSection, archiveInfo);
    }

    StringList *returnList = strLstNew();
    strLstAdd(returnList, archiveId);
    strLstAdd(returnList, archiveStop);
    FUNCTION_TEST_RETURN(returnList);
}

/***********************************************************************************************************************************
For each current backup in the backup.info file of the stanza, set the data for the backup section.
***********************************************************************************************************************************/
static StringList *
backupList(VariantList *backupSection, InfoBackup *info)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(VARIANT, backupSection);
        FUNCTION_TEST_PARAM(INFO_BACKUP, info);
    FUNCTION_TEST_END();

    ASSERT(backupSection != NULL);
    ASSERT(info != NULL);

    // Keep the first and the last wal segments needed for all the backups
    const String *minNeededWalSeg = NULL;
    const String *maxNeededWalSeg = NULL;

    // For each current backup, get the label and corresponding data and build the backup section
    for (unsigned int keyIdx = 0; keyIdx < infoBackupDataTotal(info); keyIdx++)
    {
        // Get the backup data
        InfoBackupData backupData = infoBackupData(info, keyIdx);

        Variant *backupInfo = varNewKv();

        // main keys
        kvPut(varKv(backupInfo), varNewStr(BACKUP_KEY_LABEL_STR), varNewStr(backupData.backupLabel));
        kvPut(varKv(backupInfo), varNewStr(BACKUP_KEY_TYPE_STR), varNewStr(backupData.backupType));
        kvPut(
            varKv(backupInfo), varNewStr(BACKUP_KEY_PRIOR_STR),
            (backupData.backupPrior != NULL ? varNewStr(backupData.backupPrior) : NULL));
        kvPut(
            varKv(backupInfo), varNewStr(BACKUP_KEY_REFERENCE_STR),
            (backupData.backupReference != NULL ? varNewVarLst(varLstNewStrLst(backupData.backupReference)) : NULL));

        // archive section
        KeyValue *archiveInfo = kvPutKv(varKv(backupInfo), varNewStr(KEY_ARCHIVE_STR));

        kvAdd(
            archiveInfo, varNewStr(KEY_START_STR),
            (backupData.backupArchiveStart != NULL ? varNewStr(backupData.backupArchiveStart) : NULL));
        minNeededWalSeg = (minNeededWalSeg == NULL ? backupData.backupArchiveStart : minNeededWalSeg);

        kvAdd(
            archiveInfo, varNewStr(KEY_STOP_STR),
            (backupData.backupArchiveStop != NULL ? varNewStr(backupData.backupArchiveStop) : NULL));
        maxNeededWalSeg = backupData.backupArchiveStop;

        // backrest section
        KeyValue *backrestInfo = kvPutKv(varKv(backupInfo), varNewStr(BACKUP_KEY_BACKREST_STR));

        kvAdd(backrestInfo, varNewStr(BACKREST_KEY_FORMAT_STR), varNewUInt64(backupData.backrestFormat));
        kvAdd(backrestInfo, varNewStr(BACKREST_KEY_VERSION_STR), varNewStr(backupData.backrestVersion));

        // database section
        KeyValue *dbInfo = kvPutKv(varKv(backupInfo), varNewStr(KEY_DATABASE_STR));

        kvAdd(dbInfo, varNewStr(DB_KEY_ID_STR), varNewUInt64(backupData.backupPgId));

        // info section
        KeyValue *infoInfo = kvPutKv(varKv(backupInfo), varNewStr(BACKUP_KEY_INFO_STR));

        kvAdd(infoInfo, varNewStr(KEY_SIZE_STR), varNewUInt64(backupData.backupInfoSize));
        kvAdd(infoInfo, varNewStr(KEY_DELTA_STR), varNewUInt64(backupData.backupInfoSizeDelta));

        // info:repository section
        KeyValue *repoInfo = kvPutKv(infoInfo, varNewStr(INFO_KEY_REPOSITORY_STR));

        kvAdd(repoInfo, varNewStr(KEY_SIZE_STR), varNewUInt64(backupData.backupInfoRepoSize));
        kvAdd(repoInfo, varNewStr(KEY_DELTA_STR), varNewUInt64(backupData.backupInfoRepoSizeDelta));

        // timestamp section
        KeyValue *timeInfo = kvPutKv(varKv(backupInfo), varNewStr(BACKUP_KEY_TIMESTAMP_STR));

        kvAdd(timeInfo, varNewStr(KEY_START_STR), varNewUInt64(backupData.backupTimestampStart));
        kvAdd(timeInfo, varNewStr(KEY_STOP_STR), varNewUInt64(backupData.backupTimestampStop));

        varLstAdd(backupSection, backupInfo);
    }

    StringList *neededWalSegs = strLstNew();
    strLstAdd(neededWalSegs, minNeededWalSeg);
    strLstAdd(neededWalSegs, maxNeededWalSeg);
    FUNCTION_TEST_RETURN(neededWalSegs);
}

/***********************************************************************************************************************************
Set the stanza data for each stanza found in the repo.
***********************************************************************************************************************************/
static VariantList *
stanzaInfoList(const String *stanza, StringList *stanzaList)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, stanza);
        FUNCTION_TEST_PARAM(STRING_LIST, stanzaList);
    FUNCTION_TEST_END();

    ASSERT(stanzaList != NULL);

    VariantList *result = varLstNew();
    bool stanzaFound = false;

    // Sort the list
    stanzaList = strLstSort(stanzaList, sortOrderAsc);

    for (unsigned int idx = 0; idx < strLstSize(stanzaList); idx++)
    {
        String *stanzaListName = strLstGet(stanzaList, idx);

        // If a specific stanza has been requested and this is not it, then continue to the next in the list else indicate found
        if (stanza != NULL)
        {
            if (!strEq(stanza, stanzaListName))
                continue;
            else
                stanzaFound = true;
        }

        // Create the stanzaInfo and section variables
        Variant *stanzaInfo = varNewKv();
        VariantList *dbSection = varLstNew();
        VariantList *backupSection = varLstNew();
        VariantList *archiveSection = varLstNew();
        InfoBackup *info = NULL;

        // Catch certain errors
        TRY_BEGIN()
        {
            // Attempt to load the backup info file
            info = infoBackupNew(
                storageRepo(), strNewFmt(STORAGE_PATH_BACKUP "/%s/%s", strPtr(stanzaListName), INFO_BACKUP_FILE), false,
                cipherType(cfgOptionStr(cfgOptRepoCipherType)), cfgOptionStr(cfgOptRepoCipherPass));
        }
        CATCH(FileMissingError)
        {
            // If there is no backup.info then set the status to indicate missing
            stanzaStatus(
                INFO_STANZA_STATUS_CODE_MISSING_STANZA_DATA, INFO_STANZA_STATUS_MESSAGE_MISSING_STANZA_DATA_STR, stanzaInfo);
        }
        CATCH(CryptoError)
        {
            // If a reason for the error is due to a an ecryption error, add a hint
            THROW_FMT(
                CryptoError,
                "%s\n"
                "HINT: use option --stanza if encryption settings are different for the stanza than the global settings",
                errorMessage());
        }
        TRY_END();

        // Set the stanza name and cipher
        kvPut(varKv(stanzaInfo), varNewStr(STANZA_KEY_NAME_STR), varNewStr(stanzaListName));
        kvPut(varKv(stanzaInfo), varNewStr(STANZA_KEY_CIPHER_STR), varNewStr(STANZA_VALUE_CIPHER_NONE_STR));

        // If the backup.info file exists, get the database history information (newest to oldest) and corresponding archive
        if (info != NULL)
        {
            // Determine if encryption is enabled by checking for a cipher passphrase.  This is not ideal since it does not tell us
            // what type of encryption is in use, but to figure that out we need a way to query the (possibly) remote repo to find
            // out.  No such mechanism exists so this will have to do for now.  Probably the easiest thing to do is store the
            // cipher type in the info file.
            if (infoPgCipherPass(infoBackupPg(info)) != NULL)
                kvPut(varKv(stanzaInfo), varNewStr(STANZA_KEY_CIPHER_STR), varNewStr(CIPHER_TYPE_AES_256_CBC_STR));

            // Get data for all existing backups for this stanza
            StringList *neededWalSegs = backupList(backupSection, info);
            const String *minNeededWalSeg = strLstGet(neededWalSegs, 0);
            const String *maxNeededWalSeg = strLstGet(neededWalSegs, 1);

            for (unsigned int pgIdx = infoPgDataTotal(infoBackupPg(info)) - 1; (int)pgIdx >= 0; pgIdx--)
            {
                InfoPgData pgData = infoPgData(infoBackupPg(info), pgIdx);
                Variant *pgInfo = varNewKv();

                kvPut(varKv(pgInfo), varNewStr(DB_KEY_ID_STR), varNewUInt64(pgData.id));
                kvPut(varKv(pgInfo), varNewStr(DB_KEY_SYSTEM_ID_STR), varNewUInt64(pgData.systemId));
                kvPut(varKv(pgInfo), varNewStr(DB_KEY_VERSION_STR), varNewStr(pgVersionToStr(pgData.version)));

                varLstAdd(dbSection, pgInfo);

                // Get the archive info for the DB from the archive.info file
                InfoArchive *info = infoArchiveNew(
                    storageRepo(), strNewFmt(STORAGE_PATH_ARCHIVE "/%s/%s", strPtr(stanzaListName), INFO_ARCHIVE_FILE),  false,
                    cipherType(cfgOptionStr(cfgOptRepoCipherType)), cfgOptionStr(cfgOptRepoCipherPass));
                StringList *archiveDbListReturnList = archiveDbList(stanzaListName, &pgData, archiveSection, info, (pgIdx == 0 ? true : false));
                String *archiveId = strLstGet(archiveDbListReturnList, 0);
                String *archiveStop = strLstGet(archiveDbListReturnList, 1);

                // Does minNeededWalSeg and maxNeededWalSeg exists on disk? Is archiveStop >= maxNeededWalSeg?
                if(minNeededWalSeg && maxNeededWalSeg && archiveStop)
                {
                    String *foundMinNeededWalSeg = walSegmentFind(storageRepo(), stanzaFound ? archiveId : strNewFmt("%s/%s", strPtr(stanzaListName), strPtr(archiveId)), minNeededWalSeg);
                    String *foundMaxNeededWalSeg = walSegmentFind(storageRepo(), stanzaFound ? archiveId : strNewFmt("%s/%s", strPtr(stanzaListName), strPtr(archiveId)), maxNeededWalSeg);
                    if (foundMinNeededWalSeg == NULL || foundMaxNeededWalSeg == NULL || strCmp(archiveStop, maxNeededWalSeg) < 0)
                        stanzaStatus(INFO_STANZA_STATUS_CODE_MISSING_WAL_SEG, INFO_STANZA_STATUS_MESSAGE_MISSING_WAL_SEG_STR, stanzaInfo);

                    // TODO: Validate the chain of wal segments between minNeededWalSeg and maxNeededWalSeg?

                    // TODO: Validate the chain of wal segments between maxNeededWalSeg and archiveStop?
                }
            }
        }

        // Add the database history, backup and archive sections to the stanza info
        kvPut(varKv(stanzaInfo), varNewStr(STANZA_KEY_DB_STR), varNewVarLst(dbSection));
        kvPut(varKv(stanzaInfo), varNewStr(STANZA_KEY_BACKUP_STR), varNewVarLst(backupSection));
        kvPut(varKv(stanzaInfo), varNewStr(KEY_ARCHIVE_STR), varNewVarLst(archiveSection));

        // If a status has not already been set and there are no backups then set status to no backup
        if (kvGet(varKv(stanzaInfo), varNewStr(STANZA_KEY_STATUS_STR)) == NULL &&
            varLstSize(kvGetList(varKv(stanzaInfo), varNewStr(STANZA_KEY_BACKUP_STR))) == 0)
        {
            stanzaStatus(INFO_STANZA_STATUS_CODE_NO_BACKUP, INFO_STANZA_STATUS_MESSAGE_NO_BACKUP_STR, stanzaInfo);
        }

        // If a status has still not been set then set it to OK
        if (kvGet(varKv(stanzaInfo), varNewStr(STANZA_KEY_STATUS_STR)) == NULL)
            stanzaStatus(INFO_STANZA_STATUS_CODE_OK, INFO_STANZA_STATUS_MESSAGE_OK_STR, stanzaInfo);

        varLstAdd(result, stanzaInfo);
    }

    // If looking for a specific stanza and it was not found, set minimum info and the status
    if (stanza != NULL && !stanzaFound)
    {
        Variant *stanzaInfo = varNewKv();

        kvPut(varKv(stanzaInfo), varNewStr(STANZA_KEY_NAME_STR), varNewStr(stanza));

        kvPut(varKv(stanzaInfo), varNewStr(STANZA_KEY_DB_STR), varNewVarLst(varLstNew()));
        kvPut(varKv(stanzaInfo), varNewStr(STANZA_KEY_BACKUP_STR), varNewVarLst(varLstNew()));

        stanzaStatus(INFO_STANZA_STATUS_CODE_MISSING_STANZA_PATH, INFO_STANZA_STATUS_MESSAGE_MISSING_STANZA_PATH_STR, stanzaInfo);
        varLstAdd(result, stanzaInfo);
    }

    FUNCTION_TEST_RETURN(result);
}

/***********************************************************************************************************************************
Format the text output for each database of the stanza.
***********************************************************************************************************************************/
static void
formatTextDb(const KeyValue *stanzaInfo, String *resultStr)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(KEY_VALUE, stanzaInfo);
        FUNCTION_TEST_PARAM(STRING, resultStr);
    FUNCTION_TEST_END();

    ASSERT(stanzaInfo != NULL);

    VariantList *dbSection = kvGetList(stanzaInfo, varNewStr(STANZA_KEY_DB_STR));
    VariantList *archiveSection = kvGetList(stanzaInfo, varNewStr(KEY_ARCHIVE_STR));
    VariantList *backupSection = kvGetList(stanzaInfo, varNewStr(STANZA_KEY_BACKUP_STR));

    // For each database (working from oldest to newest) find the corresponding archive and backup info
    for (unsigned int dbIdx = 0; dbIdx < varLstSize(dbSection); dbIdx++)
    {
        KeyValue *pgInfo = varKv(varLstGet(dbSection, dbIdx));
        uint64_t dbId = varUInt64(kvGet(pgInfo, varNewStr(DB_KEY_ID_STR)));

        // List is ordered so 0 is always the current DB index
        if (dbIdx == varLstSize(dbSection) - 1)
            strCat(resultStr, "\n    db (current)");

        // Get the min/max archive information for the database
        String *archiveResult = strNew("");

        for (unsigned int archiveIdx = 0; archiveIdx < varLstSize(archiveSection); archiveIdx++)
        {
            KeyValue *archiveInfo = varKv(varLstGet(archiveSection, archiveIdx));
            KeyValue *archiveDbInfo = varKv(kvGet(archiveInfo, varNewStr(KEY_DATABASE_STR)));
            uint64_t archiveDbId = varUInt64(kvGet(archiveDbInfo, varNewStr(DB_KEY_ID_STR)));

            if (archiveDbId == dbId)
            {
                strCatFmt(
                    archiveResult, "\n        wal archive min/max (%s): ",
                    strPtr(varStr(kvGet(archiveInfo, varNewStr(DB_KEY_ID_STR)))));

                // Get the archive min/max if there are any archives for the database
                if (kvGet(archiveInfo, varNewStr(ARCHIVE_KEY_MIN_STR)) != NULL)
                {
                    strCatFmt(
                        archiveResult, "%s/%s\n", strPtr(varStr(kvGet(archiveInfo, varNewStr(ARCHIVE_KEY_MIN_STR)))),
                        strPtr(varStr(kvGet(archiveInfo, varNewStr(ARCHIVE_KEY_MAX_STR)))));
                }
                else
                    strCat(archiveResult, "none present\n");
            }
        }

        // Get the information for each current backup
        String *backupResult = strNew("");

        for (unsigned int backupIdx = 0; backupIdx < varLstSize(backupSection); backupIdx++)
        {
            KeyValue *backupInfo = varKv(varLstGet(backupSection, backupIdx));
            KeyValue *backupDbInfo = varKv(kvGet(backupInfo, varNewStr(KEY_DATABASE_STR)));
            uint64_t backupDbId = varUInt64(kvGet(backupDbInfo, varNewStr(DB_KEY_ID_STR)));

            if (backupDbId == dbId)
            {
                strCatFmt(
                    backupResult, "\n        %s backup: %s\n", strPtr(varStr(kvGet(backupInfo, varNewStr(BACKUP_KEY_TYPE_STR)))),
                    strPtr(varStr(kvGet(backupInfo, varNewStr(BACKUP_KEY_LABEL_STR)))));

                KeyValue *timestampInfo = varKv(kvGet(backupInfo, varNewStr(BACKUP_KEY_TIMESTAMP_STR)));

                // Get and format the backup start/stop time
                static char timeBufferStart[20];
                static char timeBufferStop[20];
                time_t timeStart = (time_t) varUInt64(kvGet(timestampInfo, varNewStr(KEY_START_STR)));
                time_t timeStop = (time_t) varUInt64(kvGet(timestampInfo, varNewStr(KEY_STOP_STR)));

                strftime(timeBufferStart, 20, "%Y-%m-%d %H:%M:%S", localtime(&timeStart));
                strftime(timeBufferStop, 20, "%Y-%m-%d %H:%M:%S", localtime(&timeStop));

                strCatFmt(
                    backupResult, "            timestamp start/stop: %s / %s\n", timeBufferStart, timeBufferStop);
                strCat(backupResult, "            wal start/stop: ");

                KeyValue *archiveDBackupInfo = varKv(kvGet(backupInfo, varNewStr(KEY_ARCHIVE_STR)));

                if (kvGet(archiveDBackupInfo, varNewStr(KEY_START_STR)) != NULL &&
                    kvGet(archiveDBackupInfo, varNewStr(KEY_STOP_STR)) != NULL)
                {
                    strCatFmt(backupResult, "%s / %s\n", strPtr(varStr(kvGet(archiveDBackupInfo, varNewStr(KEY_START_STR)))),
                        strPtr(varStr(kvGet(archiveDBackupInfo, varNewStr(KEY_STOP_STR)))));
                }
                else
                    strCat(backupResult, "n/a\n");

                KeyValue *info = varKv(kvGet(backupInfo, varNewStr(BACKUP_KEY_INFO_STR)));

                strCatFmt(
                    backupResult, "            database size: %s, backup size: %s\n",
                    strPtr(strSizeFormat(varUInt64Force(kvGet(info, varNewStr(KEY_SIZE_STR))))),
                    strPtr(strSizeFormat(varUInt64Force(kvGet(info, varNewStr(KEY_DELTA_STR))))));

                KeyValue *repoInfo = varKv(kvGet(info, varNewStr(INFO_KEY_REPOSITORY_STR)));

                strCatFmt(
                    backupResult, "            repository size: %s, repository backup size: %s\n",
                    strPtr(strSizeFormat(varUInt64Force(kvGet(repoInfo, varNewStr(KEY_SIZE_STR))))),
                    strPtr(strSizeFormat(varUInt64Force(kvGet(repoInfo, varNewStr(KEY_DELTA_STR))))));

                if (kvGet(backupInfo, varNewStr(BACKUP_KEY_REFERENCE_STR)) != NULL)
                {
                    StringList *referenceList = strLstNewVarLst(varVarLst(kvGet(backupInfo, varNewStr(BACKUP_KEY_REFERENCE_STR))));
                    strCatFmt(backupResult, "            backup reference list: %s\n", strPtr(strLstJoin(referenceList, ", ")));
                }
            }
        }

        // If there is data to display, then display it.
        if (strSize(archiveResult) > 0 || strSize(backupResult) > 0)
        {
            if (dbIdx != varLstSize(dbSection) - 1)
                strCat(resultStr, "\n    db (prior)");

            if (strSize(archiveResult) > 0)
                strCat(resultStr, strPtr(archiveResult));

            if (strSize(backupResult) > 0)
                strCat(resultStr, strPtr(backupResult));
        }
    }
    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Render the information for the stanza based on the command parameters.
***********************************************************************************************************************************/
static String *
infoRender(void)
{
    FUNCTION_LOG_VOID(logLevelDebug);

    String *result = NULL;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Get stanza if specified
        const String *stanza = cfgOptionTest(cfgOptStanza) ? cfgOptionStr(cfgOptStanza) : NULL;

        // Get a list of stanzas in the backup directory.
        StringList *stanzaList = storageListP(storageRepo(), STORAGE_PATH_BACKUP_STR, .errorOnMissing = false);
        VariantList *infoList = varLstNew();
        String *resultStr = strNew("");

        // If the backup storage exists, then search for and process any stanzas
        if (stanzaList != NULL && strLstSize(stanzaList) > 0)
            infoList = stanzaInfoList(stanza, stanzaList);

        // Format text output
        if (strEq(cfgOptionStr(cfgOptOutput), CFGOPTVAL_INFO_OUTPUT_TEXT_STR))
        {
            // Process any stanza directories
            if  (varLstSize(infoList) > 0)
            {
                for (unsigned int stanzaIdx = 0; stanzaIdx < varLstSize(infoList); stanzaIdx++)
                {
                    KeyValue *stanzaInfo = varKv(varLstGet(infoList, stanzaIdx));

                    // Add a carriage return between stanzas
                    if (stanzaIdx > 0)
                        strCatFmt(resultStr, "\n");

                    // Stanza name and status
                    strCatFmt(
                        resultStr, "stanza: %s\n    status: ", strPtr(varStr(kvGet(stanzaInfo, varNewStr(STANZA_KEY_NAME_STR)))));

                    // If an error has occurred, provide the information that is available and move onto next stanza
                    KeyValue *stanzaStatus = varKv(kvGet(stanzaInfo, varNewStr(STANZA_KEY_STATUS_STR)));
                    int statusCode = varInt(kvGet(stanzaStatus, varNewStr(STATUS_KEY_CODE_STR)));

                    if (statusCode != INFO_STANZA_STATUS_CODE_OK)
                    {
                        strCatFmt(
                            resultStr, "%s (%s)\n", strPtr(INFO_STANZA_STATUS_ERROR),
                            strPtr(varStr(kvGet(stanzaStatus, varNewStr(STATUS_KEY_MESSAGE_STR)))));

                        if (statusCode == INFO_STANZA_STATUS_CODE_MISSING_STANZA_DATA ||
                            statusCode == INFO_STANZA_STATUS_CODE_NO_BACKUP)
                        {
                            strCatFmt(
                                resultStr, "    cipher: %s\n", strPtr(varStr(kvGet(stanzaInfo, varNewStr(STANZA_KEY_CIPHER_STR)))));

                            // If there is a backup.info file but no backups, then process the archive info
                            if (statusCode == INFO_STANZA_STATUS_CODE_NO_BACKUP)
                                formatTextDb(stanzaInfo, resultStr);
                        }

                        if (statusCode != INFO_STANZA_STATUS_CODE_MISSING_WAL_SEG)
                            continue;
                    }
                    else
                        strCatFmt(resultStr, "%s\n", strPtr(INFO_STANZA_STATUS_OK));

                    // Cipher
                    strCatFmt(resultStr, "    cipher: %s\n",
                        strPtr(varStr(kvGet(stanzaInfo, varNewStr(STANZA_KEY_CIPHER_STR)))));

                    formatTextDb(stanzaInfo, resultStr);
                }
            }
            else
                resultStr = strNew("No stanzas exist in the repository.\n");
        }
        // Format json output
        else
            resultStr = varToJson(varNewVarLst(infoList), 4);

        memContextSwitch(MEM_CONTEXT_OLD());
        result = strDup(resultStr);
        memContextSwitch(MEM_CONTEXT_TEMP());
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(STRING, result);
}

/***********************************************************************************************************************************
Render info and output to stdout
***********************************************************************************************************************************/
void
cmdInfo(void)
{
    FUNCTION_LOG_VOID(logLevelDebug);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        ioHandleWriteOneStr(STDOUT_FILENO, infoRender());
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}
