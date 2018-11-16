/***********************************************************************************************************************************
Manifest Info Handler
***********************************************************************************************************************************/
#include "common/type/string.h"

/* CSHANG It is not good to have a dependency on a config name so they've been removed. We have renamed config options before and
it's OK to affect the config file but changes to the info files would require a format version bump. option-hardlink is a prime
example as the config option name changed to repo-hardlink
These keys will replace the following perl keys:
MANIFEST_KEY_ARCHIVE_START and INFO_BACKUP_KEY_ARCHIVE_START
MANIFEST_KEY_ARCHIVE_STOP and INFO_BACKUP_KEY_ARCHIVE_STOP
MANIFEST_KEY_PRIOR and INFO_BACKUP_KEY_PRIOR
MANIFEST_KEY_TIMESTAMP_START and INFO_BACKUP_KEY_TIMESTAMP_START
MANIFEST_KEY_TIMESTAMP_STOP and INFO_BACKUP_KEY_TIMESTAMP_STOP
MANIFEST_KEY_TYPE and INFO_BACKUP_KEY_TYPE
-- options:
MANIFEST_KEY_BACKUP_STANDBY and INFO_BACKUP_KEY_BACKUP_STANDBY
MANIFEST_KEY_ARCHIVE_CHECK and INFO_BACKUP_KEY_ARCHIVE_CHECK
MANIFEST_KEY_ARCHIVE_COPY and INFO_BACKUP_KEY_ARCHIVE_COPY
MANIFEST_KEY_CHECKSUM_PAGE INFO_BACKUP_KEY_CHECKSUM_PAGE
MANIFEST_KEY_COMPRESS and INFO_BACKUP_KEY_COMPRESS
MANIFEST_KEY_HARDLINK and INFO_BACKUP_KEY_HARDLINK
MANIFEST_KEY_ONLINE and INFO_BACKUP_KEY_ONLINE
*/
STRING_EXTERN(INFO_MANIFEST_KEY_BACKUP_ARCHIVE_START_STR,           "backup-archive-start")
STRING_EXTERN(INFO_MANIFEST_KEY_BACKUP_ARCHIVE_STOP_STR,            "backup-archive-stop")
STRING_EXTERN(INFO_MANIFEST_KEY_BACKUP_PRIOR_STR,                   "backup-prior")
STRING_EXTERN(INFO_MANIFEST_KEY_BACKUP_TIMESTAMP_START_STR,         "backup-timestamp-start")
STRING_EXTERN(INFO_MANIFEST_KEY_BACKUP_TIMESTAMP_STOP_STR,          "backup-timestamp-stop")
STRING_EXTERN(INFO_MANIFEST_KEY_BACKUP_TYPE_STR,                    "backup-type")
STRING_EXTERN(INFO_MANIFEST_KEY_OPT_ARCHIVE_CHECK_STR,              "option-archive-check")
STRING_EXTERN(INFO_MANIFEST_KEY_OPT_ARCHIVE_COPY_STR,               "option-archive-copy")
STRING_EXTERN(INFO_MANIFEST_KEY_OPT_BACKUP_STANDBY_STR,             "option-backup-standby")
STRING_EXTERN(INFO_MANIFEST_KEY_OPT_CHECKSUM_PAGE_STR,              "option-checksum-page")
STRING_EXTERN(INFO_MANIFEST_KEY_OPT_COMPRESS_STR,                   "option-compress")
STRING_EXTERN(INFO_MANIFEST_KEY_OPT_HARDLINK_STR,                   "option-hardlink")
STRING_EXTERN(INFO_MANIFEST_KEY_OPT_ONLINE_STR,                     "option-online")
