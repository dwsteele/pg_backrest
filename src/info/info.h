/***********************************************************************************************************************************
Info Handler
***********************************************************************************************************************************/
#ifndef INFO_INFO_H
#define INFO_INFO_H

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
typedef struct Info Info;

#include "common/ini.h"
#include "crypto/hash.h"
#include "storage/storage.h"

/***********************************************************************************************************************************
Constants
***********************************************************************************************************************************/
STRING_DECLARE(INI_KEY_VERSION_STR)
STRING_DECLARE(INI_KEY_FORMAT_STR)

/***********************************************************************************************************************************
Constructor
***********************************************************************************************************************************/
Info *infoNew(const Storage *storage, const String *fileName);

/***********************************************************************************************************************************
Getters
***********************************************************************************************************************************/
String *infoFileName(const Info *this);
Ini *infoIni(const Info *this);

/***********************************************************************************************************************************
Destructor
***********************************************************************************************************************************/
void infoFree(Info *this);

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
#define FUNCTION_DEBUG_INFO_TYPE                                                                                                   \
    Info *
#define FUNCTION_DEBUG_INFO_FORMAT(value, buffer, bufferSize)                                                                      \
    objToLog(value, "Info", buffer, bufferSize)

#endif
