/***********************************************************************************************************************************
Restore Command
***********************************************************************************************************************************/
#include "build.auto.h"

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "command/restore/protocol.h"
#include "command/restore/restore.h"
#include "common/crypto/cipherBlock.h"
#include "common/debug.h"
#include "common/log.h"
#include "common/regExp.h"
#include "common/user.h"
#include "config/config.h"
#include "config/exec.h"
#include "info/infoBackup.h"
#include "info/manifest.h"
#include "postgres/interface.h"
#include "postgres/version.h"
#include "protocol/helper.h"
#include "protocol/parallel.h"
#include "storage/helper.h"
#include "storage/write.intern.h"

/***********************************************************************************************************************************
Recovery constants
***********************************************************************************************************************************/
#define RESTORE_COMMAND                                             "restore_command"
    STRING_STATIC(RESTORE_COMMAND_STR,                              RESTORE_COMMAND);

#define RECOVERY_TARGET                                             "recovery_target"

#define RECOVERY_TARGET_ACTION                                      "recovery_target_action"
#define RECOVERY_TARGET_ACTION_SHUTDOWN                             "shutdown"
    STRING_STATIC(RECOVERY_TARGET_ACTION_SHUTDOWN_STR,              RECOVERY_TARGET_ACTION_SHUTDOWN);

#define RECOVERY_TARGET_INCLUSIVE                                   "recovery_target_inclusive"
#define RECOVERY_TARGET_TIMELINE                                    "recovery_target_timeline"
#define PAUSE_AT_RECOVERY_TARGET                                    "pause_at_recovery_target"
#define STANDBY_MODE                                                "standby_mode"

#define RECOVERY_TYPE_DEFAULT                                       "default"
    STRING_STATIC(RECOVERY_TYPE_DEFAULT_STR,                        RECOVERY_TYPE_DEFAULT);
#define RECOVERY_TYPE_IMMEDIATE                                     "immediate"
    STRING_STATIC(RECOVERY_TYPE_IMMEDIATE_STR,                      RECOVERY_TYPE_IMMEDIATE);
#define RECOVERY_TYPE_NONE                                          "none"
    STRING_STATIC(RECOVERY_TYPE_NONE_STR,                           RECOVERY_TYPE_NONE);
#define RECOVERY_TYPE_PRESERVE                                      "preserve"
    STRING_STATIC(RECOVERY_TYPE_PRESERVE_STR,                       RECOVERY_TYPE_PRESERVE);
#define RECOVERY_TYPE_STANDBY                                       "standby"
    STRING_STATIC(RECOVERY_TYPE_STANDBY_STR,                        RECOVERY_TYPE_STANDBY);

/***********************************************************************************************************************************
Validate restore path
***********************************************************************************************************************************/
static void
restorePathValidate(void)
{
    FUNCTION_LOG_VOID(logLevelDebug);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // The PGDATA directory must exist
        // ??? We should remove this requirement in a separate commit.  What's the harm in creating the dir assuming we have perms?
        if (!storagePathExistsNP(storagePg(), NULL))
            THROW_FMT(PathMissingError, "$PGDATA directory '%s' does not exist", strPtr(cfgOptionStr(cfgOptPgPath)));

        // PostgreSQL must not be running
        if (storageExistsNP(storagePg(), PG_FILE_POSTMASTERPID_STR))
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
            !storageExistsNP(storagePg(), PG_FILE_PGVERSION_STR) && !storageExistsNP(storagePg(), BACKUP_MANIFEST_FILE_STR))
        {
            LOG_WARN(
                "--delta or --force specified but unable to find '" PG_FILE_PGVERSION "' or '" BACKUP_MANIFEST_FILE "' in '%s' to"
                    " confirm that this is a valid $PGDATA directory.  --delta and --force have been disabled and if any files"
                    " exist in the destination directories the restore will be aborted.",
               strPtr(cfgOptionStr(cfgOptPgPath)));

            // Disable delta and force so restore will fail if the directories are not empty
            cfgOptionSet(cfgOptDelta, cfgSourceDefault, VARBOOL(false));
            cfgOptionSet(cfgOptForce, cfgSourceDefault, VARBOOL(false));
        }
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Get the backup set to restore
***********************************************************************************************************************************/
static String *
restoreBackupSet(InfoBackup *infoBackup)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(INFO_BACKUP, infoBackup);
    FUNCTION_LOG_END();

    ASSERT(infoBackup != NULL);

    String *result = NULL;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // If backup set to restore is default (i.e. latest) then get the actual set
        const String *backupSet = NULL;

        if (cfgOptionSource(cfgOptSet) == cfgSourceDefault)
        {
            if (infoBackupDataTotal(infoBackup) == 0)
                THROW(BackupSetInvalidError, "no backup sets to restore");

            backupSet = infoBackupData(infoBackup, infoBackupDataTotal(infoBackup) - 1).backupLabel;
        }
        // Otherwise check to make sure the specified backup set is valid
        else
        {
            bool found = false;
            backupSet = cfgOptionStr(cfgOptSet);

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

        memContextSwitch(MEM_CONTEXT_OLD());
        result = strDup(backupSet);
        memContextSwitch(MEM_CONTEXT_TEMP());
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(STRING, result);
}

/***********************************************************************************************************************************
Validate the manifest
***********************************************************************************************************************************/
static void
restoreManifestValidate(Manifest *manifest, const String *backupSet)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
        FUNCTION_LOG_PARAM(STRING, backupSet);
    FUNCTION_LOG_END();

    ASSERT(manifest != NULL);
    ASSERT(backupSet != NULL);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // If there are no files in the manifest then something has gone horribly wrong
        CHECK(manifestFileTotal(manifest) > 0);

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
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Remap the manifest based on mappings provided by the user
***********************************************************************************************************************************/
static void
restoreManifestMap(Manifest *manifest)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
    FUNCTION_LOG_END();

    ASSERT(manifest != NULL);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Remap the data directory
        // -------------------------------------------------------------------------------------------------------------------------
        const String *pgPath = cfgOptionStr(cfgOptPgPath);
        const ManifestTarget *targetBase = manifestTargetBase(manifest);

        if (!strEq(targetBase->path, pgPath))
        {
            LOG_INFO("remap data directory to '%s'", strPtr(pgPath));
            manifestTargetUpdate(manifest, targetBase->name, pgPath, NULL);
        }

        // Remap tablespaces
        // -------------------------------------------------------------------------------------------------------------------------
        KeyValue *tablespaceMap = varKv(cfgOption(cfgOptTablespaceMap));
        const String *tablespaceMapAllPath = cfgOptionStr(cfgOptTablespaceMapAll);

        if (tablespaceMap != NULL || tablespaceMapAllPath != NULL)
        {
            StringList *tablespaceRemapped = strLstNew();

            for (unsigned int targetIdx = 0; targetIdx < manifestTargetTotal(manifest); targetIdx++)
            {
                const ManifestTarget *target = manifestTarget(manifest, targetIdx);

                // Is this a tablespace?
                if (target->tablespaceId != 0)
                {
                    const String *tablespacePath = NULL;

                    // Check for an individual mapping for this tablespace
                    if (tablespaceMap != NULL)
                    {
                        // Attempt to get the tablespace by name
                        const String *tablespacePathByName = varStr(kvGet(tablespaceMap, VARSTR(target->tablespaceName)));

                        if (tablespacePathByName != NULL)
                            strLstAdd(tablespaceRemapped, target->tablespaceName);

                        // Attempt to get the tablespace by id
                        const String *tablespacePathById = varStr(
                            kvGet(tablespaceMap, VARSTR(varStrForce(VARUINT(target->tablespaceId)))));

                        if (tablespacePathById != NULL)
                            strLstAdd(tablespaceRemapped, varStrForce(VARUINT(target->tablespaceId)));

                        // Error when both are set but the paths are different
                        if (tablespacePathByName != NULL && tablespacePathById != NULL && !
                            strEq(tablespacePathByName, tablespacePathById))
                        {
                            THROW_FMT(
                                TablespaceMapError, "tablespace remapped by name '%s' and id %u with different paths",
                                strPtr(target->tablespaceName), target->tablespaceId);
                        }
                        // Else set the path by name
                        else if (tablespacePathByName != NULL)
                        {
                            tablespacePath = tablespacePathByName;
                        }
                        // Else set the path by id
                        else if (tablespacePathById != NULL)
                            tablespacePath = tablespacePathById;
                    }

                    // If not individual mapping check if all tablespaces are being remapped
                    if (tablespacePath == NULL && tablespaceMapAllPath != NULL)
                        tablespacePath = strNewFmt("%s/%s", strPtr(tablespaceMapAllPath), strPtr(target->tablespaceName));

                    // Remap tablespace if a mapping was found
                    if (tablespacePath != NULL)
                    {
                        LOG_INFO("map tablespace '%s' to '%s'", strPtr(target->name), strPtr(tablespacePath));

                        manifestTargetUpdate(manifest, target->name, tablespacePath, NULL);
                        manifestLinkUpdate(manifest, strNewFmt(MANIFEST_TARGET_PGDATA "/%s", strPtr(target->name)), tablespacePath);
                    }
                }
            }

            // Error on invalid tablespaces
            if (tablespaceMap != NULL)
            {
                const VariantList *tablespaceMapList = kvKeyList(tablespaceMap);
                strLstSort(tablespaceRemapped, sortOrderAsc);

                for (unsigned int tablespaceMapIdx = 0; tablespaceMapIdx < varLstSize(tablespaceMapList); tablespaceMapIdx++)
                {
                    const String *tablespace = varStr(varLstGet(tablespaceMapList, tablespaceMapIdx));

                    if (!strLstExists(tablespaceRemapped, tablespace))
                        THROW_FMT(TablespaceMapError, "unable to remap invalid tablespace '%s'", strPtr(tablespace));
                }
            }

            // Issue a warning message when remapping tablespaces in PostgreSQL < 9.2
            if (manifestData(manifest)->pgVersion <= PG_VERSION_92)
                LOG_WARN("update pg_tablespace.spclocation with new tablespace locations for PostgreSQL <= " PG_VERSION_92_STR);
        }

        // Remap links
        // -------------------------------------------------------------------------------------------------------------------------
        KeyValue *linkMap = varKv(cfgOption(cfgOptLinkMap));
        bool linkAll = cfgOptionBool(cfgOptLinkAll);

        StringList *linkRemapped = strLstNew();

        for (unsigned int targetIdx = 0; targetIdx < manifestTargetTotal(manifest); targetIdx++)
        {
            const ManifestTarget *target = manifestTarget(manifest, targetIdx);

            // Is this a link?
            if (target->type == manifestTargetTypeLink && target->tablespaceId == 0)
            {
                const String *link = strSub(target->name, strSize(MANIFEST_TARGET_PGDATA_STR) + 1);
                const String *linkPath = linkMap == NULL ? NULL : varStr(kvGet(linkMap, VARSTR(link)));

                // Remap link if a mapping was found
                if (linkPath != NULL)
                {
                    LOG_INFO("map link '%s' to '%s'", strPtr(link), strPtr(linkPath));
                    manifestLinkUpdate(manifest, target->name, linkPath);

                    // If the link is a file separate the file name from the path to update the target
                    const String *linkFile = NULL;

                    if (target->file != NULL)
                    {
                        // The link destination must have at least one path component in addition to the file part. So '..' would
                        // not be a valid destination but '../file' or '/file' is.
                        if (strSize(strPath(linkPath)) == 0)
                        {
                            THROW_FMT(
                                LinkMapError, "'%s' is not long enough to be the destination for file link '%s'", strPtr(linkPath),
                                strPtr(link));
                        }

                        linkFile = strBase(linkPath);
                        linkPath = strPath(linkPath);
                    }

                    manifestTargetUpdate(manifest, target->name, linkPath, linkFile);

                    // Add to remapped list for later validation that all links were valid
                    strLstAdd(linkRemapped, link);
                }
                // If all links are not being restored then remove the target and link
                else if (!linkAll)
                {
                    if (target->file != NULL)
                        LOG_WARN("file link '%s' will be restored as a file at the same location", strPtr(link));
                    else
                    {
                        LOG_WARN(
                            "contents of directory link '%s' will be restored in a directory at the same location",
                            strPtr(link));
                    }

                    manifestLinkRemove(manifest, target->name);
                    manifestTargetRemove(manifest, target->name);
                    targetIdx--;
                }
            }
        }

        // Error on invalid links
        if (linkMap != NULL)
        {
            const VariantList *linkMapList = kvKeyList(linkMap);
            strLstSort(linkRemapped, sortOrderAsc);

            for (unsigned int linkMapIdx = 0; linkMapIdx < varLstSize(linkMapList); linkMapIdx++)
            {
                const String *link = varStr(varLstGet(linkMapList, linkMapIdx));

                if (!strLstExists(linkRemapped, link))
                    THROW_FMT(LinkMapError, "unable to remap invalid link '%s'", strPtr(link));
            }
        }
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Check ownership of items in the manifest
***********************************************************************************************************************************/
// Helper to get list of owners from a file/link/path list
#define RESTORE_MANIFEST_OWNER_GET(type)                                                                                           \
    for (unsigned int itemIdx = 0; itemIdx < manifest##type##Total(manifest); itemIdx++)                                           \
    {                                                                                                                              \
        Manifest##type *item = (Manifest##type *)manifest##type(manifest, itemIdx);                                                \
                                                                                                                                   \
        if (item->user == NULL)                                                                                                    \
            userNull = true;                                                                                                       \
        else                                                                                                                       \
            strLstAddIfMissing(userList, item->user);                                                                              \
                                                                                                                                   \
        if (item->group == NULL)                                                                                                   \
            groupNull = true;                                                                                                      \
        else                                                                                                                       \
            strLstAddIfMissing(groupList, item->group);                                                                            \
                                                                                                                                   \
        if (!userRoot())                                                                                                           \
        {                                                                                                                          \
            item->user = NULL;                                                                                                     \
            item->group = NULL;                                                                                                    \
        }                                                                                                                          \
    }

// Helper to update an owner in a file/link/path list
#define RESTORE_MANIFEST_OWNER_NULL_UPDATE(type, user, group)                                                                      \
    for (unsigned int itemIdx = 0; itemIdx < manifest##type##Total(manifest); itemIdx++)                                           \
    {                                                                                                                              \
        Manifest##type *item = (Manifest##type *)manifest##type(manifest, itemIdx);                                                \
                                                                                                                                   \
        if (item->user == NULL)                                                                                                    \
            item->user = user;                                                                                                     \
                                                                                                                                   \
        if (item->group == NULL)                                                                                                   \
            item->group = group;                                                                                                   \
    }

// Helper to warn when an owner is missing and must be remapped
#define RESTORE_MANIFEST_OWNER_WARN(type)                                                                                          \
    do                                                                                                                             \
    {                                                                                                                              \
        if (type##Null)                                                                                                            \
            LOG_WARN("unknown " #type " in backup manifest mapped to current " #type);                                             \
                                                                                                                                   \
        for (unsigned int ownerIdx = 0; ownerIdx < strLstSize(type##List); ownerIdx++)                                             \
        {                                                                                                                          \
            const String *owner = strLstGet(type##List, ownerIdx);                                                                 \
                                                                                                                                   \
            if (type##Name() == NULL ||  !strEq(type##Name(), owner))                                                              \
                LOG_WARN("unknown " #type " '%s' in backup manifest mapped to current " #type, strPtr(owner));                     \
        }                                                                                                                          \
    }                                                                                                                              \
    while (0)

static void
restoreManifestOwner(Manifest *manifest)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
    FUNCTION_LOG_END();

    ASSERT(manifest != NULL);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Build a list of users and groups in the manifest
        // -------------------------------------------------------------------------------------------------------------------------
        bool userNull = false;
        StringList *userList = strLstNew();
        bool groupNull = false;
        StringList *groupList = strLstNew();

        RESTORE_MANIFEST_OWNER_GET(File);
        RESTORE_MANIFEST_OWNER_GET(Link);
        RESTORE_MANIFEST_OWNER_GET(Path);

        // Update users and groups in the manifest (this can only be done as root)
        // -------------------------------------------------------------------------------------------------------------------------
        if (userRoot())
        {
            // Get user/group info from data directory to use for invalid user/groups
            StorageInfo pathInfo = storageInfoNP(storagePg(), manifestTargetBase(manifest)->path);

            // If user/group is null then set it to root
            if (pathInfo.user == NULL)
                pathInfo.user = userName();

            if (pathInfo.group == NULL)
                pathInfo.group = groupName();

            if (userNull || groupNull)
            {
                if (userNull)
                    LOG_WARN("unknown user in backup manifest mapped to '%s'", strPtr(pathInfo.user));

                if (groupNull)
                    LOG_WARN("unknown group in backup manifest mapped to '%s'", strPtr(pathInfo.group));

                memContextSwitch(MEM_CONTEXT_OLD());

                const String *user = strDup(pathInfo.user);
                const String *group = strDup(pathInfo.group);

                RESTORE_MANIFEST_OWNER_NULL_UPDATE(File, user, group)
                RESTORE_MANIFEST_OWNER_NULL_UPDATE(Link, user, group)
                RESTORE_MANIFEST_OWNER_NULL_UPDATE(Path, user, group)

                memContextSwitch(MEM_CONTEXT_TEMP());
            }
        }
        // Else set owners to NULL.  This means we won't make any attempt to update ownership and will just leave it as written by
        // the current user/group.  If there are existing files that are not owned by the current user/group then we will attempt
        // to update them, which will generally cause an error, though some systems allow updates to the group ownership.
        // -------------------------------------------------------------------------------------------------------------------------
        else
        {
            RESTORE_MANIFEST_OWNER_WARN(user);
            RESTORE_MANIFEST_OWNER_WARN(group);
        }
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Clean the data directory of any paths/files/links that are not in the manifest and create missing links/paths
***********************************************************************************************************************************/
typedef struct RestoreCleanCallbackData
{
    const Manifest *manifest;                                       // Manifest to compare against
    const ManifestTarget *target;                                   // Current target being compared
    const String *targetName;                                       // Name to use when finding files/paths/links
    const String *targetPath;                                       // Path of target currently being compared
    const String *subPath;                                          // Subpath in target currently being compared
    bool basePath;                                                  // Is this the base path?
    bool exists;                                                    // Does the target path exist?
    bool delta;                                                     // Is this a delta restore?
    StringList *fileIgnore;                                         // Files to ignore during clean
} RestoreCleanCallbackData;

// Helper to update ownership on a file/link/path
static void
restoreCleanOwnership(
    const String *pgPath, const String *manifestUserName, const String *manifestGroupName, uid_t actualUserId, gid_t actualGroupId,
    bool new)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, pgPath);
        FUNCTION_TEST_PARAM(STRING, manifestUserName);
        FUNCTION_TEST_PARAM(STRING, manifestGroupName);
        FUNCTION_TEST_PARAM(UINT, actualUserId);
        FUNCTION_TEST_PARAM(UINT, actualGroupId);
        FUNCTION_TEST_PARAM(BOOL, new);
    FUNCTION_TEST_END();

    ASSERT(pgPath != NULL);

    // Get the expected user id
    uid_t expectedUserId = userId();

    if (manifestUserName != NULL)
    {
        uid_t manifestUserId = userIdFromName(manifestUserName);

        if (manifestUserId != (uid_t)-1)
            expectedUserId = manifestUserId;
    }

    // Get the expected group id
    gid_t expectedGroupId = groupId();

    if (manifestGroupName != NULL)
    {
        uid_t manifestGroupId = groupIdFromName(manifestGroupName);

        if (manifestGroupId != (uid_t)-1)
            expectedGroupId = manifestGroupId;
    }

    // Update ownership if not as expected
    if (actualUserId != expectedUserId || actualGroupId != expectedGroupId)
    {
        // If this is a newly created file/link/path then there's no need to log updated permissions
        if (!new)
            LOG_DETAIL("update ownership for '%s'", strPtr(pgPath));

        THROW_ON_SYS_ERROR_FMT(
            lchown(strPtr(pgPath), expectedUserId, expectedGroupId) == -1, FileOwnerError, "unable to set ownership for '%s'",
            strPtr(pgPath));
    }

    FUNCTION_TEST_RETURN_VOID();
}

// Helper to update mode on a file/path
static void
restoreCleanMode(const String *pgPath, mode_t manifestMode, const StorageInfo *info)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, pgPath);
        FUNCTION_TEST_PARAM(MODE, manifestMode);
        FUNCTION_TEST_PARAM(INFO, info);
    FUNCTION_TEST_END();

    ASSERT(pgPath != NULL);
    ASSERT(info != NULL);

    // Update mode if not as expected
    if (manifestMode != info->mode)
    {
        LOG_DETAIL("update mode for '%s' to %04o", strPtr(pgPath), manifestMode);

        THROW_ON_SYS_ERROR_FMT(
            chmod(strPtr(pgPath), manifestMode) == -1, FileOwnerError, "unable to set mode for '%s'", strPtr(pgPath));
    }

    FUNCTION_TEST_RETURN_VOID();
}

// storageInfoList() callback that cleans the paths
static void
restoreCleanInfoListCallback(void *data, const StorageInfo *info)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM_P(VOID, data);
        FUNCTION_TEST_PARAM(STORAGE_INFO, info);
    FUNCTION_TEST_END();

    ASSERT(data != NULL);
    ASSERT(info != NULL);

    RestoreCleanCallbackData *cleanData = (RestoreCleanCallbackData *)data;

    // Don't include backup.manifest or recovery.conf (when preserved) in the comparison or empty directory check
    if (cleanData->basePath && info->type == storageTypeFile && strLstExists(cleanData->fileIgnore, info->name))
    {
        FUNCTION_TEST_RETURN_VOID();
        return;
    }

    // Is this the . path, i.e. the root path for this list?
    bool dotPath = info->type == storageTypePath && strEq(info->name, DOT_STR);

    // If this is not a delta then error because the directory is expected to be empty.  Ignore the . path.
    if (!cleanData->delta)
    {
        if (!dotPath)
        {
            THROW_FMT(
                PathNotEmptyError,
                "unable to restore to path '%s' because it contains files\n"
                "HINT: try using --delta if this is what you intended.",
                strPtr(cleanData->targetPath));
        }

        FUNCTION_TEST_RETURN_VOID();
        return;
    }

    // Construct the name used to find this file/link/path in the manifest
    const String *manifestName = dotPath ?
        cleanData->targetName : strNewFmt("%s/%s", strPtr(cleanData->targetName), strPtr(info->name));

    // Construct the path of this file/link/path in the PostgreSQL data directory
    const String *pgPath = dotPath ?
        cleanData->targetPath : strNewFmt("%s/%s", strPtr(cleanData->targetPath), strPtr(info->name));

    switch (info->type)
    {
        case storageTypeFile:
        {
            const ManifestFile *manifestFile = manifestFileFindDefault(cleanData->manifest, manifestName, NULL);

            if (manifestFile != NULL)
            {
                restoreCleanOwnership(pgPath, manifestFile->user, manifestFile->group, info->userId, info->groupId, false);
                restoreCleanMode(pgPath, manifestFile->mode, info);
            }
            else
            {
                LOG_DETAIL("remove invalid file '%s'", strPtr(pgPath));
                storageRemoveP(storageLocalWrite(), pgPath, .errorOnMissing = true);
            }

            break;
        }

        case storageTypeLink:
        {
            const ManifestLink *manifestLink = manifestLinkFindDefault(cleanData->manifest, manifestName, NULL);

            if (manifestLink != NULL)
            {
                if (!strEq(manifestLink->destination, info->linkDestination))
                {
                    LOG_DETAIL("remove link '%s' because destination changed", strPtr(pgPath));
                    storageRemoveP(storageLocalWrite(), pgPath, .errorOnMissing = true);
                }
                else
                    restoreCleanOwnership(pgPath, manifestLink->user, manifestLink->group, info->userId, info->groupId, false);
            }
            else
            {
                LOG_DETAIL("remove invalid link '%s'", strPtr(pgPath));
                storageRemoveP(storageLocalWrite(), pgPath, .errorOnMissing = true);
            }

            break;
        }

        case storageTypePath:
        {
            const ManifestPath *manifestPath = manifestPathFindDefault(cleanData->manifest, manifestName, NULL);

            if (manifestPath != NULL)
            {
                // Check ownership/permissions
                if (dotPath)
                {
                    restoreCleanOwnership(pgPath, manifestPath->user, manifestPath->group, info->userId, info->groupId, false);
                    restoreCleanMode(pgPath, manifestPath->mode, info);
                }
                // Recurse into the path
                else
                {
                    RestoreCleanCallbackData cleanDataSub = *cleanData;
                    cleanDataSub.targetName = strNewFmt("%s/%s", strPtr(cleanData->targetName), strPtr(info->name));
                    cleanDataSub.targetPath = strNewFmt("%s/%s", strPtr(cleanData->targetPath), strPtr(info->name));
                    cleanDataSub.basePath = false;

                    storageInfoListP(
                        storageLocalWrite(), cleanDataSub.targetPath, restoreCleanInfoListCallback, &cleanDataSub,
                        .errorOnMissing = true, .sortOrder = sortOrderAsc);
                }
            }
            else
            {
                LOG_DETAIL("remove invalid path '%s'", strPtr(pgPath));
                storagePathRemoveP(storageLocalWrite(), pgPath, .errorOnMissing = true, .recurse = true);
            }

            break;
        }

        // Special file types cannot exist in the manifest so just delete them
        case storageTypeSpecial:
        {
            LOG_DETAIL("remove special file '%s'", strPtr(pgPath));
            storageRemoveP(storageLocalWrite(), pgPath, .errorOnMissing = true);
            break;
        }
    }

    FUNCTION_TEST_RETURN_VOID();
}

static void
restoreCleanBuild(Manifest *manifest)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
    FUNCTION_LOG_END();

    ASSERT(manifest != NULL);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Is this a delta restore?
        bool delta = cfgOptionBool(cfgOptDelta) || cfgOptionBool(cfgOptForce);

        // Allocate data for each target
        RestoreCleanCallbackData *cleanDataList = memNew(sizeof(RestoreCleanCallbackData) * manifestTargetTotal(manifest));

        // Step 1: Check permissions and validity (is the directory empty without delta?) if the target directory exists
        // -------------------------------------------------------------------------------------------------------------------------
        StringList *pathChecked = strLstNew();

        for (unsigned int targetIdx = 0; targetIdx < manifestTargetTotal(manifest); targetIdx++)
        {
            RestoreCleanCallbackData *cleanData = &cleanDataList[targetIdx];

            cleanData->manifest = manifest;
            cleanData->target = manifestTarget(manifest, targetIdx);
            cleanData->targetName = cleanData->target->name;
            cleanData->targetPath = manifestTargetPath(manifest, cleanData->target);
            cleanData->basePath = strEq(cleanData->targetName, MANIFEST_TARGET_PGDATA_STR);
            cleanData->delta = delta;

            // Ignore backup.manifest while cleaning since it may exist from an prior incomplete restore
            cleanData->fileIgnore = strLstNew();
            strLstAdd(cleanData->fileIgnore, BACKUP_MANIFEST_FILE_STR);

            // Alse ignore recovery.conf when recovery type = preserve
            if (strEq(cfgOptionStr(cfgOptType), RECOVERY_TYPE_PRESERVE_STR))
                    strLstAdd(cleanData->fileIgnore, PG_FILE_RECOVERYCONF_STR);

            // If this is a tablespace append the tablespace identifier
            if (cleanData->target->type == manifestTargetTypeLink && cleanData->target->tablespaceId != 0)
            {
                const String *tablespaceId = pgTablespaceId(manifestData(manifest)->pgVersion);

                // Only PostgreSQL >= 9.0 has tablespace indentifiers
                if (tablespaceId != NULL)
                {
                    cleanData->targetName = strNewFmt("%s/%s", strPtr(cleanData->targetName), strPtr(tablespaceId));
                    cleanData->targetPath = strNewFmt("%s/%s", strPtr(cleanData->targetPath), strPtr(tablespaceId));
                }
            }

            strLstSort(cleanData->fileIgnore, sortOrderAsc);

            // Check that the path exists.  If not, there's no need to do any cleaning and we'll attempt to create it later.
            // Don't log check for the same path twice.  There can be multiple links to files in the same path, but logging it more
            // than once makes the logs noisy and looks like a bug.
            if (!strLstExists(pathChecked, cleanData->targetPath))
                LOG_DETAIL("check '%s' exists", strPtr(cleanData->targetPath));

            StorageInfo info = storageInfoP(storageLocal(), cleanData->targetPath, .ignoreMissing = true, .followLink = true);
            strLstAdd(pathChecked, cleanData->targetPath);

            if (info.exists)
            {
                // Make sure our uid will be able to write to this directory
                if (!userRoot() && userId() != info.userId)
                {
                    THROW_FMT(
                        PathOpenError, "unable to restore to path '%s' not owned by current user", strPtr(cleanData->targetPath));
                }

                if ((info.mode & 0700) != 0700)
                {
                    THROW_FMT(
                        PathOpenError, "unable to restore to path '%s' without rwx permissions", strPtr(cleanData->targetPath));
                }

                // If not a delta restore then check that the directories are empty, or if a file link, that the file doesn't exist
                if (!cleanData->delta)
                {
                    if (cleanData->target->file == NULL)
                    {
                        storageInfoListP(
                            storageLocal(), cleanData->targetPath, restoreCleanInfoListCallback, cleanData,
                            .errorOnMissing = true);
                    }
                    else
                    {
                        const String *file = strNewFmt("%s/%s", strPtr(cleanData->targetPath), strPtr(cleanData->target->file));

                        if (storageExistsNP(storageLocal(), file))
                        {
                            THROW_FMT(
                                FileExistsError,
                                "unable to restore file '%s' because it already exists\n"
                                "HINT: try using --delta if this is what you intended.",
                                strPtr(file));
                        }
                    }

                    // Now that we know there are no files in this target enable delta for processing in step 2
                    cleanData->delta = true;
                }

                // The target directory exists and is valid and will need to be cleaned
                cleanData->exists = true;
            }
        }

        // Skip the tablespace_map file when present so PostgreSQL does not rewrite links in pg_tblspc. The tablespace links will be
        // created after paths are cleaned.
        if (manifestFileFindDefault(manifest, STRDEF(MANIFEST_TARGET_PGDATA "/" PG_FILE_TABLESPACEMAP), NULL) != NULL &&
            manifestData(manifest)->pgVersion >= PG_VERSION_TABLESPACE_MAP)
        {
            LOG_DETAIL("skip '" PG_FILE_TABLESPACEMAP "' -- tablespace links will be created based on mappings");
            manifestFileRemove(manifest, STRDEF(MANIFEST_TARGET_PGDATA "/" PG_FILE_TABLESPACEMAP));
        }

        // Step 2: Clean target directories
        // -------------------------------------------------------------------------------------------------------------------------
        // Delete the pg_control file (if it exists) so the cluster cannot be started if restore does not complete.  Sync the path
        // so the file does not return, zombie-like, in the case of a host crash.
        if (storageExistsNP(storagePg(), STRDEF(PG_PATH_GLOBAL "/" PG_FILE_PGCONTROL)))
        {
            LOG_DETAIL("remove '" PG_PATH_GLOBAL "/" PG_FILE_PGCONTROL "' so cluster will not start if restore does not complete");
            storageRemoveNP(storagePgWrite(), STRDEF(PG_PATH_GLOBAL "/" PG_FILE_PGCONTROL));
            storagePathSyncNP(storagePgWrite(), PG_PATH_GLOBAL_STR);
        }

        for (unsigned int targetIdx = 0; targetIdx < manifestTargetTotal(manifest); targetIdx++)
        {
            RestoreCleanCallbackData *cleanData = &cleanDataList[targetIdx];

            // Only clean if the target exists
            if (cleanData->exists)
            {
                // Don't clean file links.  It doesn't matter whether the file exists or not since we know it is in the manifest.
                if (cleanData->target->file == NULL)
                {
                    // Only log when doing a delta restore because otherwise the targets should be empty.  We'll still run the clean
                    // to fix permissions/ownership on the target paths.
                    if (delta)
                        LOG_INFO("remove invalid files/links/paths from '%s'", strPtr(cleanData->targetPath));

                    // Clean the target
                    storageInfoListP(
                        storageLocalWrite(), cleanData->targetPath, restoreCleanInfoListCallback, cleanData, .errorOnMissing = true,
                        .sortOrder = sortOrderAsc);
                }
            }
            // If the target does not exist we'll attempt to create it
            else
            {
                const ManifestPath *path = NULL;

                // There is no path information for a file link so we'll need to use the data directory
                if (cleanData->target->file != NULL)
                {
                    path = manifestPathFind(manifest, MANIFEST_TARGET_PGDATA_STR);
                }
                // Else grab the info for the path that matches the link name
                else
                    path = manifestPathFind(manifest, cleanData->target->name);

                storagePathCreateP(storageLocalWrite(), cleanData->targetPath, .mode = path->mode);
                restoreCleanOwnership(cleanData->targetPath, path->user, path->group, userId(), groupId(), true);
            }
        }

        // Step 3: Create missing paths and path links
        // -------------------------------------------------------------------------------------------------------------------------
        for (unsigned int pathIdx = 0; pathIdx < manifestPathTotal(manifest); pathIdx++)
        {
            const ManifestPath *path = manifestPath(manifest, pathIdx);

            // Skip the pg_tblspc path because it only maps to the manifest.  We should remove this in a future release but not much
            // can be done about it for now.
            if (strEq(path->name, MANIFEST_TARGET_PGTBLSPC_STR))
                continue;

            // If this path has been mapped as a link then create a link.  The path has already been created as part of target
            // creation (or it might have already existed).
            const ManifestLink *link = manifestLinkFindDefault(
                manifest,
                strBeginsWith(path->name, MANIFEST_TARGET_PGTBLSPC_STR) ?
                    strNewFmt(MANIFEST_TARGET_PGDATA "/%s", strPtr(path->name)) : path->name,
                NULL);

            if (link != NULL)
            {
                const String *pgPath = storagePathNP(storagePg(), manifestPgPath(link->name));
                StorageInfo linkInfo = storageInfoP(storagePg(), pgPath, .ignoreMissing = true);

                // Create the link if it is missing.  If it exists it should already have the correct ownership and destination.
                if (!linkInfo.exists)
                {
                    LOG_DETAIL("create symlink '%s' to '%s'", strPtr(pgPath), strPtr(link->destination));

                    THROW_ON_SYS_ERROR_FMT(
                        symlink(strPtr(link->destination), strPtr(pgPath)) == -1, FileOpenError,
                        "unable to create symlink '%s' to '%s'", strPtr(pgPath), strPtr(link->destination));
                    restoreCleanOwnership(pgPath, link->user, link->group, userId(), groupId(), true);
                }
            }
            // Create the path normally
            else
            {
                const String *pgPath = storagePathNP(storagePg(), manifestPgPath(path->name));
                StorageInfo pathInfo = storageInfoP(storagePg(), pgPath, .ignoreMissing = true);

                // Create the path if it is missing  If it exists it should already have the correct ownership and mode.
                if (!pathInfo.exists)
                {
                    LOG_DETAIL("create path '%s'", strPtr(pgPath));

                    storagePathCreateP(storagePgWrite(), pgPath, .mode = path->mode, .noParentCreate = true, .errorOnExists = true);
                    restoreCleanOwnership(storagePathNP(storagePg(), pgPath), path->user, path->group, userId(), groupId(), true);
                }
            }
        }

        // Step 4: Create file links.  These don't get created during path creation because they do not have a matching path entry.
        // -------------------------------------------------------------------------------------------------------------------------
        for (unsigned int linkIdx = 0; linkIdx < manifestLinkTotal(manifest); linkIdx++)
        {
            const ManifestLink *link = manifestLink(manifest, linkIdx);

            const String *pgPath = storagePathNP(storagePg(), manifestPgPath(link->name));
            StorageInfo linkInfo = storageInfoP(storagePg(), pgPath, .ignoreMissing = true);

            // Create the link if it is missing.  If it exists it should already have the correct ownership and destination.
            if (!linkInfo.exists)
            {
                LOG_DETAIL("create symlink '%s' to '%s'", strPtr(pgPath), strPtr(link->destination));

                THROW_ON_SYS_ERROR_FMT(
                    symlink(strPtr(link->destination), strPtr(pgPath)) == -1, FileOpenError,
                    "unable to create symlink '%s' to '%s'", strPtr(pgPath), strPtr(link->destination));
                restoreCleanOwnership(pgPath, link->user, link->group, userId(), groupId(), true);
            }
        }
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Generate the expression to zero files that are not needed for selective restore
***********************************************************************************************************************************/
static String *
restoreSelectiveExpression(Manifest *manifest)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
    FUNCTION_LOG_END();

    ASSERT(manifest != NULL);

    String *result = NULL;

    // Continue if db-include is specified
    if (cfgOptionTest(cfgOptDbInclude))
    {
        MEM_CONTEXT_TEMP_BEGIN()
        {
            // Generate base expression
            RegExp *baseRegExp = regExpNew(STRDEF("^" MANIFEST_TARGET_PGDATA "/" PG_PATH_BASE "/[0-9]+/" PG_FILE_PGVERSION));

            // Generate tablespace expression
            RegExp *tablespaceRegExp = NULL;
            const String *tablespaceId = pgTablespaceId(manifestData(manifest)->pgVersion);

            if (tablespaceId == NULL)
                tablespaceRegExp = regExpNew(STRDEF("^" MANIFEST_TARGET_PGTBLSPC "/[0-9]+/[0-9]+/" PG_FILE_PGVERSION));
            else
            {
                tablespaceRegExp = regExpNew(
                    strNewFmt("^" MANIFEST_TARGET_PGTBLSPC "/[0-9]+/%s/[0-9]+/" PG_FILE_PGVERSION, strPtr(tablespaceId)));
            }

            // Generate a list of databases in base or in a tablespace
            StringList *dbList = strLstNew();

            for (unsigned int fileIdx = 0; fileIdx < manifestFileTotal(manifest); fileIdx++)
            {
                const ManifestFile *file = manifestFile(manifest, fileIdx);

                if (regExpMatch(baseRegExp, file->name) || regExpMatch(tablespaceRegExp, file->name))
                    strLstAddIfMissing(dbList, strBase(strPath(file->name)));
            }

            strLstSort(dbList, sortOrderAsc);

            // If no databases were found then this backup is not a valid cluster
            if (strLstSize(dbList) == 0)
                THROW(FormatError, "no databases found for selective restore\nHINT: is this a valid cluster?");

            // Log databases found
            LOG_DETAIL("databases found for selective restore (%s)", strPtr(strLstJoin(dbList, ", ")));

            // Remove included databases from the list
            const StringList *includeList = strLstNewVarLst(cfgOptionLst(cfgOptDbInclude));

            for (unsigned int includeIdx = 0; includeIdx < strLstSize(includeList); includeIdx++)
            {
                const String *includeDb = strLstGet(includeList, includeIdx);

                // If the db to include is not in the list as an id then search by name
                if (!strLstExists(dbList, includeDb))
                {
                    const ManifestDb *db = manifestDbFindDefault(manifest, includeDb, NULL);

                    if (db == NULL || !strLstExists(dbList, varStrForce(VARUINT(db->id))))
                        THROW_FMT(DbMissingError, "database to include '%s' does not exist", strPtr(includeDb));

                    // Set the include db to the id if the name mapping was successful
                    includeDb = varStrForce(VARUINT(db->id));
                }

                // Error if the db is a system db
                if (cvtZToUInt64(strPtr(includeDb)) < PG_USER_OBJECT_MIN_ID)
                    THROW(DbInvalidError, "system databases (template0, postgres, etc.) are included by default");

                // Remove from list of DBs to zero
                strLstRemove(dbList, includeDb);
            }

            // Build regular expression to identify files that will be zeroed
            strLstSort(dbList, sortOrderAsc);
            String *expression = NULL;

            for (unsigned int dbIdx = 0; dbIdx < strLstSize(dbList); dbIdx++)
            {
                const String *db = strLstGet(dbList, dbIdx);

                // Only user created databases can be zeroed, never system databases
                if (cvtZToUInt64(strPtr(db)) >= PG_USER_OBJECT_MIN_ID)
                {
                    // Create expression string or append |
                    if (expression == NULL)
                        expression = strNew("");
                    else
                        strCat(expression, "|");

                    // Filter files in base directory
                    strCatFmt(expression, "(^" MANIFEST_TARGET_PGDATA "/" PG_PATH_BASE "/%s/)", strPtr(db));

                    // Filter files in tablespace directories
                    for (unsigned int targetIdx = 0; targetIdx < manifestTargetTotal(manifest); targetIdx++)
                    {
                        const ManifestTarget *target = manifestTarget(manifest, targetIdx);

                        if (target->tablespaceId != 0)
                        {
                            if (tablespaceId == NULL)
                                strCatFmt(expression, "|(^%s/%s/)", strPtr(target->name), strPtr(db));
                            else
                                strCatFmt(expression, "|(^%s/%s/%s/)", strPtr(target->name), strPtr(tablespaceId), strPtr(db));
                        }
                    }
                }
            }

            // If all user databases have been selected then nothing to do
            if (expression == NULL)
            {
                LOG_INFO("nothing to filter - all user databases have been selected");
            }
            // Else return the expression
            else
            {
                memContextSwitch(MEM_CONTEXT_OLD());
                result = strDup(expression);
                memContextSwitch(MEM_CONTEXT_TEMP());
            }
        }
        MEM_CONTEXT_TEMP_END();
    }

    FUNCTION_LOG_RETURN(STRING, result);
}

/***********************************************************************************************************************************
Generate the recovery file
***********************************************************************************************************************************/
// Helper to generate recovery options
static KeyValue *
restoreRecoveryOption(unsigned int pgVersion)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(UINT, pgVersion);
    FUNCTION_LOG_END();

    KeyValue *result = NULL;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        result = kvNew();

        StringList *recoveryOptionKey = strLstNew();

        if (cfgOptionTest(cfgOptRecoveryOption))
        {
            const KeyValue *recoveryOption = cfgOptionKv(cfgOptRecoveryOption);
            recoveryOptionKey = strLstSort(strLstNewVarLst(kvKeyList(recoveryOption)), sortOrderAsc);

            for (unsigned int keyIdx = 0; keyIdx < strLstSize(recoveryOptionKey); keyIdx++)
            {
                // Get the key and value
                String *key = strLstGet(recoveryOptionKey, keyIdx);
                const String *value = varStr(kvGet(recoveryOption, VARSTR(key)));

                // Replace - in key with _.  Since we use - users naturally will as well.
                strReplaceChr(key, '-', '_');

                kvPut(result, VARSTR(key), VARSTR(value));
            }

            strLstSort(recoveryOptionKey, sortOrderAsc);
        }

        // Write restore_command
        if (!strLstExists(recoveryOptionKey, RESTORE_COMMAND_STR))
        {
            // Null out options that it does not make sense to pass from the restore command to archive-get.  All of these have
            // reasonable defaults so there is no danger of a error -- they just might not be optimal.  In any case, it seems
            // better than, for example, passing --process-max=32 to archive-get because it was specified for restore.
            KeyValue *optionReplace = kvNew();

            kvPut(optionReplace, VARSTR(CFGOPT_LOG_LEVEL_CONSOLE_STR), NULL);
            kvPut(optionReplace, VARSTR(CFGOPT_LOG_LEVEL_FILE_STR), NULL);
            kvPut(optionReplace, VARSTR(CFGOPT_LOG_LEVEL_STDERR_STR), NULL);
            kvPut(optionReplace, VARSTR(CFGOPT_LOG_SUBPROCESS_STR), NULL);
            kvPut(optionReplace, VARSTR(CFGOPT_LOG_TIMESTAMP_STR), NULL);
            kvPut(optionReplace, VARSTR(CFGOPT_PROCESS_MAX_STR), NULL);

            kvPut(
                result, VARSTRZ(RESTORE_COMMAND),
                VARSTR(
                    strNewFmt(
                        "%s %s %%f \"%%p\"", strPtr(cfgExe()),
                        strPtr(strLstJoin(cfgExecParam(cfgCmdArchiveGet, optionReplace, true), " ")))));
        }

        // If recovery type is immediate
        if (strEq(cfgOptionStr(cfgOptType), RECOVERY_TYPE_IMMEDIATE_STR))
        {
            kvPut(result, VARSTRZ(RECOVERY_TARGET), VARSTRZ(RECOVERY_TYPE_IMMEDIATE));
        }
        // Else recovery type is standby
        else if (strEq(cfgOptionStr(cfgOptType), RECOVERY_TYPE_STANDBY_STR))
        {
            // Write standby_mode
            kvPut(result, VARSTRZ(STANDBY_MODE), VARSTRDEF("on"));
        }
        // Else recovery type is not default so write target options
        else if (!strEq(cfgOptionStr(cfgOptType), RECOVERY_TYPE_DEFAULT_STR))
        {
            // Write the recovery target
            kvPut(
                result, VARSTR(strNewFmt(RECOVERY_TARGET "_%s", strPtr(cfgOptionStr(cfgOptType)))),
                VARSTR(cfgOptionStr(cfgOptTarget)));

            // Write recovery_target_inclusive
            if (cfgOptionTest(cfgOptTargetExclusive) && cfgOptionBool(cfgOptTargetExclusive))
                kvPut(result, VARSTRZ(RECOVERY_TARGET_INCLUSIVE), VARSTR(FALSE_STR));
        }

        // Write pause_at_recovery_target/recovery_target_action
        if (cfgOptionTest(cfgOptTargetAction))
        {
            const String *targetAction = cfgOptionStr(cfgOptTargetAction);

            if (!strEqZ(targetAction, cfgDefOptionDefault(cfgDefCmdRestore, cfgDefOptTargetAction)))
            {
                // Write recovery_target on supported PostgreSQL versions
                if (pgVersion >= PG_VERSION_RECOVERY_TARGET_ACTION)
                {
                    kvPut(result, VARSTRZ(RECOVERY_TARGET_ACTION), VARSTR(targetAction));
                }
                // Write pause_at_recovery_target on supported PostgreSQL versions
                else if (pgVersion >= PG_VERSION_RECOVERY_TARGET_PAUSE)
                {
                    // Shutdown action is not supported with pause_at_recovery_target setting
                    if (strEq(targetAction, RECOVERY_TARGET_ACTION_SHUTDOWN_STR))
                    {
                        THROW_FMT(
                            OptionInvalidError,
                            CFGOPT_TARGET_ACTION "=" RECOVERY_TARGET_ACTION_SHUTDOWN " is only available in PostgreSQL >= %s",
                            strPtr(pgVersionToStr(PG_VERSION_RECOVERY_TARGET_ACTION)));
                    }

                    kvPut(result, VARSTRZ(PAUSE_AT_RECOVERY_TARGET), VARSTR(FALSE_STR));
                }
                // Else error on unsupported version
                else
                {
                    THROW_FMT(
                        OptionInvalidError, CFGOPT_TARGET_ACTION " option is only available in PostgreSQL >= %s",
                        strPtr(pgVersionToStr(PG_VERSION_RECOVERY_TARGET_PAUSE)));
                }
            }
        }

        // Write recovery_target_timeline
        if (cfgOptionTest(cfgOptTargetTimeline))
            kvPut(result, VARSTRZ(RECOVERY_TARGET_TIMELINE), VARSTR(cfgOptionStr(cfgOptTargetTimeline)));

        // Move to calling context
        kvMove(result, MEM_CONTEXT_OLD());
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(KEY_VALUE, result);
}

// Helper to write recovery options into recovery.conf
static String *
restoreRecoveryConf(unsigned int pgVersion)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(UINT, pgVersion);
    FUNCTION_LOG_END();

    String *result = NULL;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        result = strNew("");

        // Output all recovery options
        KeyValue *optionKv = restoreRecoveryOption(pgVersion);
        const VariantList *optionKeyList = kvKeyList(optionKv);

        for (unsigned int optionKeyIdx = 0; optionKeyIdx < varLstSize(optionKeyList); optionKeyIdx++)
        {
            const Variant *optionKey = varLstGet(optionKeyList, optionKeyIdx);
            strCatFmt(result, "%s = '%s'\n", strPtr(varStr(optionKey)), strPtr(varStr(kvGet(optionKv, optionKey))));
        }

        // Move to calling context
        memContextSwitch(MEM_CONTEXT_OLD());
        result = strDup(result);
        memContextSwitch(MEM_CONTEXT_TEMP());
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(STRING, result);
}

static void
restoreRecoveryWrite(const Manifest *manifest)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_TEST_PARAM(MANIFEST, manifest);
    FUNCTION_LOG_END();

    // Get PostgreSQL version to write recovery for
    unsigned int pgVersion = manifestData(manifest)->pgVersion;

    // Determine which file recovery setttings will be written to
    const String *recoveryFile = PG_FILE_RECOVERYCONF_STR;

    // Use the data directory to set permissions and ownership for recovery file
    const ManifestPath *dataPath = manifestPathFind(manifest, MANIFEST_TARGET_PGDATA_STR);
    mode_t recoveryFileMode = dataPath->mode & (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // If recovery type is preserve then leave recovery file as it is
        if (strEq(cfgOptionStr(cfgOptType), RECOVERY_TYPE_PRESERVE_STR))
        {
            if (!storageExistsNP(storagePg(), recoveryFile))
            {
                LOG_WARN(
                    "recovery type is " RECOVERY_TYPE_PRESERVE " but recovery file does not exist at '%s'",
                    strPtr(storagePathNP(storagePg(), recoveryFile)));
            }
        }
        // Else write recovery file if requested
        else
        {
            // Only generate recovery file if recovery type is not none
            if (!strEq(cfgOptionStr(cfgOptType), RECOVERY_TYPE_NONE_STR))
            {
                LOG_INFO("write %s", strPtr(storagePathNP(storagePg(), recoveryFile)));

                storagePutNP(
                    storageNewWriteP(
                        storagePgWrite(), recoveryFile, .noCreatePath = true, .modeFile = recoveryFileMode, .noAtomic = true,
                        .noSyncPath = true, .user = dataPath->user, .group = dataPath->group),
                    BUFSTR(restoreRecoveryConf(pgVersion)));
            }
        }
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Generate a list of queues that determine the order of file processing
***********************************************************************************************************************************/
// Comparator to order ManifestFile objects by size then name
static int
restoreProcessQueueComparator(const void *item1, const void *item2)
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

static uint64_t
restoreProcessQueue(Manifest *manifest, List **queueList)
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

        // Generate the list of processing queues (there is always at least one)
        StringList *targetList = strLstNew();
        strLstAdd(targetList, STRDEF(MANIFEST_TARGET_PGDATA "/"));

        for (unsigned int targetIdx = 0; targetIdx < manifestTargetTotal(manifest); targetIdx++)
        {
            const ManifestTarget *target = manifestTarget(manifest, targetIdx);

            if (target->tablespaceId != 0)
                strLstAdd(targetList, strNewFmt("%s/", strPtr(target->name)));
        }

        // Generate the processing queues
        MEM_CONTEXT_BEGIN(lstMemContext(*queueList))
        {
            for (unsigned int targetIdx = 0; targetIdx < strLstSize(targetList); targetIdx++)
            {
                List *queue = lstNewP(sizeof(ManifestFile *), .comparator = restoreProcessQueueComparator);
                lstAdd(*queueList, &queue);
            }
        }
        MEM_CONTEXT_END();

        // Now put all files into the processing queues
        for (unsigned int fileIdx = 0; fileIdx < manifestFileTotal(manifest); fileIdx++)
        {
            const ManifestFile *file = manifestFile(manifest, fileIdx);

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
            lstAdd(*(List **)lstGet(*queueList, targetIdx), &file);

            // Add size to total
            result += file->size;
        }

        // Sort the queues
        for (unsigned int targetIdx = 0; targetIdx < strLstSize(targetList); targetIdx++)
            lstSort(*(List **)lstGet(*queueList, targetIdx), sortOrderDesc);

        // Move process queues to calling context
        lstMove(*queueList, MEM_CONTEXT_OLD());
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(UINT64, result);
}

/***********************************************************************************************************************************
Log the results of a job and throw errors
***********************************************************************************************************************************/
// Helper function to determine if a file should be zeroed
static bool
restoreFileZeroed(const String *manifestName, RegExp *zeroExp)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, manifestName);
        FUNCTION_TEST_PARAM(REGEXP, zeroExp);
    FUNCTION_TEST_END();

    ASSERT(manifestName != NULL);

    FUNCTION_TEST_RETURN(
        zeroExp == NULL ? false : regExpMatch(zeroExp, manifestName) && !strEndsWith(manifestName, STRDEF("/" PG_FILE_PGVERSION)));
}

// Helper function to construct the absolute pg path for any file
static String *
restoreFilePgPath(const Manifest *manifest, const String *manifestName)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, manifest);
        FUNCTION_TEST_PARAM(STRING, manifestName);
    FUNCTION_TEST_END();

    ASSERT(manifest != NULL);
    ASSERT(manifestName != NULL);

    String *result = strNewFmt("%s/%s", strPtr(manifestTargetBase(manifest)->path), strPtr(manifestPgPath(manifestName)));

    if (strEq(manifestName, STRDEF(MANIFEST_TARGET_PGDATA "/" PG_PATH_GLOBAL "/" PG_FILE_PGCONTROL)))
        result = strNewFmt("%s." STORAGE_FILE_TEMP_EXT, strPtr(result));

    FUNCTION_TEST_RETURN(result);
}

static uint64_t
restoreJobResult(const Manifest *manifest, ProtocolParallelJob *job, RegExp *zeroExp, uint64_t sizeTotal, uint64_t sizeRestored)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(MANIFEST, manifest);
        FUNCTION_LOG_PARAM(PROTOCOL_PARALLEL_JOB, job);
        FUNCTION_LOG_PARAM(REGEXP, zeroExp);
        FUNCTION_LOG_PARAM(UINT64, sizeTotal);
        FUNCTION_LOG_PARAM(UINT64, sizeRestored);
    FUNCTION_LOG_END();

    ASSERT(manifest != NULL);

    // The job was successful
    if (protocolParallelJobErrorCode(job) == 0)
    {
        MEM_CONTEXT_TEMP_BEGIN()
        {
            const ManifestFile *file = manifestFileFind(manifest, varStr(protocolParallelJobKey(job)));
            bool zeroed = restoreFileZeroed(file->name, zeroExp);
            bool copy = varBool(protocolParallelJobResult(job));

            String *log = strNew("restore");

            // Note if file was zeroed (i.e. selective restore)
            if (zeroed)
                strCat(log, " zeroed");

            // Add filename
            strCatFmt(log, " file %s", strPtr(restoreFilePgPath(manifest, file->name)));

            // If not copied and not zeroed add details to explain why it was not copied
            if (!copy && !zeroed)
            {
                strCat(log, " - ");

                // On force we match on size and modification time
                if (cfgOptionBool(cfgOptForce))
                {
                    strCatFmt(
                        log, "exists and matches size %" PRIu64 " and modification time %" PRIu64, file->size,
                        (uint64_t)file->timestamp);
                }
                // Else a checksum delta or file is zero-length
                else
                {
                    strCat(log, "exists and ");

                    // No need to copy zero-length files
                    if (file->size == 0)
                    {
                        strCat(log, "is zero size");
                    }
                    // The file matched the manifest checksum so did not need to be copied
                    else
                        strCat(log, "matches backup");
                }
            }

            // Add size and percent complete
            sizeRestored += file->size;
            strCatFmt(log, " (%s, %" PRIu64 "%%)", strPtr(strSizeFormat(file->size)), sizeRestored * 100 / sizeTotal);

            // If not zero-length add the checksum
            if (file->size != 0 && !zeroed)
                strCatFmt(log, " checksum %s", file->checksumSha1);

            LOG_PID(copy ? logLevelInfo : logLevelDetail, protocolParallelJobProcessId(job), 0, strPtr(log));
        }
        MEM_CONTEXT_TEMP_END();

        // Free the job
        protocolParallelJobFree(job);
    }
    // Else the job errored
    else
        THROW_CODE(protocolParallelJobErrorCode(job), strPtr(protocolParallelJobErrorMessage(job)));

    FUNCTION_LOG_RETURN(UINT64, sizeRestored);
}

/***********************************************************************************************************************************
Return new restore jobs as requested
***********************************************************************************************************************************/
typedef struct RestoreJobData
{
    Manifest *manifest;                                             // Backup manifest
    List *queueList;                                                // List of processing queues
    RegExp *zeroExp;                                                // Identify files that should be sparse zeroed
    const String *cipherSubPass;                                    // Passphrase used to decrypt files in the backup
} RestoreJobData;

// Helper to caculate the next queue to scan based on the client index
static int
restoreJobQueueNext(unsigned int clientIdx, int queueIdx, unsigned int queueTotal)
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

// Callback to fetch restore jobs for the parallel executor
static ProtocolParallelJob *restoreJobCallback(void *data, unsigned int clientIdx)
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
        RestoreJobData *jobData = data;

        // Determine where to begin scanning the queue (we'll stop when we get back here)
        int queueIdx = (int)(clientIdx % lstSize(jobData->queueList));
        int queueEnd = queueIdx;

        do
        {
            List *queue = *(List **)lstGet(jobData->queueList, (unsigned int)queueIdx);

            if (lstSize(queue) > 0)
            {
                const ManifestFile *file = *(ManifestFile **)lstGet(queue, 0);

                // Create restore job
                ProtocolCommand *command = protocolCommandNew(PROTOCOL_COMMAND_RESTORE_FILE_STR);

                protocolCommandParamAdd(command, VARSTR(file->name));
                protocolCommandParamAdd(
                    command, file->reference != NULL ?
                        VARSTR(file->reference) : VARSTR(manifestData(jobData->manifest)->backupLabel));
                protocolCommandParamAdd(command, VARBOOL(manifestData(jobData->manifest)->backupOptionCompress));
                protocolCommandParamAdd(command, VARSTR(restoreFilePgPath(jobData->manifest, file->name)));
                protocolCommandParamAdd(command, VARSTRZ(file->checksumSha1));
                protocolCommandParamAdd(command, VARBOOL(restoreFileZeroed(file->name, jobData->zeroExp)));
                protocolCommandParamAdd(command, VARUINT64(file->size));
                protocolCommandParamAdd(command, VARUINT64((uint64_t)file->timestamp));
                protocolCommandParamAdd(command, VARSTR(strNewFmt("%04o", file->mode)));
                protocolCommandParamAdd(command, VARSTR(file->user));
                protocolCommandParamAdd(command, VARSTR(file->group));
                protocolCommandParamAdd(command, VARUINT64((uint64_t)manifestData(jobData->manifest)->backupTimestampCopyStart));
                protocolCommandParamAdd(command, VARBOOL(cfgOptionBool(cfgOptDelta) || cfgOptionBool(cfgOptForce)));
                protocolCommandParamAdd(command, VARBOOL(cfgOptionBool(cfgOptForce)));
                protocolCommandParamAdd(command, VARSTR(jobData->cipherSubPass));

                // Remove job from the queue
                lstRemoveIdx(queue, 0);

                // Assign job to result
                result = protocolParallelJobMove(protocolParallelJobNew(VARSTR(file->name), command), MEM_CONTEXT_OLD());

                // Break out of the loop early since we found a job
                break;
            }

            queueIdx = restoreJobQueueNext(clientIdx, queueIdx, lstSize(jobData->queueList));
        }
        while (queueIdx != queueEnd);
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_TEST_RETURN(result);
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
        // Get information for the current user
        userInit();

        // PostgreSQL must be local
        if (!pgIsLocal(1))
            THROW(HostInvalidError, CFGCMD_RESTORE " command must be run on the " PG_NAME " host");

        // Validate restore path
        restorePathValidate();

        // Get the repo storage in case it is remote and encryption settings need to be pulled down
        storageRepo();

        // Load backup.info
        InfoBackup *infoBackup = infoBackupLoadFile(
            storageRepo(), INFO_BACKUP_PATH_FILE_STR, cipherType(cfgOptionStr(cfgOptRepoCipherType)),
            cfgOptionStr(cfgOptRepoCipherPass));

        // Get the backup set
        const String *backupSet = restoreBackupSet(infoBackup);

        // Load manifest
        RestoreJobData jobData = {0};

        jobData.manifest = manifestLoadFile(
            storageRepo(), strNewFmt(STORAGE_REPO_BACKUP "/%s/" BACKUP_MANIFEST_FILE, strPtr(backupSet)),
            cipherType(cfgOptionStr(cfgOptRepoCipherType)), infoPgCipherPass(infoBackupPg(infoBackup)));

        // Get the cipher subpass used to decrypt files in the backup
        jobData.cipherSubPass = manifestCipherSubPass(jobData.manifest);

        // Validate the manifest
        restoreManifestValidate(jobData.manifest, backupSet);

        // Log the backup set to restore
        LOG_INFO("restore backup set %s", strPtr(backupSet));

        // Map manifest
        restoreManifestMap(jobData.manifest);

        // Check that links are sane
        manifestLinkCheck(jobData.manifest);

        // Update ownership
        restoreManifestOwner(jobData.manifest);

        // Generate the selective restore expression
        String *expression = restoreSelectiveExpression(jobData.manifest);
        jobData.zeroExp = expression == NULL ? NULL : regExpNew(expression);

        // Clean the data directory and build path/link structure
        restoreCleanBuild(jobData.manifest);

        // Generate processing queues
        uint64_t sizeTotal = restoreProcessQueue(jobData.manifest, &jobData.queueList);

        // Save manifest to the data directory so we can restart a delta restore even if the PG_VERSION file is missing
        manifestSave(jobData.manifest, storageWriteIo(storageNewWriteNP(storagePgWrite(), BACKUP_MANIFEST_FILE_STR)));

        // Create the parallel executor
        ProtocolParallel *parallelExec = protocolParallelNew(
            (TimeMSec)(cfgOptionDbl(cfgOptProtocolTimeout) * MSEC_PER_SEC) / 2, restoreJobCallback, &jobData);

        for (unsigned int processIdx = 1; processIdx <= cfgOptionUInt(cfgOptProcessMax); processIdx++)
            protocolParallelClientAdd(parallelExec, protocolLocalGet(protocolStorageTypeRepo, processIdx));

        // Process jobs
        uint64_t sizeRestored = 0;

        do
        {
            unsigned int completed = protocolParallelProcess(parallelExec);

            for (unsigned int jobIdx = 0; jobIdx < completed; jobIdx++)
            {
                sizeRestored = restoreJobResult(
                    jobData.manifest, protocolParallelResult(parallelExec), jobData.zeroExp, sizeTotal, sizeRestored);
            }
        }
        while (!protocolParallelDone(parallelExec));

        // Write recovery settings
        restoreRecoveryWrite(jobData.manifest);

        // Remove backup.manifest
        storageRemoveNP(storagePgWrite(), BACKUP_MANIFEST_FILE_STR);

        // Sync file link paths. These need to be synced separately because they are not linked from the data directory.
        StringList *pathSynced = strLstNew();

        for (unsigned int targetIdx = 0; targetIdx < manifestTargetTotal(jobData.manifest); targetIdx++)
        {
            const ManifestTarget *target = manifestTarget(jobData.manifest, targetIdx);

            if (target->type == manifestTargetTypeLink && target->file != NULL)
            {
                const String *pgPath = manifestTargetPath(jobData.manifest, target);

                // Don't sync the same path twice.  There can be multiple links to files in the same path, but syncing it more than
                // once makes the logs noisy and looks like a bug even though it doesn't hurt anything or realistically affect
                // performance.
                if (strLstExists(pathSynced, pgPath))
                    continue;
                else
                    strLstAdd(pathSynced, pgPath);

                // Sync the path
                LOG_DETAIL("sync path '%s'", strPtr(pgPath));
                storagePathSyncNP(storageLocalWrite(), pgPath);
            }
        }

        // Sync paths in the data directory
        for (unsigned int pathIdx = 0; pathIdx < manifestPathTotal(jobData.manifest); pathIdx++)
        {
            const String *manifestName = manifestPath(jobData.manifest, pathIdx)->name;

            // Skip the pg_tblspc path because it only maps to the manifest.  We should remove this in a future release but not much
            // can be done about it for now.
            if (strEqZ(manifestName, MANIFEST_TARGET_PGTBLSPC))
                continue;

            // We'll sync global after pg_control is written
            if (strEq(manifestName, STRDEF(MANIFEST_TARGET_PGDATA "/" PG_PATH_GLOBAL)))
                continue;

            const String *pgPath = storagePathNP(storagePg(), manifestPgPath(manifestName));

            LOG_DETAIL("sync path '%s'", strPtr(pgPath));
            storagePathSyncNP(storagePgWrite(), pgPath);
        }

        // Rename pg_control
        if (storageExistsNP(storagePg(), STRDEF(PG_PATH_GLOBAL "/" PG_FILE_PGCONTROL "." STORAGE_FILE_TEMP_EXT)))
        {
            LOG_INFO(
                "restore " PG_PATH_GLOBAL "/" PG_FILE_PGCONTROL " (performed last to ensure aborted restores cannot be started)");

            storageMoveNP(
                storagePgWrite(),
                storageNewReadNP(storagePg(), STRDEF(PG_PATH_GLOBAL "/" PG_FILE_PGCONTROL "." STORAGE_FILE_TEMP_EXT)),
                storageNewWriteP(storagePgWrite(), STRDEF(PG_PATH_GLOBAL "/" PG_FILE_PGCONTROL), .noSyncPath = true));
        }
        else
            LOG_WARN("backup does not contain '" PG_PATH_GLOBAL "/" PG_FILE_PGCONTROL "' -- cluster will not start");

        // Sync global path
        LOG_DETAIL("sync path '%s'", strPtr(storagePathNP(storagePg(), PG_PATH_GLOBAL_STR)));
        storagePathSyncNP(storagePgWrite(), PG_PATH_GLOBAL_STR);
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}
