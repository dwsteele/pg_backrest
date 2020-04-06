/***********************************************************************************************************************************
Http Query

Object to track HTTP queries and output them with proper escaping.
***********************************************************************************************************************************/
#ifndef COMMON_IO_HTTP_QUERY_H
#define COMMON_IO_HTTP_QUERY_H

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
#define HTTP_QUERY_TYPE                                             HttpQuery
#define HTTP_QUERY_PREFIX                                           httpQuery

typedef struct HttpQuery HttpQuery;

#include "common/type/stringList.h"

/***********************************************************************************************************************************
Constructors
***********************************************************************************************************************************/
HttpQuery *httpQueryNew(void);

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
// Add a query item
HttpQuery *httpQueryAdd(HttpQuery *this, const String *key, const String *value);

// Get a value using the key
const String *httpQueryGet(const HttpQuery *this, const String *key);

// Get list of keys
StringList *httpQueryList(const HttpQuery *this);

// Move to a new parent mem context
HttpQuery *httpQueryMove(HttpQuery *this, MemContext *parentNew);

//Put a query item
HttpQuery *httpQueryPut(HttpQuery *this, const String *header, const String *value);

// Render the query for inclusion in an http request
String *httpQueryRender(const HttpQuery *this);

/***********************************************************************************************************************************
Destructor
***********************************************************************************************************************************/
void httpQueryFree(HttpQuery *this);

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
String *httpQueryToLog(const HttpQuery *this);

#define FUNCTION_LOG_HTTP_QUERY_TYPE                                                                                               \
    HttpQuery *
#define FUNCTION_LOG_HTTP_QUERY_FORMAT(value, buffer, bufferSize)                                                                  \
    FUNCTION_LOG_STRING_OBJECT_FORMAT(value, httpQueryToLog, buffer, bufferSize)

#endif
