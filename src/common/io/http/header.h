/***********************************************************************************************************************************
Http Header

Object to track HTTP headers.  Headers can be marked as redacted so they are not logged.
***********************************************************************************************************************************/
#ifndef COMMON_IO_HTTP_HEADER_H
#define COMMON_IO_HTTP_HEADER_H

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
#define HTTP_HEADER_TYPE                                            HttpHeader
#define HTTP_HEADER_PREFIX                                          httpHeader

typedef struct HttpHeader HttpHeader;

#include "common/type/stringList.h"

/***********************************************************************************************************************************
Constructor
***********************************************************************************************************************************/
HttpHeader *httpHeaderNew(const StringList *redactList);
HttpHeader *httpHeaderDup(const HttpHeader *header, const StringList *redactList);

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
HttpHeader *httpHeaderAdd(HttpHeader *this, const String *key, const String *value);
const String *httpHeaderGet(const HttpHeader *this, const String *key);
StringList *httpHeaderList(const HttpHeader *this);
HttpHeader *httpHeaderMove(HttpHeader *this, MemContext *parentNew);
HttpHeader *httpHeaderPut(HttpHeader *this, const String *header, const String *value);

/***********************************************************************************************************************************
Getters
***********************************************************************************************************************************/
bool httpHeaderRedact(const HttpHeader *this, const String *key);

/***********************************************************************************************************************************
Destructor
***********************************************************************************************************************************/
void httpHeaderFree(HttpHeader *this);

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
String *httpHeaderToLog(const HttpHeader *this);

#define FUNCTION_LOG_HTTP_HEADER_TYPE                                                                                              \
    HttpHeader *
#define FUNCTION_LOG_HTTP_HEADER_FORMAT(value, buffer, bufferSize)                                                                 \
    FUNCTION_LOG_STRING_OBJECT_FORMAT(value, httpHeaderToLog, buffer, bufferSize)

#endif
