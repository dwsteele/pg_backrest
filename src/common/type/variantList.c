/***********************************************************************************************************************************
Variant List Handler
***********************************************************************************************************************************/
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "common/debug.h"
#include "common/memContext.h"
#include "common/type/list.h"
#include "common/type/variantList.h"

/***********************************************************************************************************************************
Wrapper for lstNew()
***********************************************************************************************************************************/
VariantList *
varLstNew(void)
{
    FUNCTION_TEST_VOID();
    FUNCTION_TEST_RESULT(VARIANT_LIST, (VariantList *)lstNew(sizeof(Variant *)));
}

/***********************************************************************************************************************************
Create a variant list from a string list
***********************************************************************************************************************************/
VariantList *
varLstNewStrLst(const StringList *stringList)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING_LIST, stringList);
    FUNCTION_TEST_END();

    VariantList *result = NULL;

    if (stringList != NULL)
    {
        result = varLstNew();

        for (unsigned int listIdx = 0; listIdx < strLstSize(stringList); listIdx++)
            varLstAdd(result, varNewStr(strLstGet(stringList, listIdx)));
    }

    FUNCTION_TEST_RESULT(VARIANT_LIST, result);
}

/***********************************************************************************************************************************
Duplicate a variant list
***********************************************************************************************************************************/
VariantList *
varLstDup(const VariantList *source)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(VARIANT_LIST, source);
    FUNCTION_TEST_END();

    VariantList *result = NULL;

    if (source != NULL)
    {
        result = varLstNew();

        for (unsigned int listIdx = 0; listIdx < varLstSize(source); listIdx++)
            varLstAdd(result, varDup(varLstGet(source, listIdx)));
    }

    FUNCTION_TEST_RESULT(VARIANT_LIST, result);
}

/***********************************************************************************************************************************
Wrapper for lstAdd()
***********************************************************************************************************************************/
VariantList *
varLstAdd(VariantList *this, Variant *data)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(VARIANT_LIST, this);
        FUNCTION_TEST_PARAM(VARIANT, data);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(VARIANT_LIST, (VariantList *)lstAdd((List *)this, &data));
}

/***********************************************************************************************************************************
Wrapper for lstGet()
***********************************************************************************************************************************/
Variant *
varLstGet(const VariantList *this, unsigned int listIdx)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(VARIANT_LIST, this);
        FUNCTION_TEST_PARAM(UINT, listIdx);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(VARIANT, *(Variant **)lstGet((List *)this, listIdx));
}

/***********************************************************************************************************************************
Wrapper for lstSize()
***********************************************************************************************************************************/
unsigned int
varLstSize(const VariantList *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(VARIANT_LIST, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(UINT, lstSize((List *)this));
}

/***********************************************************************************************************************************
Wrapper for lstFree()
***********************************************************************************************************************************/
void
varLstFree(VariantList *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(VARIANT_LIST, this);
    FUNCTION_TEST_END();

    lstFree((List *)this);

    FUNCTION_TEST_RESULT_VOID();
}
