/***********************************************************************************************************************************
Http Header
***********************************************************************************************************************************/
#include "build.auto.h"

#include "common/debug.h"
#include "common/io/http/header.h"
#include "common/memContext.h"
#include "common/type/keyValue.h"

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
struct HttpHeader
{
    MemContext *memContext;                                         // Mem context
    const StringList *redactList;                                   // List of headers to redact during logging
    KeyValue *kv;                                                   // KeyValue store
};

/***********************************************************************************************************************************
New object
***********************************************************************************************************************************/
HttpHeader *
httpHeaderNew(const StringList *redactList)
{
    FUNCTION_TEST_VOID();

    HttpHeader *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("HttpHeader")
    {
        // Allocate state and set context
        this = memNew(sizeof(HttpHeader));
        this->memContext = MEM_CONTEXT_NEW();

        this->redactList = strLstDup(redactList);
        this->kv = kvNew();
    }
    MEM_CONTEXT_NEW_END();

    FUNCTION_TEST_RETURN(this);
}

/***********************************************************************************************************************************
Duplicate object
***********************************************************************************************************************************/
HttpHeader *
httpHeaderDup(const HttpHeader *header, const StringList *redactList)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(HTTP_HEADER, header);
        FUNCTION_TEST_PARAM(STRING_LIST, redactList);
    FUNCTION_TEST_END();

    HttpHeader *this = NULL;

    if (header != NULL)
    {

        MEM_CONTEXT_NEW_BEGIN("HttpHeader")
        {
            // Allocate state and set context
            this = memNew(sizeof(HttpHeader));
            this->memContext = MEM_CONTEXT_NEW();

            this->redactList = redactList == NULL ? strLstDup(header->redactList) : strLstDup(redactList);
            this->kv = kvDup(header->kv);
        }
        MEM_CONTEXT_NEW_END();
    }

    FUNCTION_TEST_RETURN(this);
}

/***********************************************************************************************************************************
Add a header
***********************************************************************************************************************************/
HttpHeader *
httpHeaderAdd(HttpHeader *this, const String *key, const String *value)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(HTTP_HEADER, this);
        FUNCTION_TEST_PARAM(STRING, key);
        FUNCTION_TEST_PARAM(STRING, value);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(key != NULL);
    ASSERT(value != NULL);

    // Make sure the key does not already exist
    const Variant *keyVar = VARSTR(key);

    if (kvGet(this->kv, keyVar) != NULL)
        THROW_FMT(AssertError, "key '%s' already exists", strPtr(key));

    // Store the key
    kvPut(this->kv, keyVar, VARSTR(value));

    FUNCTION_TEST_RETURN(this);
}

/***********************************************************************************************************************************
Get a value using the key
***********************************************************************************************************************************/
const String *
httpHeaderGet(const HttpHeader *this, const String *key)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(HTTP_HEADER, this);
        FUNCTION_TEST_PARAM(STRING, key);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(key != NULL);

    FUNCTION_TEST_RETURN(varStr(kvGet(this->kv, VARSTR(key))));
}

/***********************************************************************************************************************************
Get list of keys
***********************************************************************************************************************************/
StringList *
httpHeaderList(const HttpHeader *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(HTTP_HEADER, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(strLstSort(strLstNewVarLst(kvKeyList(this->kv)), sortOrderAsc));
}

/***********************************************************************************************************************************
Move object to a new mem context
***********************************************************************************************************************************/
HttpHeader *
httpHeaderMove(HttpHeader *this, MemContext *parentNew)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(HTTP_HEADER, this);
        FUNCTION_TEST_PARAM(MEM_CONTEXT, parentNew);
    FUNCTION_TEST_END();

    ASSERT(parentNew != NULL);

    if (this != NULL)
        memContextMove(this->memContext, parentNew);

    FUNCTION_TEST_RETURN(this);
}

/***********************************************************************************************************************************
Put a header
***********************************************************************************************************************************/
HttpHeader *
httpHeaderPut(HttpHeader *this, const String *key, const String *value)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(HTTP_HEADER, this);
        FUNCTION_TEST_PARAM(STRING, key);
        FUNCTION_TEST_PARAM(STRING, value);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(key != NULL);
    ASSERT(value != NULL);

    // Store the key
    kvPut(this->kv, VARSTR(key), VARSTR(value));

    FUNCTION_TEST_RETURN(this);
}

/***********************************************************************************************************************************
Should the header be redacted when logging?
***********************************************************************************************************************************/
bool
httpHeaderRedact(const HttpHeader *this, const String *key)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(HTTP_HEADER, this);
        FUNCTION_TEST_PARAM(STRING, key);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(key != NULL);

    FUNCTION_TEST_RETURN(this->redactList != NULL && strLstExists(this->redactList, key));
}

/***********************************************************************************************************************************
Render as string for logging
***********************************************************************************************************************************/
String *
httpHeaderToLog(const HttpHeader *this)
{
    String *result = strNew("{");
    const StringList *keyList = httpHeaderList(this);

    for (unsigned int keyIdx = 0; keyIdx < strLstSize(keyList); keyIdx++)
    {
        const String *key = strLstGet(keyList, keyIdx);

        if (strSize(result) != 1)
            strCat(result, ", ");

        if (httpHeaderRedact(this, key))
            strCatFmt(result, "%s: <redacted>", strPtr(key));
        else
            strCatFmt(result, "%s: '%s'", strPtr(key), strPtr(httpHeaderGet(this, key)));
    }

    strCat(result, "}");

    return result;
}

/***********************************************************************************************************************************
Free the object
***********************************************************************************************************************************/
void
httpHeaderFree(HttpHeader *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(HTTP_HEADER, this);
    FUNCTION_TEST_END();

    if (this != NULL)
        memContextFree(this->memContext);

    FUNCTION_TEST_RETURN_VOID();
}
