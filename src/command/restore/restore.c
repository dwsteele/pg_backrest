/***********************************************************************************************************************************
Restore Command
***********************************************************************************************************************************/
#include "build.auto.h"

#include "command/restore/restore.h"
#include "common/crypto/cipherBlock.h"
#include "common/debug.h"
#include "common/io/bufferWrite.h" // !!! REMOVE WITH MANIFEST TEST CODE
#include "common/log.h"
#include "config/config.h"
#include "info/infoBackup.h"
#include "info/manifest.h"
#include "postgres/interface.h"
#include "postgres/version.h"
#include "protocol/helper.h"
#include "storage/helper.h"

/***********************************************************************************************************************************
Remap the manifest based on mappings provided by the user
***********************************************************************************************************************************/
static void
restoreManifestRemap(Manifest *manifest)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
    FUNCTION_LOG_END();

    ASSERT(manifest != NULL);

    // Reassign the base path if specified
    const String *pgPath = cfgOptionStr(cfgOptPgPath);
    const ManifestTarget *targetBase = manifestTargetFind(manifest, MANIFEST_TARGET_PGDATA_STR);

    if (!strEq(targetBase->path, pgPath))
    {
        LOG_INFO("remap data directory to '%s'", strPtr(pgPath));
        manifestTargetUpdate(manifest, targetBase->name, pgPath);
    }

    // Remap tablespaces
    // KeyValue *tablespaceMap = varKv(cfgOption(cfgOptTablespaceMap));
    // const String *tablespaceMapAllPath = cfgOptionStr(cfgOptTablespaceMapAll);
    //
    // if (tablespaceMap != NULL || tablespaceMapAllPath != NULL)
    // {
    //     for (unsigned int targetIdx = 0; targetIdx < manifestTargetTotal(manifest); targetIdx++)
    //     {
    //         const ManifestTarget *target = manifestTarget(manifest, targetIdx);
    //
    //         // Is this a tablespace?
    //         if (target->tablespaceId != 0)
    //         {
    //             // Check for an individual mapping for this tablespace
    //             const String *tablespacePath = varStr(kvGet(tablespaceMap, VARSTR(target->name)));
    //
    //             if (tablespacePath == NULL && tablespaceMapAllPath != NULL)
    //                 tablespacePath = strNewFmt("%s/%s", strPtr(tablespaceMapAllPath), strPtr(target->tablespaceName));
    //
    //             // Get tablespace target
    //             // const ManifestTarget *target = manifestTargetFindDefault(
    //             //     manifest, varStr(varLstGet(tablespaceList, tablespaceIdx)), NULL);
    //
    //             // if (target == NULL || target->tablespaceId == 0)
    //             //     THROW_FMT(ErrorTablespaceMap, "unable to remap invalid tablespace '%s'", strPtr(target->name));
    //
    //             // Error if this tablespace has already been remapped
    //             // if (strListExists(tablespaceRemapped))
    //             //     THROW_FMT(ErrorTablespaceMap, "tablespace '%s' has already been remapped", strPtr(target->name));
    //
    //             // strLstAdd(tablespaceRemapped, target->name);
    //
    //             // Remap tablespace if a mapping was found
    //             if (tablespacePath != NULL)
    //             {
    //                 manifestTargetUpdate(manifest, target->name, tablespacePath);
    //
    //                 // !!! And do the same thing for the link
    //                 // manifestLinkUpdate(manifest, strNewFmt(MANIFEST_TARGET_PGDATA "/%s", strPtr(target->name), );
    //             }
    //         }
    //     }
    //
    //     // !!! CHECK FOR INVALID TABLESPACES
    // }

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Restore a backup
***********************************************************************************************************************************/
void
cmdRestore(void)
{
    FUNCTION_LOG_VOID(logLevelDebug);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // PostgreSQL must be local
        if (!pgIsLocal(1))
            THROW(HostInvalidError, CFGCMD_RESTORE " command must be run on the " PG_NAME " host");

        // The PGDATA directory must exist
        // ??? We should also do this for the rest of the paths that backrest will not create (but later after manifest load)
        if (!storagePathExistsNP(storagePg(), NULL))
            THROW_FMT(PathMissingError, "$PGDATA directory '%s' does not exist", strPtr(cfgOptionStr(cfgOptPgPath)));

        // PostgreSQL must not be running
        if (storageExistsNP(storagePg(), STRDEF(PG_FILE_POSTMASTERPID)))
        {
            THROW_FMT(
                PostmasterRunningError,
                "unable to restore while PostgreSQL is running\n"
                    "HINT: presence of '" PG_FILE_POSTMASTERPID "' in '%s' indicates PostgreSQL is running.\n"
                    "HINT: remove '" PG_FILE_POSTMASTERPID "' only if PostgreSQL is not running.",
                strPtr(cfgOptionStr(cfgOptPgPath)));
        }

        // If the restore will be destructive attempt to verify that PGDATA looks like a valid PostgreSQL directory
        if ((cfgOptionBool(cfgOptDelta) || cfgOptionBool(cfgOptForce)) &&
            !storageExistsNP(storagePg(), STRDEF(PG_FILE_PGVERSION)) && !storageExistsNP(storagePg(), STRDEF(MANIFEST_FILE)))
        {
            LOG_WARN(
                "--delta or --force specified but unable to find '" PG_FILE_PGVERSION "' or '" MANIFEST_FILE "' in '%s' to"
                    " confirm that this is a valid $PGDATA directory.  --delta and --force have been disabled and if any files"
                    " exist in the destination directories the restore will be aborted.",
               strPtr(cfgOptionStr(cfgOptPgPath)));

            cfgOptionSet(cfgOptDelta, cfgSourceDefault, VARBOOL(false));
            cfgOptionSet(cfgOptForce, cfgSourceDefault, VARBOOL(false));
        }

        // Get the repo storage in case it is remote and encryption settings need to be pulled down
        storageRepo();

        // Load backup.info
        InfoBackup *infoBackup = infoBackupLoadFile(
            storageRepo(), INFO_BACKUP_PATH_FILE_STR, cipherType(cfgOptionStr(cfgOptRepoCipherType)),
            cfgOptionStr(cfgOptRepoCipherPass));

        // If backup set to restore is default (i.e. latest) then get the actual set
        const String *backupSet = NULL;

        if (cfgOptionSource(cfgOptSet) == cfgSourceDefault)
        {
            if (infoBackupDataTotal(infoBackup) == 0)
                THROW(BackupSetInvalidError, "no backup sets to restore");

            backupSet = strDup(infoBackupData(infoBackup, infoBackupDataTotal(infoBackup) - 1).backupLabel);
        }
        // Otherwise check to make sure the specified backup set is valid
        else
        {
            backupSet = strDup(cfgOptionStr(cfgOptSet));

            bool found = false;

            for (unsigned int backupIdx = 0; backupIdx < infoBackupDataTotal(infoBackup); backupIdx++)
            {
                if (strEq(infoBackupData(infoBackup, backupIdx).backupLabel, backupSet))
                {
                    found = true;
                    break;
                }
            }

            if (!found)
                THROW_FMT(BackupSetInvalidError, "backup set %s is not valid", strPtr(backupSet));
        }

        // Load manifest
        Manifest *manifest = manifestLoadFile(
            storageRepo(), strNewFmt(STORAGE_REPO_BACKUP "/%s/" MANIFEST_FILE, strPtr(backupSet)),
            cipherType(cfgOptionStr(cfgOptRepoCipherType)), infoPgCipherPass(infoBackupPg(infoBackup)));

        // !!! THIS IS TEMPORARY TO DOUBLE-CHECK THE C MANIFEST CODE.  LOAD THE ORIGINAL MANIFEST AND COMPARE IT TO WHAT WE WOULD
        // SAVE TO DISK IF WE SAVED NOW.  THE LATER SAVE MAY HAVE MADE MODIFICATIONS BASED ON USER INPUT SO WE CAN'T USE THAT.
        // -------------------------------------------------------------------------------------------------------------------------
        if (cipherType(cfgOptionStr(cfgOptRepoCipherType)) == cipherTypeNone)                       // {uncovered_branch - !!! TEST}
        {
            Buffer *manifestTestPerlBuffer = storageGetNP(
                storageNewReadNP(
                    storageRepo(), strNewFmt(STORAGE_REPO_BACKUP "/%s/" MANIFEST_FILE, strPtr(backupSet))));

            Buffer *manifestTestCBuffer = bufNew(0);
            manifestSave(manifest, ioBufferWriteNew(manifestTestCBuffer));

            if (!bufEq(manifestTestPerlBuffer, manifestTestCBuffer))                                // {uncovered_branch - !!! TEST}
            {
                // Dump manifests to disk so we can check them with diff
                storagePutNP(                                                                       // {uncovered - !!! TEST}
                    storageNewWriteNP(storagePgWrite(), STRDEF(MANIFEST_FILE ".expected")), manifestTestPerlBuffer);
                storagePutNP(                                                                       // {uncovered - !!! TEST}
                    storageNewWriteNP(storagePgWrite(), STRDEF(MANIFEST_FILE ".actual")), manifestTestCBuffer);

                THROW_FMT(                                                                          // {uncovered - !!! TEST}
                    AssertError, "C and Perl manifests are not equal, files saved to '%s'",
                    strPtr(storagePathNP(storagePgWrite(), NULL)));
            }
        }

        // Sanity check to ensure the manifest has not been moved to a new directory
        const ManifestData *data = manifestData(manifest);

        if (!strEq(data->backupLabel, backupSet))
        {
            THROW_FMT(
                FormatError,
                "requested backup '%s' and manifest label '%s' do not match\n"
                "HINT: this indicates some sort of corruption (at the very least paths have been renamed).",
                strPtr(backupSet), strPtr(data->backupLabel));
        }

        // Log the backup set to restore
        LOG_INFO("restore backup set %s", strPtr(backupSet));

        // Remap manifest
        restoreManifestRemap(manifest);

        // Save manifest before any modifications are made to PGDATA
        manifestSave(manifest, storageWriteIo(storageNewWriteNP(storagePgWrite(), MANIFEST_FILE_STR)));
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}
