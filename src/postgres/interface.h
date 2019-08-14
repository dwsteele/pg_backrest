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
#define PG_FILE_POSTMASTERPID                                       "postmaster.pid"

#define PG_PATH_ARCHIVE_STATUS                                      "archive_status"
#define PG_PATH_GLOBAL                                              "global"

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

    uint32_t controlVersion;
    uint32_t catalogVersion;

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
PgControl pgControlFromFile(const Storage *storage, const String *pgPath);
PgControl pgControlFromBuffer(const Buffer *controlFile);
unsigned int pgVersionFromStr(const String *version);
String *pgVersionToStr(unsigned int version);

PgWal pgWalFromFile(const String *walFile);
PgWal pgWalFromBuffer(const Buffer *walBuffer);

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
