/***********************************************************************************************************************************
PostgreSQL Interface
***********************************************************************************************************************************/
#ifndef POSTGRES_INTERFACE_H
#define POSTGRES_INTERFACE_H

#include <stdint.h>
#include <sys/types.h>

#include "common/type/string.h"
#include "storage/storage.h"

/***********************************************************************************************************************************
Defines for various Postgres paths and files
***********************************************************************************************************************************/
#define PG_FILE_PGCONTROL                                           "pg_control"
#define PG_FILE_PGVERSION                                           "PG_VERSION"
    STRING_DECLARE(PG_FILE_PGVERSION_STR);
#define PG_FILE_POSTGRESQLAUTOCONF                                  "postgresql.auto.conf"
    STRING_DECLARE(PG_FILE_POSTGRESQLAUTOCONF_STR);
#define PG_FILE_POSTMASTERPID                                       "postmaster.pid"
    STRING_DECLARE(PG_FILE_POSTMASTERPID_STR);
#define PG_FILE_RECOVERYCONF                                        "recovery.conf"
    STRING_DECLARE(PG_FILE_RECOVERYCONF_STR);
#define PG_FILE_RECOVERYSIGNAL                                      "recovery.signal"
    STRING_DECLARE(PG_FILE_RECOVERYSIGNAL_STR);
#define PG_FILE_STANDBYSIGNAL                                       "standby.signal"
    STRING_DECLARE(PG_FILE_STANDBYSIGNAL_STR);
#define PG_FILE_TABLESPACEMAP                                       "tablespace_map"

#define PG_PATH_ARCHIVE_STATUS                                      "archive_status"
#define PG_PATH_BASE                                                "base"
#define PG_PATH_GLOBAL                                              "global"
    STRING_DECLARE(PG_PATH_GLOBAL_STR);

#define PG_NAME_WAL                                                 "wal"
    STRING_DECLARE(PG_NAME_WAL_STR);
#define PG_NAME_XLOG                                                "xlog"
    STRING_DECLARE(PG_NAME_XLOG_STR);

/***********************************************************************************************************************************
Name of default PostgreSQL database used for running all queries and commands
***********************************************************************************************************************************/
#define PG_DB_POSTGRES                                              "postgres"
    STRING_DECLARE(PG_DB_POSTGRES_STR);

/***********************************************************************************************************************************
Define default page size

Page size can only be changed at compile time and is not known to be well-tested, so only the default page size is supported.
***********************************************************************************************************************************/
#define PG_PAGE_SIZE_DEFAULT                                        ((unsigned int)(8 * 1024))

/***********************************************************************************************************************************
Define the minimum oid that can be used for a user object

Everything below this number should have been created at initdb time.
***********************************************************************************************************************************/
#define PG_USER_OBJECT_MIN_ID                                       16384

/***********************************************************************************************************************************
Define default segment size and pages per segment

Segment size can only be changed at compile time and is not known to be well-tested, so only the default segment size is supported.
***********************************************************************************************************************************/
#define PG_SEGMENT_SIZE_DEFAULT                                     ((unsigned int)(1 * 1024 * 1024 * 1024))
#define PG_SEGMENT_PAGE_DEFAULT                                     (PG_SEGMENT_SIZE_DEFAULT / PG_PAGE_SIZE_DEFAULT)

/***********************************************************************************************************************************
PostgreSQL Control File Info
***********************************************************************************************************************************/
typedef struct PgControl
{
    unsigned int version;
    uint64_t systemId;

    unsigned int pageSize;
    unsigned int walSegmentSize;

    bool pageChecksum;
} PgControl;

/***********************************************************************************************************************************
PostgreSQL WAL Info
***********************************************************************************************************************************/
typedef struct PgWal
{
    unsigned int version;
    uint64_t systemId;
} PgWal;

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
uint32_t pgCatalogVersion(unsigned int pgVersion);
PgControl pgControlFromFile(const Storage *storage);
PgControl pgControlFromBuffer(const Buffer *controlFile);
uint32_t pgControlVersion(unsigned int pgVersion);
unsigned int pgVersionFromStr(const String *version);
String *pgVersionToStr(unsigned int version);

PgWal pgWalFromFile(const String *walFile);
PgWal pgWalFromBuffer(const Buffer *walBuffer);

// Get the tablespace identifier used to distinguish versions in a tablespace directory, e.g. PG_9.0_201008051
String *pgTablespaceId(unsigned int pgVersion);

const String *pgWalName(unsigned int pgVersion);

/***********************************************************************************************************************************
Test Functions
***********************************************************************************************************************************/
#ifdef DEBUG
    Buffer *pgControlTestToBuffer(PgControl pgControl);
    void pgWalTestToBuffer(PgWal pgWal, Buffer *walBuffer);
#endif

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
String *pgControlToLog(const PgControl *pgControl);
String *pgWalToLog(const PgWal *pgWal);

#define FUNCTION_LOG_PG_CONTROL_TYPE                                                                                               \
    PgControl
#define FUNCTION_LOG_PG_CONTROL_FORMAT(value, buffer, bufferSize)                                                                  \
    FUNCTION_LOG_STRING_OBJECT_FORMAT(&value, pgControlToLog, buffer, bufferSize)

#define FUNCTION_LOG_PG_WAL_TYPE                                                                                                   \
    PgWal
#define FUNCTION_LOG_PG_WAL_FORMAT(value, buffer, bufferSize)                                                                      \
    FUNCTION_LOG_STRING_OBJECT_FORMAT(&value, pgWalToLog, buffer, bufferSize)

#endif
