/***********************************************************************************************************************************
Test String Lists
***********************************************************************************************************************************/
#include "common/stackTrace.h"

/***********************************************************************************************************************************
Test Run
***********************************************************************************************************************************/
void
testRun(void)
{
    FUNCTION_HARNESS_VOID();

    // *****************************************************************************************************************************
    if (testBegin("strLstNew(), strLstAdd, strLstGet(), strLstMove(), strLstSize(), and strLstFree()"))
    {
        // Add strings to the list
        // -------------------------------------------------------------------------------------------------------------------------
        StringList *list = NULL;

        MEM_CONTEXT_TEMP_BEGIN()
        {
            list = strLstNew();

            for (int listIdx = 0; listIdx <= LIST_INITIAL_SIZE; listIdx++)
            {
                if (listIdx == 0)
                {
                    TEST_RESULT_PTR(strLstAdd(list, NULL), list, "add null item");
                }
                else
                    TEST_RESULT_PTR(strLstAdd(list, strNewFmt("STR%02d", listIdx)), list, "add item %d", listIdx);
            }

            strLstMove(list, MEM_CONTEXT_OLD());
        }
        MEM_CONTEXT_TEMP_END();

        TEST_RESULT_INT(strLstSize(list), 9, "list size");

        // Read them back and check values
        // -------------------------------------------------------------------------------------------------------------------------
        for (unsigned int listIdx = 0; listIdx < strLstSize(list); listIdx++)
        {
            if (listIdx == 0)
            {
                TEST_RESULT_PTR(strLstGet(list, listIdx), NULL, "check null item");
            }
            else
                TEST_RESULT_STR(strPtr(strLstGet(list, listIdx)), strPtr(strNewFmt("STR%02u", listIdx)), "check item %u", listIdx);
        }

        TEST_RESULT_VOID(strLstFree(list), "free string list");
        TEST_RESULT_VOID(strLstFree(NULL), "free null string list");
    }

    // *****************************************************************************************************************************
    if (testBegin("strLstNewSplit()"))
    {
        TEST_RESULT_STR(strPtr(strLstJoin(strLstNewSplit(strNew(""), strNew(", ")), ", ")), "", "empty list");
        TEST_RESULT_STR(strPtr(strLstJoin(strLstNewSplit(strNew("item1"), strNew(", ")), ", ")), "item1", "one item");
        TEST_RESULT_STR(
            strPtr(strLstJoin(strLstNewSplit(strNew("item1, item2"), strNew(", ")), ", ")), "item1, item2", "two items");
    }

    // *****************************************************************************************************************************
    if (testBegin("strLstNewSplitSize()"))
    {
        TEST_RESULT_STR(strPtr(strLstJoin(strLstNewSplitSize(strNew(""), strNew(" "), 0), ", ")), "", "empty list");
        TEST_RESULT_STR(strPtr(strLstJoin(strLstNewSplitSizeZ(strNew("abc def"), " ", 3), "-")), "abc-def", "two items");
        TEST_RESULT_STR(strPtr(strLstJoin(strLstNewSplitSizeZ(strNew("abc def"), " ", 4), "-")), "abc-def", "one items");
        TEST_RESULT_STR(strPtr(strLstJoin(strLstNewSplitSizeZ(strNew("abc def ghi"), " ", 4), "-")), "abc-def-ghi", "three items");
        TEST_RESULT_STR(strPtr(strLstJoin(strLstNewSplitSizeZ(strNew("abc def ghi"), " ", 8), "-")), "abc def-ghi", "three items");
        TEST_RESULT_STR(strPtr(strLstJoin(strLstNewSplitSizeZ(strNew("abc def "), " ", 4), "-")), "abc-def ", "two items");

        TEST_RESULT_STR(
            strPtr(strLstJoin(strLstNewSplitSize(strNew("this is a short sentence"), strNew(" "), 10), "\n")),
            "this is a\n"
            "short\n"
            "sentence",
            "empty list");
    }

    // *****************************************************************************************************************************
    if (testBegin("strLstNewVarLst()"))
    {
        VariantList *varList = varLstNew();

        varLstAdd(varList, varNewStr(strNew("string1")));
        varLstAdd(varList, varNewStr(strNew("string2")));

        TEST_RESULT_STR(strPtr(strLstJoin(strLstNewVarLst(varList), ", ")), "string1, string2", "string list from variant list");

        varLstFree(varList);
    }

    // *****************************************************************************************************************************
    if (testBegin("strLstPtr()"))
    {
        StringList *list = strLstNew();

        // Add strings to the list
        // -------------------------------------------------------------------------------------------------------------------------
        for (int listIdx = 0; listIdx <= 3; listIdx++)
        {
            if (listIdx == 0)
                strLstAdd(list, NULL);
            else
                strLstAdd(list, strNewFmt("STR%02d", listIdx));
        }

        // Check pointer
        // -------------------------------------------------------------------------------------------------------------------------
        const char **szList = strLstPtr(list);

        for (unsigned int listIdx = 0; listIdx < strLstSize(list); listIdx++)
        {
            if (listIdx == 0)
            {
                TEST_RESULT_PTR(szList[listIdx], NULL, "check null item");
            }
            else
                TEST_RESULT_STR(szList[listIdx], strPtr(strNewFmt("STR%02u", listIdx)), "check item %u", listIdx);
        }

        strLstFree(list);
    }

    // *****************************************************************************************************************************
    if (testBegin("strLstExists() and strLstExistsZ()"))
    {
        StringList *list = strLstNew();
        strLstAddZ(list, "A");
        strLstAddZ(list, "C");

        TEST_RESULT_BOOL(strLstExists(list, strNew("B")), false, "string does not exist");
        TEST_RESULT_BOOL(strLstExists(list, strNew("C")), true, "string exists");
        TEST_RESULT_BOOL(strLstExistsZ(list, "B"), false, "string does not exist");
        TEST_RESULT_BOOL(strLstExistsZ(list, "C"), true, "string exists");
    }

    // *****************************************************************************************************************************
    if (testBegin("strLstJoin()"))
    {
        StringList *list = strLstNew();

        TEST_RESULT_STR(strPtr(strLstJoin(list, ", ")), "", "empty list");

        strLstAdd(list, strNew("item1"));
        strLstAddZ(list, "item2");

        TEST_RESULT_STR(strPtr(strLstJoin(list, ", ")), "item1, item2", "list");

        strLstAdd(list, NULL);

        TEST_RESULT_STR(strPtr(strLstJoin(list, ", ")), "item1, item2, [NULL]", "list with NULL at end");

        TEST_RESULT_STR(strPtr(strLstJoin(strLstDup(list), ", ")), "item1, item2, [NULL]", "dup'd list will NULL at end");

        strLstFree(list);
    }

    // *****************************************************************************************************************************
    if (testBegin("strLstSort()"))
    {
        StringList *list = strLstNew();

        strLstAddZ(list, "c");
        strLstAddZ(list, "a");
        strLstAddZ(list, "b");

        TEST_RESULT_STR(strPtr(strLstJoin(strLstSort(list, sortOrderAsc), ", ")), "a, b, c", "sort ascending");
        TEST_RESULT_STR(strPtr(strLstJoin(strLstSort(list, sortOrderDesc), ", ")), "c, b, a", "sort descending");
    }

    // *****************************************************************************************************************************
    if (testBegin("strLstToLog()"))
    {
        StringList *list = strLstNew();
        char buffer[STACK_TRACE_PARAM_MAX];

        TEST_RESULT_INT(strLstToLog(NULL, buffer, 4), 4, "format null list with too small buffer");
        TEST_RESULT_STR(buffer, "nul", "    check format");

        TEST_RESULT_INT(strLstToLog(NULL, buffer, STACK_TRACE_PARAM_MAX), 4, "format null list");
        TEST_RESULT_STR(buffer, "null", "    check format");

        TEST_RESULT_INT(strLstToLog(list, buffer, STACK_TRACE_PARAM_MAX), 4, "format empty list");
        TEST_RESULT_STR(buffer, "{[]}", "    check format");

        strLstAddZ(list, "item1");

        TEST_RESULT_INT(strLstToLog(list, buffer, STACK_TRACE_PARAM_MAX), 11, "format 1 item list");
        TEST_RESULT_STR(buffer, "{[\"item1\"]}", "    check format");

        strLstAddZ(list, "item2");
        strLstAddZ(list, "item3");

        TEST_RESULT_INT(strLstToLog(list, buffer, STACK_TRACE_PARAM_MAX), 29, "format 3 item list");
        TEST_RESULT_STR(buffer, "{[\"item1\", \"item2\", \"item3\"]}", "    check format");
    }

    FUNCTION_HARNESS_RESULT_VOID();
}
