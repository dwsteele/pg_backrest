/***********************************************************************************************************************************
PostgreSQL Info Handler
***********************************************************************************************************************************/
#ifndef INFO_INFOPG_H
#define INFO_INFOPG_H

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
typedef struct InfoPg InfoPg;

#include <stdint.h>

#include "crypto/crypto.h"
#include "storage/storage.h"
#include "common/ini.h"

/***********************************************************************************************************************************
Constants
***********************************************************************************************************************************/
#define INFO_KEY_DB_ID                                              "db-id"
    STRING_DECLARE(INFO_KEY_DB_ID_STR);

/***********************************************************************************************************************************
Information about the PostgreSQL cluster
***********************************************************************************************************************************/
typedef struct InfoPgData
{
    unsigned int id;
    uint64_t systemId;
    uint32_t catalogVersion;
    uint32_t controlVersion;
    unsigned int version;
} InfoPgData;

/***********************************************************************************************************************************
Info types for determining data in DB section
***********************************************************************************************************************************/
typedef enum
{
    infoPgArchive,                                                  // archive info file
    infoPgBackup,                                                   // backup info file
    infoPgManifest,                                                 // manifest file
} InfoPgType;

/***********************************************************************************************************************************
Constructor
***********************************************************************************************************************************/
InfoPg *infoPgNew(const Storage *storage, const String *fileName, InfoPgType type, CipherType cipherType, const String *cipherPass);

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
void infoPgAdd(InfoPg *this, const InfoPgData *infoPgData);

/***********************************************************************************************************************************
Getters
***********************************************************************************************************************************/
String *infoPgArchiveId(const InfoPg *this, unsigned int pgDataIdx);
const String *infoPgCipherPass(const InfoPg *this);
InfoPgData infoPgData(const InfoPg *this, unsigned int pgDataIdx);
InfoPgData infoPgDataCurrent(const InfoPg *this);
unsigned int infoPgDataTotal(const InfoPg *this);
Ini *infoPgIni(const InfoPg *this);

/***********************************************************************************************************************************
Destructor
***********************************************************************************************************************************/
void infoPgFree(InfoPg *this);

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
String *infoPgDataToLog(const InfoPgData *this);

#define FUNCTION_DEBUG_INFO_PG_TYPE                                                                                                \
    InfoPg *
#define FUNCTION_DEBUG_INFO_PG_FORMAT(value, buffer, bufferSize)                                                                   \
    objToLog(value, "InfoPg", buffer, bufferSize)
#define FUNCTION_DEBUG_INFO_PG_DATA_TYPE                                                                                           \
    InfoPgData
#define FUNCTION_DEBUG_INFO_PG_DATA_FORMAT(value, buffer, bufferSize)                                                              \
    FUNCTION_DEBUG_STRING_OBJECT_FORMAT(&value, infoPgDataToLog, buffer, bufferSize)
#define FUNCTION_DEBUG_INFO_PG_DATAP_TYPE                                                                                          \
    InfoPgData *
#define FUNCTION_DEBUG_INFO_PG_DATAP_FORMAT(value, buffer, bufferSize)                                                             \
    FUNCTION_DEBUG_STRING_OBJECT_FORMAT(value, infoPgDataToLog, buffer, bufferSize)


#endif
