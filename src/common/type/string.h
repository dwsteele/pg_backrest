/***********************************************************************************************************************************
String Handler
***********************************************************************************************************************************/
#ifndef COMMON_TYPE_STRING_H
#define COMMON_TYPE_STRING_H

/***********************************************************************************************************************************
String object
***********************************************************************************************************************************/
typedef struct String String;

#include "common/type/buffer.h"

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
String *strNew(const char *string);
String *strNewBuf(const Buffer *buffer);
String *strNewFmt(const char *format, ...) __attribute__((format(printf, 1, 2)));
String *strNewN(const char *string, size_t size);

String *strBase(const String *this);
bool strBeginsWith(const String *this, const String *beginsWith);
bool strBeginsWithZ(const String *this, const char *beginsWith);
String *strCat(String *this, const char *cat);
String *strCatChr(String *this, char cat);
String *strCatFmt(String *this, const char *format, ...) __attribute__((format(printf, 2, 3)));
int strCmp(const String *this, const String *compare);
int strCmpZ(const String *this, const char *compare);
String *strDup(const String *this);
bool strEmpty(const String *this);
bool strEndsWith(const String *this, const String *endsWith);
bool strEndsWithZ(const String *this, const char *endsWith);
bool strEq(const String *this, const String *compare);
bool strEqZ(const String *this, const char *compare);
String *strFirstUpper(String *this);
String *strFirstLower(String *this);
String *strUpper(String *this);
String *strLower(String *this);
String *strPath(const String *this);
const char *strPtr(const String *this);
String *strQuote(const String *this, const String *quote);
String *strQuoteZ(const String *this, const char *quote);
String *strReplaceChr(String *this, char find, char replace);
size_t strSize(const String *this);
String *strSub(const String *this, size_t start);
String *strSubN(const String *this, size_t start, size_t size);
String *strTrim(String *this);
int strChr(const String *this, char chr);
String *strTrunc(String *this, int idx);

void strFree(String *this);

/***********************************************************************************************************************************
Fields that are common between dynamically allocated and constant strings

There is nothing user-accessible here but this construct allows constant strings to be created and then handled by the same
functions that process dynamically allocated strings.
***********************************************************************************************************************************/
struct StringCommon
{
    size_t size;
    char *buffer;
};

/***********************************************************************************************************************************
Macros for constant strings

Frequently used constant strings can be declared with these macros at compile time rather than dynamically at run time.

Note that strings created in this way are declared as const so can't be modified or freed by the str*() methods.  Casting to
String * will result in a segfault due to modifying read-only memory.

By convention all string constant identifiers are appended with _STR.
***********************************************************************************************************************************/
// Create a string constant inline.  Useful when the constant will only be use once.
#define STRING_CONST(value)                                                                                                        \
    ((const String *)&(const struct StringCommon){.size = sizeof(value) - 1, .buffer = (char *)value})

// Used to declare string constants that will be externed using STRING_DECLARE().  Must be used in a .c file.
#define STRING_EXTERN(name, value)                                                                                                 \
    const String *name = STRING_CONST(value)

// Used to declare string constants that will be local to the .c file.  Must be used in a .c file.
#define STRING_STATIC(name, value)                                                                                                 \
    static const String *name = STRING_CONST(value)

// Used to extern string constants declared with STRING_EXTERN(.  Must be used in a .h file.
#define STRING_DECLARE(name)                                                                                                       \
    extern const String *name

/***********************************************************************************************************************************
Constant strings that are generally useful
***********************************************************************************************************************************/
STRING_DECLARE(CR_STR);
STRING_DECLARE(EMPTY_STR);
STRING_DECLARE(FSLASH_STR);
STRING_DECLARE(LF_STR);
STRING_DECLARE(N_STR);
STRING_DECLARE(NULL_STR);
STRING_DECLARE(Y_STR);
STRING_DECLARE(ZERO_STR);

/***********************************************************************************************************************************
Helper function/macro for object logging
***********************************************************************************************************************************/
typedef String *(*StrObjToLogFormat)(const void *object);

size_t strObjToLog(const void *object, StrObjToLogFormat formatFunc, char *buffer, size_t bufferSize);

#define FUNCTION_DEBUG_STRING_OBJECT_FORMAT(object, formatFunc, buffer, bufferSize)                                                \
    strObjToLog(object, (StrObjToLogFormat)formatFunc, buffer, bufferSize)

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
String *strToLog(const String *this);

#define FUNCTION_DEBUG_CONST_STRING_TYPE                                                                                           \
    const String *
#define FUNCTION_DEBUG_CONST_STRING_FORMAT(value, buffer, bufferSize)                                                              \
    FUNCTION_DEBUG_STRING_FORMAT(value, buffer, bufferSize)

#define FUNCTION_DEBUG_STRING_TYPE                                                                                                 \
    String *
#define FUNCTION_DEBUG_STRING_FORMAT(value, buffer, bufferSize)                                                                    \
    FUNCTION_DEBUG_STRING_OBJECT_FORMAT(value, strToLog, buffer, bufferSize)

#define FUNCTION_DEBUG_STRINGP_TYPE                                                                                                \
    const String **
#define FUNCTION_DEBUG_STRINGP_FORMAT(value, buffer, bufferSize)                                                                   \
    ptrToLog(value, "String **", buffer, bufferSize)

#endif
