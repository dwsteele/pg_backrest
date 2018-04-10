/***********************************************************************************************************************************
Command and Option Configuration

Automatically generated by Build.pm -- do not modify directly.
***********************************************************************************************************************************/
#ifndef XS_CONFIG_CONFIG_AUTO_H
#define XS_CONFIG_CONFIG_AUTO_H

/***********************************************************************************************************************************
Command constants
***********************************************************************************************************************************/
#define CFGCMD_ARCHIVE_GET                                          cfgCmdArchiveGet
#define CFGCMD_ARCHIVE_PUSH                                         cfgCmdArchivePush
#define CFGCMD_BACKUP                                               cfgCmdBackup
#define CFGCMD_CHECK                                                cfgCmdCheck
#define CFGCMD_EXPIRE                                               cfgCmdExpire
#define CFGCMD_HELP                                                 cfgCmdHelp
#define CFGCMD_INFO                                                 cfgCmdInfo
#define CFGCMD_LOCAL                                                cfgCmdLocal
#define CFGCMD_REMOTE                                               cfgCmdRemote
#define CFGCMD_RESTORE                                              cfgCmdRestore
#define CFGCMD_STANZA_CREATE                                        cfgCmdStanzaCreate
#define CFGCMD_STANZA_DELETE                                        cfgCmdStanzaDelete
#define CFGCMD_STANZA_UPGRADE                                       cfgCmdStanzaUpgrade
#define CFGCMD_START                                                cfgCmdStart
#define CFGCMD_STOP                                                 cfgCmdStop
#define CFGCMD_VERSION                                              cfgCmdVersion

/***********************************************************************************************************************************
Option constants
***********************************************************************************************************************************/
#define CFGOPT_ARCHIVE_ASYNC                                        cfgOptArchiveAsync
#define CFGOPT_ARCHIVE_CHECK                                        cfgOptArchiveCheck
#define CFGOPT_ARCHIVE_COPY                                         cfgOptArchiveCopy
#define CFGOPT_ARCHIVE_QUEUE_MAX                                    cfgOptArchiveQueueMax
#define CFGOPT_ARCHIVE_TIMEOUT                                      cfgOptArchiveTimeout
#define CFGOPT_BACKUP_STANDBY                                       cfgOptBackupStandby
#define CFGOPT_BUFFER_SIZE                                          cfgOptBufferSize
#define CFGOPT_CHECKSUM_PAGE                                        cfgOptChecksumPage
#define CFGOPT_CMD_SSH                                              cfgOptCmdSsh
#define CFGOPT_COMMAND                                              cfgOptCommand
#define CFGOPT_COMPRESS                                             cfgOptCompress
#define CFGOPT_COMPRESS_LEVEL                                       cfgOptCompressLevel
#define CFGOPT_COMPRESS_LEVEL_NETWORK                               cfgOptCompressLevelNetwork
#define CFGOPT_CONFIG                                               cfgOptConfig
#define CFGOPT_CONFIG_INCLUDE_PATH                                  cfgOptConfigIncludePath
#define CFGOPT_CONFIG_PATH                                          cfgOptConfigPath
#define CFGOPT_DB_INCLUDE                                           cfgOptDbInclude
#define CFGOPT_DB_TIMEOUT                                           cfgOptDbTimeout
#define CFGOPT_DELTA                                                cfgOptDelta
#define CFGOPT_FORCE                                                cfgOptForce
#define CFGOPT_HOST_ID                                              cfgOptHostId
#define CFGOPT_LINK_ALL                                             cfgOptLinkAll
#define CFGOPT_LINK_MAP                                             cfgOptLinkMap
#define CFGOPT_LOCK_PATH                                            cfgOptLockPath
#define CFGOPT_LOG_LEVEL_CONSOLE                                    cfgOptLogLevelConsole
#define CFGOPT_LOG_LEVEL_FILE                                       cfgOptLogLevelFile
#define CFGOPT_LOG_LEVEL_STDERR                                     cfgOptLogLevelStderr
#define CFGOPT_LOG_PATH                                             cfgOptLogPath
#define CFGOPT_LOG_TIMESTAMP                                        cfgOptLogTimestamp
#define CFGOPT_MANIFEST_SAVE_THRESHOLD                              cfgOptManifestSaveThreshold
#define CFGOPT_NEUTRAL_UMASK                                        cfgOptNeutralUmask
#define CFGOPT_ONLINE                                               cfgOptOnline
#define CFGOPT_OUTPUT                                               cfgOptOutput
#define CFGOPT_PERL_OPTION                                          cfgOptPerlOption
#define CFGOPT_PG_HOST                                              cfgOptPgHost
#define CFGOPT_PG_HOST_CMD                                          cfgOptPgHostCmd
#define CFGOPT_PG_HOST_CONFIG                                       cfgOptPgHostConfig
#define CFGOPT_PG_HOST_PORT                                         cfgOptPgHostPort
#define CFGOPT_PG_HOST_USER                                         cfgOptPgHostUser
#define CFGOPT_PG_PATH                                              cfgOptPgPath
#define CFGOPT_PG_PORT                                              cfgOptPgPort
#define CFGOPT_PG_SOCKET_PATH                                       cfgOptPgSocketPath
#define CFGOPT_PROCESS                                              cfgOptProcess
#define CFGOPT_PROCESS_MAX                                          cfgOptProcessMax
#define CFGOPT_PROTOCOL_TIMEOUT                                     cfgOptProtocolTimeout
#define CFGOPT_RECOVERY_OPTION                                      cfgOptRecoveryOption
#define CFGOPT_REPO_CIPHER_PASS                                     cfgOptRepoCipherPass
#define CFGOPT_REPO_CIPHER_TYPE                                     cfgOptRepoCipherType
#define CFGOPT_REPO_HARDLINK                                        cfgOptRepoHardlink
#define CFGOPT_REPO_HOST                                            cfgOptRepoHost
#define CFGOPT_REPO_HOST_CMD                                        cfgOptRepoHostCmd
#define CFGOPT_REPO_HOST_CONFIG                                     cfgOptRepoHostConfig
#define CFGOPT_REPO_HOST_PORT                                       cfgOptRepoHostPort
#define CFGOPT_REPO_HOST_USER                                       cfgOptRepoHostUser
#define CFGOPT_REPO_PATH                                            cfgOptRepoPath
#define CFGOPT_REPO_RETENTION_ARCHIVE                               cfgOptRepoRetentionArchive
#define CFGOPT_REPO_RETENTION_ARCHIVE_TYPE                          cfgOptRepoRetentionArchiveType
#define CFGOPT_REPO_RETENTION_DIFF                                  cfgOptRepoRetentionDiff
#define CFGOPT_REPO_RETENTION_FULL                                  cfgOptRepoRetentionFull
#define CFGOPT_REPO_S3_BUCKET                                       cfgOptRepoS3Bucket
#define CFGOPT_REPO_S3_CA_FILE                                      cfgOptRepoS3CaFile
#define CFGOPT_REPO_S3_CA_PATH                                      cfgOptRepoS3CaPath
#define CFGOPT_REPO_S3_ENDPOINT                                     cfgOptRepoS3Endpoint
#define CFGOPT_REPO_S3_HOST                                         cfgOptRepoS3Host
#define CFGOPT_REPO_S3_KEY                                          cfgOptRepoS3Key
#define CFGOPT_REPO_S3_KEY_SECRET                                   cfgOptRepoS3KeySecret
#define CFGOPT_REPO_S3_REGION                                       cfgOptRepoS3Region
#define CFGOPT_REPO_S3_VERIFY_SSL                                   cfgOptRepoS3VerifySsl
#define CFGOPT_REPO_TYPE                                            cfgOptRepoType
#define CFGOPT_RESUME                                               cfgOptResume
#define CFGOPT_SET                                                  cfgOptSet
#define CFGOPT_SPOOL_PATH                                           cfgOptSpoolPath
#define CFGOPT_STANZA                                               cfgOptStanza
#define CFGOPT_START_FAST                                           cfgOptStartFast
#define CFGOPT_STOP_AUTO                                            cfgOptStopAuto
#define CFGOPT_TABLESPACE_MAP                                       cfgOptTablespaceMap
#define CFGOPT_TABLESPACE_MAP_ALL                                   cfgOptTablespaceMapAll
#define CFGOPT_TARGET                                               cfgOptTarget
#define CFGOPT_TARGET_ACTION                                        cfgOptTargetAction
#define CFGOPT_TARGET_EXCLUSIVE                                     cfgOptTargetExclusive
#define CFGOPT_TARGET_TIMELINE                                      cfgOptTargetTimeline
#define CFGOPT_TEST                                                 cfgOptTest
#define CFGOPT_TEST_DELAY                                           cfgOptTestDelay
#define CFGOPT_TEST_POINT                                           cfgOptTestPoint
#define CFGOPT_TYPE                                                 cfgOptType

#endif
