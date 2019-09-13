/***********************************************************************************************************************************
List Handler
***********************************************************************************************************************************/
#include "build.auto.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/debug.h"
#include "common/type/list.h"
#include "common/object.h"

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
struct List
{
    MemContext *memContext;
    size_t itemSize;
    unsigned int listSize;
    unsigned int listSizeMax;
    unsigned char *list;
    ListComparator *comparator;
};

OBJECT_DEFINE_MOVE(LIST);
OBJECT_DEFINE_FREE(LIST);

/***********************************************************************************************************************************
Create a new list
***********************************************************************************************************************************/
List *
lstNew(size_t itemSize)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SIZE, itemSize);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RETURN(lstNewP(itemSize, .comparator = lstComparatorStr));
}

List *
lstNewParam(size_t itemSize, ListParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SIZE, itemSize);
        FUNCTION_TEST_PARAM(FUNCTIONP, param.comparator);
    FUNCTION_TEST_END();

    List *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("List")
    {
        // Create object
        this = memNew(sizeof(List));
        this->memContext = MEM_CONTEXT_NEW();
        this->itemSize = itemSize;
        this->comparator = param.comparator;
    }
    MEM_CONTEXT_NEW_END();

    FUNCTION_TEST_RETURN(this);
}

/***********************************************************************************************************************************
Add an item to the end of the list
***********************************************************************************************************************************/
void *
lstAdd(List *this, const void *item)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(LIST, this);
        FUNCTION_TEST_PARAM_P(VOID, item);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(item != NULL);

    FUNCTION_TEST_RETURN(lstInsert(this, lstSize(this), item));
}

/***********************************************************************************************************************************
Clear items from a list
***********************************************************************************************************************************/
List *
lstClear(List *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(LIST, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (this->list != NULL)
    {
        MEM_CONTEXT_BEGIN(this->memContext)
        {
            memFree(this->list);
        }
        MEM_CONTEXT_END();

        this->listSize = 0;
        this->listSizeMax = 0;
    }

    FUNCTION_TEST_RETURN(this);
}

/***********************************************************************************************************************************
Compare Strings or structs with a String as the first member
***********************************************************************************************************************************/
int
lstComparatorStr(const void *item1, const void *item2)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM_P(VOID, item1);
        FUNCTION_TEST_PARAM_P(VOID, item2);
    FUNCTION_TEST_END();

    ASSERT(item1 != NULL);
    ASSERT(item2 != NULL);

    FUNCTION_TEST_RETURN(strCmp(*(String **)item1, *(String **)item2));
}

/***********************************************************************************************************************************
Get an item from the list
***********************************************************************************************************************************/
void *
lstGet(const List *this, unsigned int listIdx)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(LIST, this);
        FUNCTION_TEST_PARAM(UINT, listIdx);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    // Ensure list index is in range
    if (listIdx >= this->listSize)
        THROW_FMT(AssertError, "cannot get index %u from list with %u value(s)", listIdx, this->listSize);

    // Return pointer to list item
    FUNCTION_TEST_RETURN(this->list + (listIdx * this->itemSize));
}

/***********************************************************************************************************************************
Find an item in the list
***********************************************************************************************************************************/
unsigned int
lstFindIdx(const List *this, const void *item)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(LIST, this);
        FUNCTION_TEST_PARAM_P(VOID, item);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(item != NULL);

    unsigned int result = LIST_NOT_FOUND;

    for (unsigned int listIdx = 0; listIdx < lstSize(this); listIdx++)
    {
        if (this->comparator(item, lstGet(this, listIdx)) == 0)
        {
            result = listIdx;
            break;
        }
    }

    FUNCTION_TEST_RETURN(result);
}

void *
lstFindDefault(const List *this, const void *item, void *itemDefault)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(LIST, this);
        FUNCTION_TEST_PARAM_P(VOID, item);
        FUNCTION_TEST_PARAM_P(VOID, itemDefault);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(item != NULL);

    unsigned int listIdx = lstFindIdx(this, item);

    FUNCTION_TEST_RETURN(listIdx == LIST_NOT_FOUND ? itemDefault : lstGet(this, listIdx));
}

void *
lstFind(const List *this, const void *item)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(LIST, this);
        FUNCTION_TEST_PARAM_P(VOID, item);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(item != NULL);

    FUNCTION_TEST_RETURN(lstFindDefault(this, item, NULL));
}

/***********************************************************************************************************************************
Insert an item into the list
***********************************************************************************************************************************/
void *
lstInsert(List *this, unsigned int listIdx, const void *item)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(LIST, this);
        FUNCTION_TEST_PARAM_P(VOID, item);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(listIdx <= lstSize(this));
    ASSERT(item != NULL);

    // If list size = max then allocate more space
    if (this->listSize == this->listSizeMax)
    {
        MEM_CONTEXT_BEGIN(this->memContext)
        {
            // If nothing has been allocated yet
            if (this->listSizeMax == 0)
            {
                this->listSizeMax = LIST_INITIAL_SIZE;
                this->list = memNewRaw(this->listSizeMax * this->itemSize);
            }
            // Else the list needs to be extended
            else
            {
                this->listSizeMax *= 2;
                this->list = memGrowRaw(this->list, this->listSizeMax * this->itemSize);
            }
        }
        MEM_CONTEXT_END();
    }

    // If not inserting at the end then move items down to make space
    void *itemPtr = this->list + (listIdx * this->itemSize);

    if (listIdx != lstSize(this))
        memmove(this->list + ((listIdx + 1) * this->itemSize), itemPtr, (lstSize(this) - listIdx) * this->itemSize);

    // Copy item into the list
    memcpy(itemPtr, item, this->itemSize);
    this->listSize++;

    FUNCTION_TEST_RETURN(itemPtr);
}

/***********************************************************************************************************************************
Remove an item from the list
***********************************************************************************************************************************/
List *
lstRemoveIdx(List *this, unsigned int listIdx)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(LIST, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(listIdx <= lstSize(this));

    // Remove the item by moving the items after it down
    this->listSize--;

    memmove(
        this->list + (listIdx * this->itemSize), this->list + ((listIdx + 1) * this->itemSize),
        (lstSize(this) - listIdx) * this->itemSize);

    FUNCTION_TEST_RETURN(this);
}

bool
lstRemove(List *this, const void *item)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(LIST, this);
        FUNCTION_TEST_PARAM_P(VOID, item);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(item != NULL);

    unsigned int listIdx = lstFindIdx(this, item);

    if (listIdx != LIST_NOT_FOUND)
    {
        lstRemoveIdx(this, listIdx);
        FUNCTION_TEST_RETURN(true);
    }

    FUNCTION_TEST_RETURN(false);
}

/***********************************************************************************************************************************
Return the memory context for this list
***********************************************************************************************************************************/
MemContext *
lstMemContext(const List *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(LIST, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->memContext);
}

/***********************************************************************************************************************************
Return list size
***********************************************************************************************************************************/
unsigned int
lstSize(const List *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(LIST, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->listSize);
}

/***********************************************************************************************************************************
List sort
***********************************************************************************************************************************/
static List *lstSortList = NULL;

static int
lstSortDescComparator(const void *item1, const void *item2)
{
    return lstSortList->comparator(item2, item1);
}

List *
lstSort(List *this, SortOrder sortOrder)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(LIST, this);
        FUNCTION_TEST_PARAM(ENUM, sortOrder);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    switch (sortOrder)
    {
        case sortOrderAsc:
        {
            qsort(this->list, this->listSize, this->itemSize, this->comparator);
            break;
        }

        case sortOrderDesc:
        {
            // Assign the list that will be sorted for the comparator function to use
            lstSortList = this;

            qsort(this->list, this->listSize, this->itemSize, lstSortDescComparator);
            break;
        }

        case sortOrderNone:
            break;
    }

    FUNCTION_TEST_RETURN(this);
}

/***********************************************************************************************************************************
Set a new comparator
***********************************************************************************************************************************/
List *
lstComparatorSet(List *this, ListComparator *comparator)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(LIST, this);
        FUNCTION_TEST_PARAM(FUNCTIONP, comparator);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    this->comparator = comparator;

    FUNCTION_TEST_RETURN(this);
}

/***********************************************************************************************************************************
Render as string for logging
***********************************************************************************************************************************/
String *
lstToLog(const List *this)
{
    return strNewFmt("{size: %u}", this->listSize);
}
