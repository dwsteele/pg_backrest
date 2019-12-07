/***********************************************************************************************************************************
Backup Command
***********************************************************************************************************************************/
#include "build.auto.h"

#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "command/control/common.h"
#include "command/backup/backup.h"
#include "command/backup/common.h"
#include "command/backup/file.h"
#include "command/backup/protocol.h"
#include "command/check/common.h"
#include "command/stanza/common.h"
#include "common/crypto/cipherBlock.h"
#include "common/compress/gzip/common.h"
#include "common/compress/gzip/compress.h"
#include "common/debug.h"
#include "common/io/filter/size.h"
#include "common/log.h"
#include "common/time.h"
#include "common/type/convert.h"
#include "config/config.h"
#include "db/helper.h"
#include "info/infoBackup.h"
#include "info/manifest.h"
#include "postgres/interface.h"
#include "postgres/version.h"
#include "protocol/helper.h"
#include "protocol/parallel.h"
#include "storage/helper.h"
#include "version.h"

/***********************************************************************************************************************************
Backup constants
***********************************************************************************************************************************/
#define BACKUP_PATH_HISTORY                                         "backup.history"
#define BACKUP_LINK_LATEST                                          "latest"

/**********************************************************************************************************************************
Generate a unique backup label that does not contain a timestamp from a previous backup
***********************************************************************************************************************************/
// Helper to format the backup label
static String *
backupLabelFormat(BackupType type, const String *backupLabelLast, time_t timestamp)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(ENUM, type);
        FUNCTION_LOG_PARAM(STRING, backupLabelLast);
        FUNCTION_LOG_PARAM(TIME, timestamp);
    FUNCTION_LOG_END();

    ASSERT((type == backupTypeFull && backupLabelLast == NULL) || (type != backupTypeFull && backupLabelLast != NULL));
    ASSERT(timestamp > 0);

    // Format the timestamp
    char buffer[16];
    THROW_ON_SYS_ERROR(
        strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", localtime(&timestamp)) == 0, AssertError, "unable to format time");

    // If full label
    String *result = NULL;

    if (type == backupTypeFull)
    {
        result = strNewFmt("%sF", buffer);
    }
    // Else diff or incr label
    else
    {
        // Get the full backup portion of the last backup label
        result = strSubN(backupLabelLast, 0, 16);

        // Append the diff/incr timestamp
        strCatFmt(result, "_%s%s", buffer, type == backupTypeDiff ? "D" : "I");
    }

    FUNCTION_LOG_RETURN(STRING, result);
}

static String *
backupLabelCreate(BackupType type, const String *backupLabelLast, time_t timestamp)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(ENUM, type);
        FUNCTION_LOG_PARAM(STRING, backupLabelLast);
        FUNCTION_LOG_PARAM(TIME, timestamp);
    FUNCTION_LOG_END();

    ASSERT((type == backupTypeFull && backupLabelLast == NULL) || (type != backupTypeFull && backupLabelLast != NULL));
    ASSERT(timestamp > 0);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        while (true)
        {
            // Get the current year to use for searching the history.  Put this in the loop since we could theoretically roll over to
            // a new year while searching for a valid label.
            char year[5];
            THROW_ON_SYS_ERROR(
                strftime(year, sizeof(year), "%Y", localtime(&timestamp)) == 0, AssertError, "unable to format year");

            // Create regular expression for search.  We can't just search on the label because we want to be sure that no other
            // backup uses the same timestamp, no matter what type it is.
            String *timestampStr = strSubN(backupLabelFormat(backupTypeFull, NULL, timestamp), 0, 15);
            String *timestampExp = strNewFmt("(^%sF$)|(_%s(D|I)$)", strPtr(timestampStr), strPtr(timestampStr));

            // Check for the timestamp in the backup path
            if (strLstSize(storageListP(storageRepo(), STORAGE_REPO_BACKUP_STR, .expression = timestampExp)) == 0)
            {
                // Now check in the backup.history
                String *historyPath = strNewFmt(STORAGE_REPO_BACKUP "/" BACKUP_PATH_HISTORY "/%s", year);
                String *historyExp = strNewFmt(
                    "(^%sF\\.manifest\\." GZIP_EXT "$)|(_%s(D|I)\\.manifest\\." GZIP_EXT "$)", strPtr(timestampStr),
                    strPtr(timestampStr));

                // If the timestamp also does not appear in the history then it is safe to use
                if (strLstSize(storageListP(storageRepo(), historyPath, .expression = historyExp)) == 0)
                    break;
            }

            // ??? This is likely to work in virtually all cases, but it would be far better to make sure that the time is later
            // than any other backup label.
            timestamp++;
        }
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(STRING, backupLabelFormat(type, backupLabelLast, timestamp));
}

/***********************************************************************************************************************************
Get the postgres database and storage objects
***********************************************************************************************************************************/
#define FUNCTION_LOG_BACKUP_DATA_TYPE                                                                                              \
    BackupData *
#define FUNCTION_LOG_BACKUP_DATA_FORMAT(value, buffer, bufferSize)                                                                 \
    objToLog(value, "BackupData", buffer, bufferSize)

typedef struct BackupData
{
    unsigned int pgIdPrimary;                                       // Configuration id of the primary
    Db *dbPrimary;                                                  // Database connection to the primary
    const Storage *storagePrimary;                                  // Storage object for the primary
    const String *hostPrimary;                                      // Host name of the primary

    unsigned int pgIdStandby;                                       // Configuration id of the standby
    Db *dbStandby;                                                  // Database connection to the standby
    const Storage *storageStandby;                                  // Storage object for the standby
    const String *hostStandby;                                      // Host name of the standby

    unsigned int version;                                           // PostgreSQL version
    unsigned int pageSize;                                          // PostgreSQL page size
} BackupData;

static BackupData *
backupInit(const InfoBackup *infoBackup)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(INFO_BACKUP, infoBackup);
    FUNCTION_LOG_END();

    ASSERT(infoBackup != NULL);

    // Initialize for offline backup
    BackupData *result = memNew(sizeof(BackupData));
    *result = (BackupData){.pgIdPrimary = 1};

    // Check that the PostgreSQL version supports backup from standby. The check is done using the stanza info because pg_control
    // cannot be loaded until a primary is found -- which will also lead to an error if the version does not support standby. If the
    // pg_control version does not match the stanza version then there will be an error further down.
    InfoPgData infoPg = infoPgDataCurrent(infoBackupPg(infoBackup));

    if (cfgOptionBool(cfgOptOnline) && cfgOptionBool(cfgOptBackupStandby) && infoPg.version < PG_VERSION_BACKUP_STANDBY)
    {
        THROW_FMT(
            ConfigError, "option '" CFGOPT_BACKUP_STANDBY "' not valid for " PG_NAME " < %s",
            strPtr(pgVersionToStr(PG_VERSION_BACKUP_STANDBY)));
    }

    if (!cfgOptionBool(cfgOptOnline) && cfgOptionBool(cfgOptBackupStandby))
    {
        LOG_WARN(
            "option " CFGOPT_BACKUP_STANDBY " is enabled but backup is offline - backups will be performed from the primary");
        cfgOptionSet(cfgOptBackupStandby, cfgSourceParam, BOOL_FALSE_VAR);
    }

    // Get database info when online
    if (cfgOptionBool(cfgOptOnline))
    {
        bool backupStandby = cfgOptionBool(cfgOptBackupStandby);
        DbGetResult dbInfo = dbGet(!backupStandby, true, backupStandby);

        result->pgIdPrimary = dbInfo.primaryId;
        result->dbPrimary = dbInfo.primary;

        if (backupStandby)
        {
            ASSERT(dbInfo.standbyId != 0);

            result->pgIdStandby = dbInfo.standbyId;
            result->dbStandby = dbInfo.standby;
            result->storageStandby = storagePgId(result->pgIdStandby);
            result->hostStandby = cfgOptionStr(cfgOptPgHost + result->pgIdStandby - 1);
        }
    }

    // Add primary info
    result->storagePrimary = storagePgId(result->pgIdPrimary);
    result->hostPrimary = cfgOptionStr(cfgOptPgHost + result->pgIdPrimary - 1);

    // Get pg_control info from the primary
    PgControl pgControl = pgControlFromFile(result->storagePrimary);

    result->version = pgControl.version;
    result->pageSize = pgControl.pageSize;

    // Validate pg_control info against the stanza
    if (result->version != infoPg.version || pgControl.systemId != infoPg.systemId)
    {
        THROW_FMT(
            BackupMismatchError,
            PG_NAME " version %s, system-id %" PRIu64 " do not match stanza version %s, system-id %" PRIu64,
            strPtr(pgVersionToStr(pgControl.version)), pgControl.systemId, strPtr(pgVersionToStr(infoPg.version)),
            infoPg.systemId);
    }

    // Only allow stop auto in PostgreSQL >= 9.3 and <= 9.5
    if (cfgOptionBool(cfgOptStopAuto) && (result->version < PG_VERSION_93 || result->version > PG_VERSION_95))
    {
        LOG_WARN(
            CFGOPT_STOP_AUTO " option is only available in " PG_NAME " >= " PG_VERSION_93_STR " and <= " PG_VERSION_95_STR);
        cfgOptionSet(cfgOptStopAuto, cfgSourceParam, BOOL_FALSE_VAR);
    }

    // Only allow start-fast option for PostgreSQL >= 8.4
    if (cfgOptionBool(cfgOptStartFast) && result->version < PG_VERSION_84)
    {
        LOG_WARN(CFGOPT_START_FAST " option is only available in " PG_NAME " >= " PG_VERSION_84_STR);
        cfgOptionSet(cfgOptStartFast, cfgSourceParam, BOOL_FALSE_VAR);
    }

    // If checksum page is not explicity set then automatically enable it when checksums are available
    if (!cfgOptionTest(cfgOptChecksumPage))
    {
        // If online then use the value in pg_control to set checksum-page
        if (cfgOptionBool(cfgOptOnline))
        {
            cfgOptionSet(cfgOptChecksumPage, cfgSourceParam, VARBOOL(pgControl.pageChecksum));
        }
        // Else set to false.  An offline cluster is likely to have false positives so better if the user enables manually
        else
            cfgOptionSet(cfgOptChecksumPage, cfgSourceParam, BOOL_FALSE_VAR);
    }
    // Else if checksums have been explicitly enabled but are not available then warn and reset. ??? We should be able to make this
    // determination when offline as well, but the integration tests don't write pg_control accurately enough to support it.
    else if (cfgOptionBool(cfgOptOnline) && !pgControl.pageChecksum && cfgOptionBool(cfgOptChecksumPage))
    {
        LOG_WARN(CFGOPT_CHECKSUM_PAGE " option set to true but checksums are not enabled on the cluster, resetting to false");
        cfgOptionSet(cfgOptChecksumPage, cfgSourceParam, BOOL_FALSE_VAR);
    }

    FUNCTION_LOG_RETURN(BACKUP_DATA, result);
}

/**********************************************************************************************************************************
Get time from the database or locally depending on online
***********************************************************************************************************************************/
static time_t
backupTime(BackupData *backupData, bool waitRemainder)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(BACKUP_DATA, backupData);
        FUNCTION_LOG_PARAM(BOOL, waitRemainder);
    FUNCTION_LOG_END();

    // Offline backups will just grab the time from the local system since the value of copyStart is not important in this context.
    // No worries about causing a delta backup since switching online will do that anyway.
    time_t result = time(NULL);

    // When online get the time from the database server
    if (cfgOptionBool(cfgOptOnline))
    {
        // Get time from the database
        TimeMSec timeMSec = dbTimeMSec(backupData->dbPrimary);
        result = (time_t)(timeMSec / MSEC_PER_SEC);

        // Sleep the remainder of the second when requested (this is so copyStart is not subject to one second resolution issues)
        if (waitRemainder)
        {
            sleepMSec(MSEC_PER_SEC - (timeMSec % MSEC_PER_SEC));

            // Check time again to be sure we slept long enough
            if (result >= (time_t)(dbTimeMSec(backupData->dbPrimary) / MSEC_PER_SEC))
                THROW(AssertError, "invalid sleep for online backup time with wait remainder");
        }
    }

    FUNCTION_LOG_RETURN(TIME, result);
}

/***********************************************************************************************************************************
Create an incremental backup if type is not full and a prior backup exists
***********************************************************************************************************************************/
// Helper to find a compatible prior backup
static Manifest *
backupBuildIncrPrior(const InfoBackup *infoBackup)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(INFO_BACKUP, infoBackup);
    FUNCTION_LOG_END();

    ASSERT(infoBackup != NULL);

    Manifest *result = NULL;

    // No incremental if backup type is full
    BackupType type = backupType(cfgOptionStr(cfgOptType));

    if (type != backupTypeFull)
    {
        MEM_CONTEXT_TEMP_BEGIN()
        {
            InfoPgData infoPg = infoPgDataCurrent(infoBackupPg(infoBackup));
            const String *backupLabelPrior = NULL;
            unsigned int backupTotal = infoBackupDataTotal(infoBackup);

            for (unsigned int backupIdx = backupTotal - 1; backupIdx < backupTotal; backupIdx--)
            {
                 InfoBackupData backupPrior = infoBackupData(infoBackup, backupIdx);

                 // The prior backup for a diff must be full
                 if (type == backupTypeDiff && backupType(backupPrior.backupType) != backupTypeFull)
                    continue;

                // The backups must come from the same cluster ??? This should enable delta instead
                if (infoPg.id != backupPrior.backupPgId)
                    continue;

                // This backup is a candidate for prior
                backupLabelPrior = strDup(backupPrior.backupLabel);
                break;
            }

            // If there is a prior backup then check that options for the new backup are compatible
            if (backupLabelPrior != NULL)
            {
                result = manifestLoadFile(
                    storageRepo(), strNewFmt(STORAGE_REPO_BACKUP "/%s/" BACKUP_MANIFEST_FILE, strPtr(backupLabelPrior)),
                    cipherType(cfgOptionStr(cfgOptRepoCipherType)), infoPgCipherPass(infoBackupPg(infoBackup)));
                const ManifestData *manifestPriorData = manifestData(result);

                LOG_INFO_FMT(
                    "last backup label = %s, version = %s", strPtr(manifestData(result)->backupLabel),
                    strPtr(manifestData(result)->backrestVersion));

                // Warn if compress option changed
                if (cfgOptionBool(cfgOptCompress) != manifestPriorData->backupOptionCompress)
                {
                    LOG_WARN_FMT(
                        "%s backup cannot alter compress option to '%s', reset to value in %s",
                        strPtr(cfgOptionStr(cfgOptType)), cvtBoolToConstZ(cfgOptionBool(cfgOptCompress)),
                        strPtr(backupLabelPrior));
                    cfgOptionSet(cfgOptCompress, cfgSourceParam, VARBOOL(manifestPriorData->backupOptionCompress));
                }

                // Warn if hardlink option changed ??? Doesn't seem like this is needed?  Hardlinks are always to a directory that
                // is guaranteed to contain a real file -- like references.  Also annoying that if the full backup was not
                // hardlinked then an diff/incr can't be because we need more testing.
                if (cfgOptionBool(cfgOptRepoHardlink) != manifestPriorData->backupOptionHardLink)
                {
                    LOG_WARN_FMT(
                        "%s backup cannot alter hardlink option to '%s', reset to value in %s",
                        strPtr(cfgOptionStr(cfgOptType)), cvtBoolToConstZ(cfgOptionBool(cfgOptRepoHardlink)),
                        strPtr(backupLabelPrior));
                    cfgOptionSet(cfgOptRepoHardlink, cfgSourceParam, VARBOOL(manifestPriorData->backupOptionHardLink));
                }

                // If not defined this backup was done in a version prior to page checksums being introduced.  Just set
                // checksum-page to false and move on without a warning.  Page checksums will start on the next full backup.
                if (manifestData(result)->backupOptionChecksumPage == NULL)
                {
                    cfgOptionSet(cfgOptChecksumPage, cfgSourceParam, BOOL_FALSE_VAR);
                }
                // Don't allow the checksum-page option to change in a diff or incr backup.  This could be confusing as only
                // certain files would be checksummed and the list could be incomplete during reporting.
                else
                {
                    bool checksumPagePrior = varBool(manifestData(result)->backupOptionChecksumPage);

                    // Warn if an incompatible setting was explicitly requested
                    if (checksumPagePrior != cfgOptionBool(cfgOptChecksumPage))
                    {
                        LOG_WARN_FMT(
                            "%s backup cannot alter '" CFGOPT_CHECKSUM_PAGE "' option to '%s', reset to '%s' from %s",
                            strPtr(cfgOptionStr(cfgOptType)), cvtBoolToConstZ(cfgOptionBool(cfgOptChecksumPage)),
                            cvtBoolToConstZ(checksumPagePrior), strPtr(manifestData(result)->backupLabel));
                    }

                    cfgOptionSet(cfgOptChecksumPage, cfgSourceParam, VARBOOL(checksumPagePrior));
                }

                manifestMove(result, MEM_CONTEXT_OLD());
            }
            else
            {
                LOG_WARN_FMT("no prior backup exists, %s backup has been changed to full", strPtr(cfgOptionStr(cfgOptType)));
                cfgOptionSet(cfgOptType, cfgSourceParam, VARSTR(backupTypeStr(backupTypeFull)));
            }
        }
        MEM_CONTEXT_TEMP_END();
    }

    FUNCTION_LOG_RETURN(MANIFEST, result);
}

static bool
backupBuildIncr(const InfoBackup *infoBackup, Manifest *manifest, Manifest *manifestPrior, const String *archiveStart)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(INFO_BACKUP, infoBackup);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
        FUNCTION_LOG_PARAM(MANIFEST, manifestPrior);
        FUNCTION_LOG_PARAM(STRING, archiveStart);
    FUNCTION_LOG_END();

    ASSERT(infoBackup != NULL);
    ASSERT(manifest != NULL);

    bool result = false;

    // No incremental if no prior manifest
    if (manifestPrior != NULL)
    {
        MEM_CONTEXT_TEMP_BEGIN()
        {
            // Move the manifest to this context so it will be freed when we are done
            manifestMove(manifestPrior, MEM_CONTEXT_TEMP());

            // Build incremental manifest
            manifestBuildIncr(manifest, manifestPrior, backupType(cfgOptionStr(cfgOptType)), archiveStart);

            // Set the cipher subpass from prior manifest since we want a single subpass for the entire backup set
            manifestCipherSubPassSet(manifest, manifestCipherSubPass(manifestPrior));

            // Incremental was built
            result = true;
        }
        MEM_CONTEXT_TEMP_END();
    }

    FUNCTION_LOG_RETURN(BOOL, result);
}

/***********************************************************************************************************************************
Check for a backup that can be resumed and merge into the manifest if found
***********************************************************************************************************************************/
typedef struct BackupResumeData
{
    Manifest *manifest;                                             // New manifest
    const Manifest *manifestResume;                                 // Resumed manifest
    const bool compressed;                                          // Is the backup compressed?
    const bool delta;                                               // Is this a delta backup?
    const String *backupPath;                                       // Path to the current level of the backup being cleaned
    const String *manifestParentName;                               // Parent manifest name used to construct manifest name
} BackupResumeData;

// Callback to clean invalid paths/files/links out of the repo
void backupResumeCallback(void *data, const StorageInfo *info)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM_P(VOID, data);
        FUNCTION_TEST_PARAM(STORAGE_INFO, *storageInfo);
    FUNCTION_TEST_END();

    ASSERT(data != NULL);
    ASSERT(info != NULL);

    BackupResumeData *resumeData = data;

    // Skip all . paths because they have already been handled on the previous level of recursion
    if (strEq(info->name, DOT_STR))
    {
        FUNCTION_TEST_RETURN_VOID();
        return;
    }

    // Skip backup.manifest.copy -- it will never be in the manifest
    if (resumeData->manifestParentName == NULL && strEqZ(info->name, BACKUP_MANIFEST_FILE INFO_COPY_EXT))
    {
        FUNCTION_TEST_RETURN_VOID();
        return;
    }

    // Build the name used to lookup files in the manifest
    const String *manifestName = resumeData->manifestParentName != NULL ?
        strNewFmt("%s/%s", strPtr(resumeData->manifestParentName), strPtr(info->name)) : info->name;

    // Build the backup path used to remove files/links/paths that are invalid
    const String *backupPath = strNewFmt("%s/%s", strPtr(resumeData->backupPath), strPtr(info->name));

    // Process file types
    switch (info->type)
    {
        // Check paths
        // -------------------------------------------------------------------------------------------------------------------------
        case storageTypePath:
        {
            // If the path was not found remove it
            if (manifestPathFindDefault(resumeData->manifest, manifestName, NULL) == NULL)
            {
                LOG_DETAIL_FMT("remove path '%s' from resumed backup", strPtr(storagePathP(storageRepo(), backupPath)));
                storagePathRemoveP(storageRepoWrite(), backupPath, .recurse = true);
            }
            // Else recurse into the path
            {
                BackupResumeData resumeDataSub = *resumeData;
                resumeDataSub.manifestParentName = manifestName;
                resumeDataSub.backupPath = backupPath;

                storageInfoListP(
                    storageRepo(), resumeDataSub.backupPath, backupResumeCallback, &resumeDataSub, .sortOrder = sortOrderAsc);
            }

            break;
        }

        // Check files
        // -------------------------------------------------------------------------------------------------------------------------
        case storageTypeFile:
        {
            // If the backup is compressed then strip off the extension before doing the lookup
            if (resumeData->compressed)
                manifestName = strSubN(manifestName, 0, strSize(manifestName) - sizeof(GZIP_EXT));

            // Find the file in both manifests
            const ManifestFile *file = manifestFileFindDefault(resumeData->manifest, manifestName, NULL);
            const ManifestFile *fileResume = manifestFileFindDefault(resumeData->manifestResume, manifestName, NULL);

            // Check if the file can be resumed or must be removed
            const char *removeReason = NULL;

            if (file == NULL)
                removeReason = "missing in manifest";
            else if (file->reference != NULL)
                removeReason = "reference in manifest";
            else if (fileResume == NULL)
                removeReason = "missing in resumed manifest";
            // CSHANG -- this is new an means we don't having to worry about checking for hardlinks on resume because a hardlink
            // will always have a reference.
            else if (fileResume->reference != NULL)
                removeReason = "reference in resumed manifest";
            else if (fileResume->checksumSha1[0] == '\0')
                removeReason = "no checksum in resumed manifest";
            else if (file->size != fileResume->size)
                removeReason = "mismatched size";
            else if (!resumeData->delta && file->timestamp != fileResume->timestamp)
                removeReason = "mismatched timestamp";
            else if (file->size == 0)
                // ??? don't resume zero size files because Perl wouldn't -- this can be removed after the migration)
                removeReason = "zero size";
            else
            {
                manifestFileUpdate(
                    resumeData->manifest, manifestName, file->size, fileResume->sizeRepo, fileResume->checksumSha1, NULL,
                    fileResume->checksumPage, fileResume->checksumPageError, fileResume->checksumPageErrorList);
            }

            // Remove the file if it could not be resumed
            if (removeReason != NULL)
            {
                LOG_DETAIL_FMT(
                    "remove file '%s' from resumed backup (%s)", strPtr(storagePathP(storageRepo(), backupPath)), removeReason);
                storageRemoveP(storageRepoWrite(), backupPath);
            }

            break;
        }

        // Remove links.  We could check that the link has not changed and preserve it but it doesn't seem worth the extra testing.
        // The link will be recreated during the backup if needed.
        // -------------------------------------------------------------------------------------------------------------------------
        case storageTypeLink:
        {
            storageRemoveP(storageRepoWrite(), backupPath);
            break;
        }

        // Remove special files
        // -------------------------------------------------------------------------------------------------------------------------
        case storageTypeSpecial:
        {
            LOG_WARN_FMT("remove special file '%s' from resumed backup", strPtr(storagePathP(storageRepo(), backupPath)));
            storageRemoveP(storageRepoWrite(), backupPath);
            break;
        }
    }

    FUNCTION_TEST_RETURN_VOID();
}

// Helper to find a resumable backup
static const Manifest *
backupResumeFind(const Manifest *manifest, const String *cipherPass)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
        FUNCTION_TEST_PARAM(STRING, cipherPass);
    FUNCTION_LOG_END();

    ASSERT(manifest != NULL);

    Manifest *result = NULL;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Only the last backup can be resumed
        const StringList *backupList = strLstSort(
            storageListP(
                storageRepo(), STRDEF(STORAGE_REPO_BACKUP),
                .expression = backupRegExpP(.full = true, .differential = true, .incremental = true)),
            sortOrderDesc);

        if (strLstSize(backupList) > 0)
        {
            const String *backupLabel = strLstGet(backupList, 0);
            const String *manifestFile = strNewFmt(STORAGE_REPO_BACKUP "/%s/" BACKUP_MANIFEST_FILE, strPtr(backupLabel));

            // Resumable backups have a copy of the manifest but no main
            if (storageExistsP(storageRepo(), strNewFmt("%s" INFO_COPY_EXT, strPtr(manifestFile))) &&
                !storageExistsP(storageRepo(), manifestFile))
            {
                bool usable = false;
                const String *reason = STRDEF("resume is disabled");
                Manifest *manifestResume = NULL;

                // Attempt to read the manifest file in the resumable backup to see if it can be used.  If any error at all occurs
                // then the backup will be considered unusable and a resume will not be attempted.
                if (cfgOptionBool(cfgOptResume))
                {
                    reason = strNewFmt("unable to read %s" INFO_COPY_EXT, strPtr(manifestFile));

                    TRY_BEGIN()
                    {
                        manifestResume = manifestLoadFile(
                            storageRepo(), manifestFile, cipherType(cfgOptionStr(cfgOptRepoCipherType)), cipherPass);
                        const ManifestData *manifestResumeData = manifestData(manifestResume);

                        // Check pgBackRest version. This allows the resume implementation to be changed with each version of
                        // pgBackRest at the expense of users losing a resumable back after an upgrade, which seems worth the cost.
                        if (!strEq(manifestResumeData->backrestVersion, manifestData(manifest)->backrestVersion))
                        {
                            reason = strNewFmt(
                                "new " PROJECT_NAME " version '%s' does not match resumable " PROJECT_NAME " version '%s'",
                                strPtr(manifestData(manifest)->backrestVersion), strPtr(manifestResumeData->backrestVersion));
                        }
                        // Check backup type ??? Do we really care about the backup type?
                        else if (manifestResumeData->backupType != backupType(cfgOptionStr(cfgOptType)))
                        {
                            reason = strNewFmt(
                                "new backup type '%s' does not match resumable backup type '%s'", strPtr(cfgOptionStr(cfgOptType)),
                                strPtr(backupTypeStr(manifestResumeData->backupType)));
                        }
                        // Check prior backup label ??? Do we really care about the prior backup label?
                        else if (!strEq(manifestResumeData->backupLabelPrior, manifestData(manifest)->backupLabelPrior))
                        {
                            reason = strNewFmt(
                                "new prior backup label '%s' does not match resumable prior backup label '%s'",
                                manifestResumeData->backupLabelPrior ? strPtr(manifestResumeData->backupLabelPrior) : "<undef>",
                                manifestData(manifest)->backupLabelPrior ?
                                    strPtr(manifestData(manifest)->backupLabelPrior) : "<undef>");
                        }
                        // Check compression.  Compression can't be changed between backups so resume won't work either.
                        else if (manifestResumeData->backupOptionCompress != cfgOptionBool(cfgOptCompress))
                        {
                            reason = strNewFmt(
                                "new compression '%s' does not match resumable compression '%s'",
                                cvtBoolToConstZ(cfgOptionBool(cfgOptCompress)),
                                cvtBoolToConstZ(manifestResumeData->backupOptionCompress));
                        }
                        // CSHANG -- no longer checking hardlinks here since we will only resume files that have no reference in the
                        // resumed manifest (which was true in Perl, too).  No reference means no hardlink, so hardlinks will all be
                        // removed during clean if they exist.
                        else
                            usable = true;
                    }
                    CATCH_ANY()
                    {
                    }
                    TRY_END();
                }

                // If the backup is usable then return the manifest
                if (usable)
                {
                    result = manifestMove(manifestResume, MEM_CONTEXT_OLD());
                }
                // Else warn and remove the unusable backup
                else
                {
                    LOG_WARN_FMT("backup '%s' cannot be resumed: %s", strPtr(backupLabel), strPtr(reason));

                    storagePathRemoveP(
                        storageRepoWrite(), strNewFmt(STORAGE_REPO_BACKUP "/%s", strPtr(backupLabel)), .recurse = true);
                }
            }
            }
        }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(MANIFEST, result);
}

static bool
backupResume(Manifest *manifest, const String *cipherPass)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
        FUNCTION_TEST_PARAM(STRING, cipherPass);
    FUNCTION_LOG_END();

    ASSERT(manifest != NULL);

    bool result = false;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        const Manifest *manifestResume = backupResumeFind(manifest, cipherPass);

        // If a resumable backup was found set the label and cipher subpass
        if (manifestResume)
        {
            // Resuming
            result = true;

            // Set the backup label to the resumed backup
            manifestBackupLabelSet(manifest, manifestData(manifestResume)->backupLabel);

            LOG_WARN_FMT(
                "resumable backup %s of same type exists -- remove invalid files and resume",
                strPtr(manifestData(manifest)->backupLabel));

            // If resuming a full backup then copy cipher subpass since it was used to encrypt the resumable files
            if (manifestData(manifest)->backupType == backupTypeFull)
                manifestCipherSubPassSet(manifest, manifestCipherSubPass(manifestResume));

            // Clean resumed backup
            BackupResumeData resumeData =
            {
                .manifest = manifest,
                .manifestResume = manifestResume,
                .compressed = cfgOptionBool(cfgOptCompress),
                .delta = cfgOptionBool(cfgOptDelta),
                .backupPath = strNewFmt(STORAGE_REPO_BACKUP "/%s", strPtr(manifestData(manifest)->backupLabel)),
            };

            storageInfoListP(storageRepo(), resumeData.backupPath, backupResumeCallback, &resumeData, .sortOrder = sortOrderAsc);
        }
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(BOOL, result);
}

/***********************************************************************************************************************************
Start the backup
***********************************************************************************************************************************/
typedef struct BackupStartResult
{
    String *lsn;
    String *walSegmentName;
    VariantList *dbList;
    VariantList *tablespaceList;
} BackupStartResult;

#define FUNCTION_LOG_BACKUP_START_RESULT_TYPE                                                                                      \
    BackupStartResult
#define FUNCTION_LOG_BACKUP_START_RESULT_FORMAT(value, buffer, bufferSize)                                                         \
    objToLog(&value, "BackupStartResult", buffer, bufferSize)

static BackupStartResult
backupStart(BackupData *backupData)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(BACKUP_DATA, backupData);
    FUNCTION_LOG_END();

    BackupStartResult result = {.lsn = NULL};

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // If this is an offline backup
        if (!cfgOptionBool(cfgOptOnline))
        {
            // Check if Postgres is running and if so only continue when forced
            if (storageExistsP(backupData->storagePrimary, PG_FILE_POSTMASTERPID_STR))
            {
                if (cfgOptionBool(cfgOptForce))
                {
                    LOG_WARN(
                        "--no-" CFGOPT_ONLINE " passed and " PG_FILE_POSTMASTERPID " exists but --" CFGOPT_FORCE " was passed so"
                        " backup will continue though it looks like the postmaster is running and the backup will probably not be"
                        " consistent");
                }
                else
                {
                    THROW(
                        PostmasterRunningError,
                        "--no-" CFGOPT_ONLINE " passed but " PG_FILE_POSTMASTERPID " exists - looks like the postmaster is running."
                        " Shutdown the postmaster and try again, or use --force.");
                }
            }
        }
        // Else start the backup normally
        else
        {
            // Check database configuration
            checkDbConfig(backupData->version, backupData->pgIdPrimary, backupData->dbPrimary, false);

            // Start backup
            LOG_INFO_FMT(
                "execute %sexclusive pg_start_backup(): backup begins after the %s checkpoint completes",
                backupData->version >= PG_VERSION_96 ? "non-" : "",
                cfgOptionBool(cfgOptStartFast) ? "requested immediate" : "next regular");

            DbBackupStartResult dbBackupStartResult = dbBackupStart(
                backupData->dbPrimary, cfgOptionBool(cfgOptStartFast), cfgOptionBool(cfgOptStopAuto));

            memContextSwitch(MEM_CONTEXT_OLD());
            result.lsn = strDup(dbBackupStartResult.lsn);
            result.walSegmentName = strDup(dbBackupStartResult.walSegmentName);
            result.dbList = dbList(backupData->dbPrimary);
            result.tablespaceList = dbTablespaceList(backupData->dbPrimary);
            memContextSwitch(MEM_CONTEXT_TEMP());

            LOG_INFO_FMT("backup start archive = %s, lsn = %s", strPtr(result.walSegmentName), strPtr(result.lsn));

            // Wait for replay on the standby to catch up
            if (cfgOptionBool(cfgOptBackupStandby))
            {
                LOG_INFO_FMT("wait for replay on the standby to reach %s", strPtr(result.lsn));
                dbReplayWait(backupData->dbStandby, result.lsn, (TimeMSec)cfgOptionDbl(cfgOptArchiveTimeout) * MSEC_PER_SEC);
                LOG_INFO_FMT("replay on the standby reached %s", strPtr(result.lsn));

                // The standby db object won't be used anymore so free it
                dbFree(backupData->dbStandby);

                // The standby protocol connection won't be used anymore so free it
                protocolRemoteFree(backupData->pgIdStandby);
            }
        }
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(BACKUP_START_RESULT, result);
}
/***********************************************************************************************************************************
Stop the backup
***********************************************************************************************************************************/
// Helper to write a file from a string to the repository and update the manifest
static void
backupFilePut(BackupData *backupData, Manifest *manifest, const String *name, const String *content)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(BACKUP_DATA, backupData);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
        FUNCTION_LOG_PARAM(STRING, name);
        FUNCTION_LOG_PARAM(STRING, content);
    FUNCTION_LOG_END();

    // Skip files with no content
    if (content != NULL)
    {
        MEM_CONTEXT_TEMP_BEGIN()
        {
            // Create file
            const String *manifestName = strNewFmt(MANIFEST_TARGET_PGDATA "/%s", strPtr(name));
            bool compress = cfgOptionBool(cfgOptCompress);

            StorageWrite *write = storageNewWriteP(
                storageRepoWrite(),
                strNewFmt(
                    STORAGE_REPO_BACKUP "/%s/%s%s", strPtr(manifestData(manifest)->backupLabel), strPtr(manifestName),
                    compress ? "." GZIP_EXT : ""),
                .compressible = true);

            IoFilterGroup *filterGroup = ioWriteFilterGroup(storageWriteIo(write));

            // Add SHA1 filter
            ioFilterGroupAdd(filterGroup, cryptoHashNew(HASH_TYPE_SHA1_STR));

            // Add compression
            if (compress)
            {
                ioFilterGroupAdd(
                    ioWriteFilterGroup(storageWriteIo(write)), gzipCompressNew((int)cfgOptionUInt(cfgOptCompressLevel), false));
            }

            // Add encryption filter if required
            cipherBlockFilterGroupAdd(
                filterGroup, cipherType(cfgOptionStr(cfgOptRepoCipherType)), cipherModeEncrypt, manifestCipherSubPass(manifest));

            // Add size filter last to calculate repo size
            ioFilterGroupAdd(filterGroup, ioSizeNew());

            // Write file
            storagePutP(write, BUFSTR(content));

            // Use base path to set ownership and mode
            const ManifestPath *basePath = manifestPathFind(manifest, MANIFEST_TARGET_PGDATA_STR);

            // Add to manifest
            ManifestFile file =
            {
                .name = manifestName,
                .primary = true,
                .mode = basePath->mode & (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH),
                .user = basePath->user,
                .group = basePath->group,
                .size = strSize(content),
                .sizeRepo = varUInt64Force(ioFilterGroupResult(filterGroup, SIZE_FILTER_TYPE_STR)),
                .timestamp = backupTime(backupData, false),
            };

            memcpy(
                file.checksumSha1, strPtr(varStr(ioFilterGroupResult(filterGroup, CRYPTO_HASH_FILTER_TYPE_STR))),
                HASH_TYPE_SHA1_SIZE_HEX + 1);

            manifestFileAdd(manifest, &file);

            LOG_DETAIL_FMT("wrote '%s' file returned from pg_stop_backup()", strPtr(name));
        }
        MEM_CONTEXT_TEMP_END();
    }

    FUNCTION_LOG_RETURN_VOID();
}

/*--------------------------------------------------------------------------------------------------------------------------------*/
typedef struct BackupStopResult
{
    String *lsn;
    String *walSegmentName;
} BackupStopResult;

#define FUNCTION_LOG_BACKUP_STOP_RESULT_TYPE                                                                                       \
    BackupStopResult
#define FUNCTION_LOG_BACKUP_STOP_RESULT_FORMAT(value, buffer, bufferSize)                                                          \
    objToLog(&value, "BackupStopResult", buffer, bufferSize)

static BackupStopResult
backupStop(BackupData *backupData, Manifest *manifest)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(BACKUP_DATA, backupData);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
    FUNCTION_LOG_END();

    BackupStopResult result = {.lsn = NULL};

    MEM_CONTEXT_TEMP_BEGIN()
    {
        if (cfgOptionBool(cfgOptOnline))
        {
            // Stop the backup
            LOG_INFO_FMT(
                "execute %sexclusive pg_stop_backup() and wait for all WAL segments to archive",
                backupData->version >= PG_VERSION_96 ? "non-" : "");

            DbBackupStopResult dbBackupStopResult = dbBackupStop(backupData->dbPrimary);

            backupFilePut(backupData, manifest, STRDEF(PG_FILE_BACKUPLABEL), dbBackupStopResult.backupLabel);
            backupFilePut(backupData, manifest, STRDEF(PG_FILE_TABLESPACEMAP), dbBackupStopResult.tablespaceMap);

            memContextSwitch(MEM_CONTEXT_OLD());
            result.lsn = strDup(dbBackupStopResult.lsn);
            result.walSegmentName = strDup(dbBackupStopResult.walSegmentName);
            memContextSwitch(MEM_CONTEXT_TEMP());

            LOG_INFO_FMT("backup stop archive = %s, lsn = %s", strPtr(result.walSegmentName), strPtr(result.lsn));
        }
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(BACKUP_STOP_RESULT, result);
}

/***********************************************************************************************************************************
Log the results of a job and throw errors
***********************************************************************************************************************************/
static uint64_t
backupJobResult(
    Manifest *manifest, const String *host, const String *const fileName, ProtocolParallelJob *const job, const uint64_t sizeTotal,
    uint64_t sizeCopied, unsigned int pageSize)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
        FUNCTION_LOG_PARAM(STRING, host);
        FUNCTION_LOG_PARAM(STRING, fileName);
        FUNCTION_LOG_PARAM(PROTOCOL_PARALLEL_JOB, job);
        FUNCTION_LOG_PARAM(UINT64, sizeTotal);
        FUNCTION_LOG_PARAM(UINT64, sizeCopied);
        FUNCTION_LOG_PARAM(UINT, pageSize);
    FUNCTION_LOG_END();

    ASSERT(manifest != NULL);
    ASSERT(fileName != NULL);
    ASSERT(job != NULL);

    // The job was successful
    if (protocolParallelJobErrorCode(job) == 0)
    {
        MEM_CONTEXT_TEMP_BEGIN()
        {
            const ManifestFile *const file = manifestFileFind(manifest, varStr(protocolParallelJobKey(job)));
            const unsigned int processId = protocolParallelJobProcessId(job);

            const VariantList *const jobResult = varVarLst(protocolParallelJobResult(job));
            const BackupCopyResult copyResult = (BackupCopyResult)varUIntForce(varLstGet(jobResult, 0));
            const uint64_t copySize = varUInt64(varLstGet(jobResult, 1));
            const uint64_t repoSize = varUInt64(varLstGet(jobResult, 2));
            const String *const copyChecksum = varStr(varLstGet(jobResult, 3));
            const KeyValue *const checksumPageResult = varKv(varLstGet(jobResult, 4));

            // Increment backup copy progress
            sizeCopied += copySize;

            // Create log file name
            const String *fileLog = host == NULL ? fileName : strNewFmt("%s:%s", strPtr(host), strPtr(fileName));

            // Format log strings
            const String *const logProgress =
                strNewFmt(
                    "%s, %" PRIu64 "%%", strPtr(strSizeFormat(copySize)), sizeTotal == 0 ? 100 : sizeCopied * 100 / sizeTotal);
            const String *const logChecksum = copySize != 0 ? strNewFmt(" checksum %s", strPtr(copyChecksum)) : EMPTY_STR;

            // If the file is in a prior backup and nothing changed, then nothing needs to be done
            if (copyResult == backupCopyResultNoOp)
            {
                LOG_DETAIL_PID_FMT(
                    processId, "match file from prior backup %s (%s)%s", strPtr(fileLog), strPtr(logProgress), strPtr(logChecksum));
            }
            // Else if the file was removed during backup then remove from manifest
            else if (copyResult == backupCopyResultSkip)
            {
                LOG_DETAIL_FMT("skip file removed by database %s", strPtr(fileLog));
                manifestFileRemove(manifest, file->name);
            }
            // Else file was copied so update manifest
            else
            {
                // If the the repo matched the expect checksum then log
                if (copyResult == backupCopyResultChecksum)
                {
                    LOG_DETAIL_PID_FMT(
                        processId, "checksum resumed file %s (%s)%s", strPtr(fileLog), strPtr(logProgress), strPtr(logChecksum));
                }
                // Else the file was copied
                else
                {
                    // If the file had to be recopied then warn that there may be an issue with corruption in the repository
                    // ??? This should really be below the message below for more context -- can be moved after the migration
                    // ??? The name should be a pg path not manifest name -- can be fixed after the migration
                    if (copyResult == backupCopyResultReCopy)
                    {
                        LOG_WARN_FMT(
                            "resumed backup file %s does not have expected checksum %s. The file will be recopied and backup will"
                            " continue but this may be an issue unless the resumed backup path in the repository is known to be"
                            " corrupted.\n"
                            "NOTE: this does not indicate a problem with the PostgreSQL page checksums.",
                            strPtr(file->name), file->checksumSha1);
                    }

                    LOG_INFO_PID_FMT(
                        processId, "backup file %s (%s)%s", strPtr(fileLog), strPtr(logProgress), strPtr(logChecksum));

                    // If the file had page checksums calculated during the copy
                    bool checksumPageError = file->checksumPageError;
                    const VariantList *checksumPageErrorList = file->checksumPageErrorList;

                    if (checksumPageResult != NULL)
                    {
                        ASSERT(file->checksumPage);

                        if (varBool(kvGet(checksumPageResult, VARSTRDEF("valid"))))
                        {
                            checksumPageError = false;
                            checksumPageErrorList = NULL;
                        }
                        else
                        {
                            checksumPageError = true;

                            if (!varBool(kvGet(checksumPageResult, VARSTRDEF("align"))))
                            {
                                checksumPageErrorList = NULL;

                                // ??? Update formatting after migration
                                LOG_WARN_FMT(
                                    "page misalignment in file %s: file size %" PRIu64 " is not divisible by page size %u",
                                    strPtr(fileLog), copySize, pageSize);
                            }
                            else
                            {
                                // Format the page checksum errors
                                checksumPageErrorList = varVarLst(kvGet(checksumPageResult, VARSTRDEF("error")));
                                ASSERT(varLstSize(checksumPageErrorList) > 0);

                                String *error = strNew("");
                                unsigned int errorTotalMin = 0;

                                for (unsigned int errorIdx = 0; errorIdx < varLstSize(checksumPageErrorList); errorIdx++)
                                {
                                    const Variant *const errorItem = varLstGet(checksumPageErrorList, errorIdx);

                                    // Add a comma if this is not the first item
                                    if (errorIdx != 0)
                                        strCat(error, ", ");

                                    // If an error range
                                    if (varType(errorItem) == varTypeVariantList)
                                    {
                                        const VariantList *const errorItemList = varVarLst(errorItem);
                                        ASSERT(varLstSize(errorItemList) == 2);

                                        strCatFmt(
                                            error, "%" PRIu64 "-%" PRIu64, varUInt64(varLstGet(errorItemList, 0)),
                                            varUInt64(varLstGet(errorItemList, 1)));
                                        errorTotalMin += 2;
                                    }
                                    else
                                    {
                                        ASSERT(varType(errorItem) == varTypeUInt64);

                                        strCatFmt(error, "%" PRIu64, varUInt64(errorItem));
                                        errorTotalMin++;
                                    }
                                }

                                // Make message plural when appropriate
                                const String *const plural = errorTotalMin > 1 ? STRDEF("s") : EMPTY_STR;

                                // ??? Update formatting after migration
                                LOG_WARN_FMT(
                                    "invalid page checksum%s found in file %s at page%s %s", strPtr(plural), strPtr(fileLog),
                                    strPtr(plural), strPtr(error));
                            }
                        }
                    }

                    // Remove any reference to the file's existence in a prior backup
                    // !!! THIS IS SO NOT KOSHER -- MAKE THIS A PARAM OF MANIFESTFILEUPDATE()
                    ((ManifestFile *)manifestFileFind(manifest, file->name))->reference = NULL;

                    manifestFileUpdate(
                        manifest, file->name, copySize, repoSize, copySize > 0 ? strPtr(copyChecksum) : "", NULL,
                        file->checksumPage, checksumPageError, checksumPageErrorList);
                }
            }
        }
        MEM_CONTEXT_TEMP_END();

        // Free the job
        protocolParallelJobFree(job);
    }
    // Else the job errored
    else
        THROW_CODE(protocolParallelJobErrorCode(job), strPtr(protocolParallelJobErrorMessage(job)));

    FUNCTION_LOG_RETURN(UINT64, sizeCopied);
}

/***********************************************************************************************************************************
Save a copy of the backup manifest during processing
***********************************************************************************************************************************/
static void
backupManifestSaveCopy(const InfoBackup *const infoBackup, Manifest *const manifest)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(INFO_BACKUP, infoBackup);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
    FUNCTION_LOG_END();

    ASSERT(manifest != NULL);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Open file for write
        IoWrite *write = storageWriteIo(
            storageNewWriteP(
                storageRepoWrite(),
                strNewFmt(
                    STORAGE_REPO_BACKUP "/%s/" BACKUP_MANIFEST_FILE INFO_COPY_EXT, strPtr(manifestData(manifest)->backupLabel))));

        // Add encryption filter if required
        cipherBlockFilterGroupAdd(
            ioWriteFilterGroup(write), cipherType(cfgOptionStr(cfgOptRepoCipherType)), cipherModeEncrypt,
            infoPgCipherPass(infoBackupPg(infoBackup)));

        // Save file
        manifestSave(manifest, write);
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Process the backup manifest
***********************************************************************************************************************************/
// Comparator to order ManifestFile objects by size then name
static int
backupProcessQueueComparator(const void *item1, const void *item2)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM_P(VOID, item1);
        FUNCTION_TEST_PARAM_P(VOID, item2);
    FUNCTION_TEST_END();

    ASSERT(item1 != NULL);
    ASSERT(item2 != NULL);

    // If the size differs then that's enough to determine order
    if ((*(ManifestFile **)item1)->size < (*(ManifestFile **)item2)->size)
        FUNCTION_TEST_RETURN(-1);
    else if ((*(ManifestFile **)item1)->size > (*(ManifestFile **)item2)->size)
        FUNCTION_TEST_RETURN(1);

    // If size is the same then use name to generate a deterministic ordering (names must be unique)
    FUNCTION_TEST_RETURN(strCmp((*(ManifestFile **)item1)->name, (*(ManifestFile **)item2)->name));
}

// Helper to generate the backup queues
static uint64_t
backupProcessQueue(Manifest *manifest, List **queueList)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
        FUNCTION_LOG_PARAM_P(LIST, queueList);
    FUNCTION_LOG_END();

    ASSERT(manifest != NULL);

    uint64_t result = 0;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Create list of process queue
        *queueList = lstNew(sizeof(List *));

        // Generate the list of targets
        StringList *targetList = strLstNew();
        strLstAdd(targetList, STRDEF(MANIFEST_TARGET_PGDATA "/"));

        for (unsigned int targetIdx = 0; targetIdx < manifestTargetTotal(manifest); targetIdx++)
        {
            const ManifestTarget *target = manifestTarget(manifest, targetIdx);

            if (target->tablespaceId != 0)
                strLstAdd(targetList, strNewFmt("%s/", strPtr(target->name)));
        }

        // Generate the processing queues (there is always at least one)
        bool backupStandby = cfgOptionBool(cfgOptBackupStandby);
        unsigned int queueOffset = backupStandby ? 1 : 0;

        MEM_CONTEXT_BEGIN(lstMemContext(*queueList))
        {
            for (unsigned int queueIdx = 0; queueIdx < strLstSize(targetList) + queueOffset; queueIdx++)
            {
                List *queue = lstNewP(sizeof(ManifestFile *), .comparator = backupProcessQueueComparator);
                lstAdd(*queueList, &queue);
            }
        }
        MEM_CONTEXT_END();

        // Now put all files into the processing queues
        bool delta = cfgOptionBool(cfgOptDelta);
        uint64_t fileTotal = 0;

        for (unsigned int fileIdx = 0; fileIdx < manifestFileTotal(manifest); fileIdx++)
        {
            const ManifestFile *file = manifestFile(manifest, fileIdx);

            // If the file is a reference it should only be backed up if delta and not zero size
            if (file->reference != NULL && (!delta || file->size == 0))
                continue;

            // Files that must be copied from the primary are always put in queue 0 when backup from standby
            if (backupStandby && file->primary)
            {
                lstAdd(*(List **)lstGet(*queueList, 0), &file);
            }
            else
            {
                // Find the target that contains this file
                unsigned int targetIdx = 0;

                do
                {
                    // A target should always be found
                    CHECK(targetIdx < strLstSize(targetList));

                    if (strBeginsWith(file->name, strLstGet(targetList, targetIdx)))
                        break;

                    targetIdx++;
                }
                while (1);

                // Add file to queue
                lstAdd(*(List **)lstGet(*queueList, targetIdx + queueOffset), &file);
            }

            // Add size to total
            result += file->size;

            // Increment total files
            fileTotal++;
        }

        // !!! pg_control should always be in the backup (unless this is an offline backup)
        // if (!$oBackupManifest->test(MANIFEST_SECTION_TARGET_FILE, MANIFEST_FILE_PGCONTROL) && cfgOption(CFGOPT_ONLINE))
        // {
        //     confess &log(ERROR, DB_FILE_PGCONTROL . " must be present in all online backups\n" .
        //                  'HINT: is something wrong with the clock or filesystem timestamps?', ERROR_FILE_MISSING);
        // }

        // If there are no files to backup then we'll exit with an error unless.  The could happen if the database is down and
        // backup is called with --no-online twice in a row.
        if (fileTotal == 0)
            THROW(FileMissingError, "no files have changed since the last backup - this seems unlikely");

        // Sort the queues
        for (unsigned int targetIdx = 0; targetIdx < strLstSize(targetList); targetIdx++)
            lstSort(*(List **)lstGet(*queueList, targetIdx), sortOrderDesc);

        // Move process queues to calling context
        lstMove(*queueList, MEM_CONTEXT_OLD());
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(UINT64, result);
}

// Helper to caculate the next queue to scan based on the client index
static int
backupJobQueueNext(unsigned int clientIdx, int queueIdx, unsigned int queueTotal)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(UINT, clientIdx);
        FUNCTION_TEST_PARAM(INT, queueIdx);
        FUNCTION_TEST_PARAM(UINT, queueTotal);
    FUNCTION_TEST_END();

    // Move (forward or back) to the next queue
    queueIdx += clientIdx % 2 ? -1 : 1;

    // Deal with wrapping on either end
    if (queueIdx < 0)
        FUNCTION_TEST_RETURN((int)queueTotal - 1);
    else if (queueIdx == (int)queueTotal)
        FUNCTION_TEST_RETURN(0);

    FUNCTION_TEST_RETURN(queueIdx);
}

// Callback to fetch backup jobs for the parallel executor
typedef struct BackupJobData
{
    const String *const backupLabel;                                // Backup label (defines the backup path)
    const bool backupStandby;                                       // Backup from standby
    const String *const cipherSubPass;                              // Passphrase used to encrypt files in the backup
    const bool compress;                                            // Is the backup compressed?
    const unsigned int compressLevel;                               // Compress level if backup is compressed
    const bool delta;                                               // Is this a checksum delta backup?

    List *queueList;                                                // List of processing queues
} BackupJobData;

static ProtocolParallelJob *backupJobCallback(void *data, unsigned int clientIdx)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM_P(VOID, data);
        FUNCTION_TEST_PARAM(UINT, clientIdx);
    FUNCTION_TEST_END();

    ASSERT(data != NULL);

    ProtocolParallelJob *result = NULL;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Get a new job if there are any left
        BackupJobData *jobData = data;

        // Determine where to begin scanning the queue (we'll stop when we get back here)
        unsigned int queueOffset = jobData->backupStandby && clientIdx > 0 ? 1 : 0;
        int queueIdx = jobData->backupStandby && clientIdx == 0 ? 0 : (int)(clientIdx % (lstSize(jobData->queueList) - queueOffset));
        int queueEnd = queueIdx;

        do
        {
            List *queue = *(List **)lstGet(jobData->queueList, (unsigned int)queueIdx + queueOffset);

            if (lstSize(queue) > 0)
            {
                const ManifestFile *file = *(ManifestFile **)lstGet(queue, 0);

                // Create backup job
                ProtocolCommand *command = protocolCommandNew(PROTOCOL_COMMAND_BACKUP_FILE_STR);

                protocolCommandParamAdd(command, VARSTR(manifestPathPg(file->name)));

                protocolCommandParamAdd(command, VARBOOL(true)); // !!! NEED EXCEPTION FOR PG_CONTROL

                protocolCommandParamAdd(command, VARUINT64(file->size));
                protocolCommandParamAdd(command, file->checksumSha1[0] != 0 ? VARSTRZ(file->checksumSha1) : NULL);
                protocolCommandParamAdd(command, VARBOOL(file->checksumPage));
                protocolCommandParamAdd(command, VARUINT(0xFFFFFFFF)); // !!! COMBINE INTO ONE PARAM
                protocolCommandParamAdd(command, VARUINT(0xFFFFFFFF)); // !!! COMBINE INTO ONE PARAM
                protocolCommandParamAdd(command, VARSTR(file->name));
                protocolCommandParamAdd(command, VARBOOL(file->reference != NULL));
                protocolCommandParamAdd(command, VARBOOL(jobData->compress));
                protocolCommandParamAdd(command, VARBOOL(jobData->compressLevel));
                protocolCommandParamAdd(command, VARSTR(jobData->backupLabel));
                protocolCommandParamAdd(command, VARBOOL(jobData->delta));
                protocolCommandParamAdd(command, VARSTR(jobData->cipherSubPass));

                // Remove job from the queue
                lstRemoveIdx(queue, 0);

                // Assign job to result
                result = protocolParallelJobMove(protocolParallelJobNew(VARSTR(file->name), command), MEM_CONTEXT_OLD());

                // Break out of the loop early since we found a job
                break;
            }

            if (!jobData->backupStandby || clientIdx > 0)
                queueIdx = backupJobQueueNext(clientIdx, queueIdx, lstSize(jobData->queueList) - queueOffset);
        }
        while (queueIdx != queueEnd);
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_TEST_RETURN(result);
}

static void
backupProcess(BackupData *backupData, Manifest *manifest)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(BACKUP_DATA, backupData);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
    FUNCTION_LOG_END();

    ASSERT(manifest != NULL);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Get backup info
        const BackupType backupType = manifestData(manifest)->backupType;
        const String *const backupLabel = manifestData(manifest)->backupLabel;
        const String *const backupPathExp = strNewFmt(STORAGE_REPO_BACKUP "/%s", strPtr(backupLabel));
        bool hardLink = cfgOptionBool(cfgOptRepoHardlink) && storageFeature(storageRepoWrite(), storageFeatureHardLink);
        bool backupStandby = cfgOptionBool(cfgOptBackupStandby);

        // If this is a full backup or hard-linked and paths are supported then create all paths explicitly so that empty paths will
        // exist in to repo.  Also create tablspace symlinks when symlinks are available,  This makes it possible for the user to
        // make a copy of the backup path and get a valid cluster.
        if (backupType == backupTypeFull || hardLink)
        {
            // Create paths when available
            if (storageFeature(storageRepoWrite(), storageFeaturePath))
            {
                for (unsigned int pathIdx = 0; pathIdx < manifestPathTotal(manifest); pathIdx++)
                {
                    storagePathCreateP(
                        storageRepoWrite(),
                        strNewFmt("%s/%s", strPtr(backupPathExp), strPtr(manifestPath(manifest, pathIdx)->name)));
                }
            }

            // Create tablespace symlinks when available
            if (storageFeature(storageRepoWrite(), storageFeatureSymLink))
            {
                for (unsigned int targetIdx = 0; targetIdx < manifestTargetTotal(manifest); targetIdx++)
                {
                    const ManifestTarget *const target = manifestTarget(manifest, targetIdx);

                    if (target->tablespaceId != 0)
                    {
                        const String *const link = storagePathP(
                            storageRepo(),
                            strNewFmt("%s/" MANIFEST_TARGET_PGDATA "/%s", strPtr(backupPathExp), strPtr(target->name)));
                        const String *const linkDestination = strNewFmt(
                            "../../" MANIFEST_TARGET_PGTBLSPC "/%u", target->tablespaceId);

                        THROW_ON_SYS_ERROR_FMT(
                            symlink(strPtr(linkDestination), strPtr(link)) == -1, FileOpenError,
                            "unable to create symlink '%s' to '%s'", strPtr(link), strPtr(linkDestination));
                    }
                }
            }
        }

        // Generate processing queues
        BackupJobData jobData =
        {
            .backupLabel = backupLabel,
            .backupStandby = backupStandby,
            .compress = cfgOptionBool(cfgOptCompress),
            .compressLevel = cfgOptionUInt(cfgOptCompressLevel),
            .cipherSubPass = manifestCipherSubPass(manifest),
            .delta = cfgOptionBool(cfgOptDelta),
        };

        uint64_t sizeTotal = backupProcessQueue(manifest, &jobData.queueList);

        // Create the parallel executor
        ProtocolParallel *parallelExec = protocolParallelNew(
            (TimeMSec)(cfgOptionDbl(cfgOptProtocolTimeout) * MSEC_PER_SEC) / 2, backupJobCallback, &jobData);

        // First client is always on the primary
        protocolParallelClientAdd(parallelExec, protocolLocalGet(protocolStorageTypePg, backupData->pgIdPrimary, 1));

        // Create the rest of the clients on the primary or standby depending on the value of backup-standby.  Note that standby
        // backups doesn't count the primary client in process-max.
        unsigned int processMax = cfgOptionUInt(cfgOptProcessMax) + (backupStandby ? 1 : 0);
        unsigned int pgId = backupStandby ? backupData->pgIdStandby : backupData->pgIdPrimary;

        for (unsigned int processIdx = 2; processIdx <= processMax; processIdx++)
            protocolParallelClientAdd(parallelExec, protocolLocalGet(protocolStorageTypePg, pgId, processIdx));

        // Process jobs
        uint64_t sizeCopied = 0;

        MEM_CONTEXT_TEMP_RESET_BEGIN()
        {
            do
            {
                unsigned int completed = protocolParallelProcess(parallelExec);

                for (unsigned int jobIdx = 0; jobIdx < completed; jobIdx++)
                {
                    ProtocolParallelJob *job = protocolParallelResult(parallelExec);

                    sizeCopied = backupJobResult(
                        manifest,
                        backupStandby && protocolParallelJobProcessId(job) > 1 ? backupData->hostStandby : backupData->hostPrimary,
                        storagePathP(
                            protocolParallelJobProcessId(job) > 1 ? storagePgId(pgId) : backupData->storagePrimary,
                            manifestPathPg(manifestFileFind(manifest, varStr(protocolParallelJobKey(job)))->name)),
                        job, sizeTotal, sizeCopied, backupData->pageSize);
                }

                // A keep-alive is required here for the remote holding open the backup connection
                protocolKeepAlive();

                // Reset the memory context occasionally so we don't use too much memory or slow down processing
                MEM_CONTEXT_TEMP_RESET(1000);
            }
            while (!protocolParallelDone(parallelExec));
        }
        MEM_CONTEXT_TEMP_END();

        // # Determine how often the manifest will be saved
        // my $lManifestSaveCurrent = 0;
        // my $lManifestSaveSize = int($lSizeTotal / 100);
        //
        // if (cfgOptionSource(CFGOPT_MANIFEST_SAVE_THRESHOLD) ne CFGDEF_SOURCE_DEFAULT ||
        //     $lManifestSaveSize < cfgOption(CFGOPT_MANIFEST_SAVE_THRESHOLD))
        // {
        //     $lManifestSaveSize = cfgOption(CFGOPT_MANIFEST_SAVE_THRESHOLD);
        // }
        //
        //     # Determine whether to save the manifest
        //     $lManifestSaveCurrent += $lSize;
        //
        //     if ($lManifestSaveCurrent >= $lManifestSaveSize)
        //     {
        //         $oManifest->saveCopy();
        //
        //         logDebugMisc
        //         (
        //             $strOperation, 'save manifest',
        //             {name => 'lManifestSaveSize', value => $lManifestSaveSize},
        //             {name => 'lManifestSaveCurrent', value => $lManifestSaveCurrent}
        //         );
        //
        //         $lManifestSaveCurrent = 0;
        //     }

        // Output references or create hardlinks for all files
        const char *const compressExt = jobData.compress ? "." GZIP_EXT : "";

        for (unsigned int fileIdx = 0; fileIdx < manifestFileTotal(manifest); fileIdx++)
        {
            const ManifestFile *const file = manifestFile(manifest, fileIdx);

            // If the file has a reference, then it was not copied since it can be retrieved from the referenced backup. However,
            // if hardlinking is enabled the link will need to be created.
            if (file->reference != NULL)
            {
                // If hardlinking is enabled then create a hardlink for files that have not changed since the last backup
                if (hardLink)
                {
                    LOG_DETAIL_FMT("hardlink %s to %s",  strPtr(file->name), strPtr(file->reference));

                    const String *const linkName = storagePathP(
                        storageRepo(), strNewFmt("%s/%s%s", strPtr(backupPathExp), strPtr(file->name), compressExt));
                    const String *const linkDestination =  storagePathP(
                        storageRepo(),
                        strNewFmt(STORAGE_REPO_BACKUP "/%s/%s%s", strPtr(file->reference), strPtr(file->name), compressExt));

                    THROW_ON_SYS_ERROR_FMT(
                        link(strPtr(linkDestination), strPtr(linkName)) == -1, FileOpenError,
                        "unable to create hardlink '%s' to '%s'", strPtr(linkName), strPtr(linkDestination));
                }
                // Else log the reference. With delta, it is possible that references may have been removed if a file needed to be
                // recopied.
                else
                    LOG_DETAIL_FMT("reference %s to %s", strPtr(file->name), strPtr(file->reference));
            }
        }

        // Sync backup paths if required
        if (storageFeature(storageRepoWrite(), storageFeaturePathSync))
        {
            for (unsigned int pathIdx = 0; pathIdx < manifestPathTotal(manifest); pathIdx++)
            {
                const String *const path = strNewFmt("%s/%s", strPtr(backupPathExp), strPtr(manifestPath(manifest, pathIdx)->name));

                if (backupType == backupTypeFull || hardLink || storagePathExistsP(storageRepo(), path))
                    storagePathSyncP(storageRepoWrite(), path);
            }
        }

        LOG_INFO_FMT("%s backup size = %s", strPtr(backupTypeStr(backupType)), strPtr(strSizeFormat(sizeTotal)));
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Check and copy WAL segments required to make the backup consistent
***********************************************************************************************************************************/
static void
backupArchiveCheckCopy(Manifest *manifest)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
    FUNCTION_LOG_END();

    ASSERT(manifest != NULL);

    // !!! If archive logs are required to complete the backup, then check them.  This is the default, but can be overridden if the
    // archive logs are going to a different server.  Be careful of this option because there is no way to verify that the backup
    // will be consistent - at least not here.
    // if (cfgOption(CFGOPT_ONLINE) && cfgOption(CFGOPT_ARCHIVE_CHECK))
    // {
    //     # Save the backup manifest before getting archive logs in case of failure
    //     $oBackupManifest->saveCopy();
    //
    //     # Create the modification time for the archive logs
    //     my $lModificationTime = time();
    //
    //     # After the backup has been stopped, need to make a copy of the archive logs to make the db consistent
    //     logDebugMisc($strOperation, "retrieve archive logs !!!START!!!:!!!STOP!!!");
    //
    //     my $oArchiveInfo = new pgBackRest::Archive::Info(storageRepo()->pathGet(STORAGE_REPO_ARCHIVE), true);
    //     my $strArchiveId = $oArchiveInfo->archiveId();
    //     my @stryArchive = lsnFileRange('START', $strLsnStop, $rhParam->{pgVersion}, 16);
    //
    //     foreach my $strArchive (@stryArchive)
    //     {
    //         my $strArchiveFile = walSegmentFind(
    //             storageRepo(), $strArchiveId, substr($strArchiveStop, 0, 8) . $strArchive, cfgOption(CFGOPT_ARCHIVE_TIMEOUT));
    //
    //         $strArchive = substr($strArchiveFile, 0, 24);
    //
    //         if (cfgOption(CFGOPT_ARCHIVE_COPY))
    //         {
    //             logDebugMisc($strOperation, "archive: ${strArchive} (${strArchiveFile})");
    //
    //             # Copy the log file from the archive repo to the backup
    //             my $bArchiveCompressed = $strArchiveFile =~ ('^.*\.' . COMPRESS_EXT . '\$');
    //
    //             storageRepo()->copy(
    //                 storageRepo()->openRead(STORAGE_REPO_ARCHIVE . "/${strArchiveId}/${strArchiveFile}",
    //                     {strCipherPass => $oArchiveInfo->cipherPassSub()}),
    //                 storageRepo()->openWrite(STORAGE_REPO_BACKUP . "/$rhParam->{backupLabel}/" . MANIFEST_TARGET_PGDATA . qw{/} .
    //                     $oBackupManifest->walPath() . "/${strArchive}" . (cfgOption(CFGOPT_COMPRESS) ? qw{.} . COMPRESS_EXT : ''),
    //                     {bPathCreate => true, strCipherPass => $strCipherPassBackupSet})
    //                 );
    //
    //             # Add the archive file to the manifest so it can be part of the restore and checked in validation
    //             my $strPathLog = MANIFEST_TARGET_PGDATA . qw{/} . $oBackupManifest->walPath();
    //             my $strFileLog = "${strPathLog}/${strArchive}";
    //
    //             # Add file to manifest
    //             $oBackupManifest->fileAdd(
    //                 $strFileLog, $lModificationTime, PG_WAL_SEGMENT_SIZE, substr($strArchiveFile, 25, 40), true);
    //         }
    //     }
    // }

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Save and update all files required to complete the backup
***********************************************************************************************************************************/
static void
backupComplete(InfoBackup *const infoBackup, Manifest *const manifest)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(INFO_BACKUP, infoBackup);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
    FUNCTION_LOG_END();

    ASSERT(manifest != NULL);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        const String *const backupLabel = manifestData(manifest)->backupLabel;

        // Final save of the backup manifest
        // -------------------------------------------------------------------------------------------------------------------------
        backupManifestSaveCopy(infoBackup, manifest);

        storageCopy(
            storageNewReadP(
                storageRepo(), strNewFmt(STORAGE_REPO_BACKUP "/%s/" BACKUP_MANIFEST_FILE INFO_COPY_EXT, strPtr(backupLabel))),
            storageNewWriteP(
                storageRepoWrite(), strNewFmt(STORAGE_REPO_BACKUP "/%s/" BACKUP_MANIFEST_FILE, strPtr(backupLabel))));

        // Copy a compressed version of the manifest to history. If the repo is encrypted then the passphrase to open the manifest
        // is required.  We can't just do a straight copy since the destination needs to be compressed and that must happen before
        // encryption in order to be efficient.
        // -------------------------------------------------------------------------------------------------------------------------
        StorageRead *manifestRead = storageNewReadP(
                storageRepo(), strNewFmt(STORAGE_REPO_BACKUP "/%s/" BACKUP_MANIFEST_FILE, strPtr(backupLabel)));

        cipherBlockFilterGroupAdd(
            ioReadFilterGroup(storageReadIo(manifestRead)), cipherType(cfgOptionStr(cfgOptRepoCipherType)), cipherModeDecrypt,
            infoPgCipherPass(infoBackupPg(infoBackup)));

        StorageWrite *manifestWrite = storageNewWriteP(
                storageRepoWrite(),
                strNewFmt(
                    STORAGE_REPO_BACKUP "/" BACKUP_PATH_HISTORY "/%s/%s.manifest." GZIP_EXT, strPtr(strSubN(backupLabel, 0, 4)),
                    strPtr(backupLabel)));

        ioFilterGroupAdd(ioWriteFilterGroup(storageWriteIo(manifestWrite)), gzipCompressNew(9, false));

        cipherBlockFilterGroupAdd(
            ioWriteFilterGroup(storageWriteIo(manifestWrite)), cipherType(cfgOptionStr(cfgOptRepoCipherType)), cipherModeEncrypt,
            infoPgCipherPass(infoBackupPg(infoBackup)));

        storageCopyP(manifestRead, manifestWrite);

        // Sync history path if required
        if (storageFeature(storageRepoWrite(), storageFeaturePathSync))
            storagePathSyncP(storageRepoWrite(), STRDEF(STORAGE_REPO_BACKUP "/" BACKUP_PATH_HISTORY));

        // Create a symlink to the most recent backup if supported.  This link is purely informational for the user and is never
        // used by us since symlinks are not supported on all storage types.
        // -------------------------------------------------------------------------------------------------------------------------
        const String *const latestLink = storagePathP(storageRepo(), STRDEF(STORAGE_REPO_BACKUP "/" BACKUP_LINK_LATEST));

        // Remove an existing latest link/file in case symlink capabilities have changed
        storageRemoveP(storageRepoWrite(), latestLink);

        if (storageFeature(storageRepoWrite(), storageFeatureSymLink))
        {
            THROW_ON_SYS_ERROR_FMT(
                symlink(strPtr(backupLabel), strPtr(latestLink)) == -1, FileOpenError,
                "unable to create symlink '%s' to '%s'", strPtr(latestLink), strPtr(backupLabel));
        }

        // Sync backup path if required
        if (storageFeature(storageRepoWrite(), storageFeaturePathSync))
            storagePathSyncP(storageRepoWrite(), STORAGE_REPO_BACKUP_STR);

        // Add manifest and save backup.info (infoBackupSaveFile() is responsible for proper syncing)
        // -------------------------------------------------------------------------------------------------------------------------
        infoBackupDataAdd(infoBackup, manifest);

        infoBackupSaveFile(
            infoBackup, storageRepoWrite(), INFO_BACKUP_PATH_FILE_STR, cipherType(cfgOptionStr(cfgOptRepoCipherType)),
            cfgOptionStr(cfgOptRepoCipherPass));
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Make a backup
***********************************************************************************************************************************/
void
cmdBackup(void)
{
    FUNCTION_LOG_VOID(logLevelDebug);

    // Verify the repo is local
    repoIsLocalVerify();

    // Test for stop file
    lockStopTest();

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Load backup.info
        InfoBackup *infoBackup = infoBackupLoadFileReconstruct(
            storageRepo(), INFO_BACKUP_PATH_FILE_STR, cipherType(cfgOptionStr(cfgOptRepoCipherType)),
            cfgOptionStr(cfgOptRepoCipherPass));
        InfoPgData infoPg = infoPgDataCurrent(infoBackupPg(infoBackup));

        // Get pg storage and database objects
        BackupData *backupData = backupInit(infoBackup);

        // Get the start timestamp which will later be written into the manifest to track total backup time
        time_t timestampStart = backupTime(backupData, false);

        // Check if there is a prior manifest if diff/incr
        Manifest *manifestPrior = backupBuildIncrPrior(infoBackup);

        // Start the backup
        BackupStartResult backupStartResult = backupStart(backupData);

        // Build the manifest
        Manifest *manifest = manifestNewBuild(
            backupData->storagePrimary, infoPg.version, cfgOptionBool(cfgOptOnline), cfgOptionBool(cfgOptChecksumPage),
            strLstNewVarLst(cfgOptionLst(cfgOptExclude)), backupStartResult.tablespaceList);

        // Validate the manifest using the copy start time
        manifestBuildValidate(manifest, cfgOptionBool(cfgOptDelta), backupTime(backupData, true), cfgOptionBool(cfgOptCompress));

        // Build an incremental backup if type is not full (manifestPrior will be freed in this call)
        if (!backupBuildIncr(infoBackup, manifest, manifestPrior, backupStartResult.walSegmentName))
            manifestCipherSubPassSet(manifest, cipherPassGen(cipherType(cfgOptionStr(cfgOptRepoCipherType))));

        // Set delta if it is not already set and the manifest requires it
        if (!cfgOptionBool(cfgOptDelta) && varBool(manifestData(manifest)->backupOptionDelta))
            cfgOptionSet(cfgOptDelta, cfgSourceParam, BOOL_TRUE_VAR);

        // Resume a backup when possible
        if (!backupResume(manifest, infoPgCipherPass(infoBackupPg(infoBackup))))
        {
            manifestBackupLabelSet(
                manifest,
                backupLabelCreate(backupType(cfgOptionStr(cfgOptType)), manifestData(manifest)->backupLabelPrior, timestampStart));
        }

        // Save the manifest before processing starts
        backupManifestSaveCopy(infoBackup, manifest);

        // Process the backup manifest
        backupProcess(backupData, manifest);

        // Stop the backup
        BackupStopResult backupStopResult = backupStop(backupData, manifest);

        // Complete manifest
        manifestBuildComplete(
            manifest, timestampStart, backupStartResult.lsn, backupStartResult.walSegmentName, backupTime(backupData, false),
            backupStopResult.lsn, backupStopResult.walSegmentName, infoPg.id, infoPg.systemId, backupStartResult.dbList,
            cfgOptionBool(cfgOptOnline) && cfgOptionBool(cfgOptArchiveCheck),
            !cfgOptionBool(cfgOptOnline) || (cfgOptionBool(cfgOptArchiveCheck) && cfgOptionBool(cfgOptArchiveCopy)),
            cfgOptionUInt(cfgOptBufferSize), cfgOptionUInt(cfgOptCompressLevel), cfgOptionUInt(cfgOptCompressLevelNetwork),
            cfgOptionBool(cfgOptRepoHardlink), cfgOptionBool(cfgOptOnline), cfgOptionUInt(cfgOptProcessMax),
            cfgOptionBool(cfgOptBackupStandby));

        // The primary db object won't be used anymore so free it
        dbFree(backupData->dbPrimary);

        // The primary protocol connection won't be used anymore so free it.  Any further access to the primary storage object may
        // result in an error (likely eof).
        protocolRemoteFree(backupData->pgIdPrimary);

        // Check and copy WAL segments required to make the backup consistent
        backupArchiveCheckCopy(manifest);

        // Complete the backup
        LOG_INFO_FMT("new backup label = %s", strPtr(manifestData(manifest)->backupLabel));
        backupComplete(infoBackup, manifest);
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}
