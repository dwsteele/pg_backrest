/***********************************************************************************************************************************
Test Lists
***********************************************************************************************************************************/

/***********************************************************************************************************************************
Test sort comparator
***********************************************************************************************************************************/
static int
testComparator(const void *item1, const void *item2)
{
    int int1 = *(int *)item1;
    int int2 = *(int *)item2;

    if (int1 < int2)
        return -1;

    if (int1 > int2)
        return 1;

    return 0;
}

/***********************************************************************************************************************************
Test Run
***********************************************************************************************************************************/
void
testRun(void)
{
    FUNCTION_HARNESS_VOID();

    // *****************************************************************************************************************************
    if (testBegin("lstNew(), lstMemContext(), and lstFree()"))
    {
        List *list = lstNew(sizeof(void *));

        TEST_RESULT_INT(list->itemSize, sizeof(void *), "item size");
        TEST_RESULT_INT(list->listSize, 0, "list size");
        TEST_RESULT_INT(list->listSizeMax, 0, "list size max");
        TEST_RESULT_PTR(lstMemContext(list), list->memContext, "list mem context");

        void *ptr = NULL;
        TEST_RESULT_PTR(lstAdd(list, &ptr), list, "add item");

        TEST_RESULT_VOID(lstFree(list), "free list");
        TEST_RESULT_VOID(lstFree(lstNew(1)), "free empty list");
        TEST_RESULT_VOID(lstFree(NULL), "free null list");
    }

    // *****************************************************************************************************************************
    if (testBegin("lstAdd(), lstMove(), and lstSize()"))
    {
        List *list = NULL;

        MEM_CONTEXT_TEMP_BEGIN()
        {
            list = lstNew(sizeof(int));

            // Add ints to the list
            for (int listIdx = 0; listIdx <= LIST_INITIAL_SIZE; listIdx++)
                TEST_RESULT_PTR(lstAdd(list, &listIdx), list, "add item %d", listIdx);

            lstMove(list, MEM_CONTEXT_OLD());
        }
        MEM_CONTEXT_TEMP_END();

        TEST_RESULT_INT(lstSize(list), 9, "list size");

        // Read them back and check values
        for (unsigned int listIdx = 0; listIdx < lstSize(list); listIdx++)
        {
            int *item = lstGet(list, listIdx);
            TEST_RESULT_INT(*item, listIdx, "check item %u", listIdx);
        }

        TEST_ERROR(lstGet(list, lstSize(list)), AssertError, "cannot get index 9 from list with 9 value(s)");
        TEST_RESULT_VOID(lstMove(NULL, memContextTop()), "move null list");
    }

    // *****************************************************************************************************************************
    if (testBegin("lstSort"))
    {
        List *list = lstNew(sizeof(int));
        int value;

        value = 3; lstAdd(list, &value);
        value = 5; lstAdd(list, &value);
        value = 3; lstAdd(list, &value);
        value = 2; lstAdd(list, &value);

        TEST_RESULT_PTR(lstSort(list, testComparator), list, "list sort");

        TEST_RESULT_INT(*((int *)lstGet(list, 0)), 2, "sort value 0");
        TEST_RESULT_INT(*((int *)lstGet(list, 1)), 3, "sort value 1");
        TEST_RESULT_INT(*((int *)lstGet(list, 2)), 3, "sort value 2");
        TEST_RESULT_INT(*((int *)lstGet(list, 3)), 5, "sort value 3");
    }

    FUNCTION_HARNESS_RESULT_VOID();
}
