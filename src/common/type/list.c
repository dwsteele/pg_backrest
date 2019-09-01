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
};

OBJECT_DEFINE_MOVE(LIST);
OBJECT_DEFINE_FREE(LIST);

/***********************************************************************************************************************************
Create a new list
***********************************************************************************************************************************/
List *
lstNew(size_t itemSize)
{
    FUNCTION_TEST_VOID();

    List *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("List")
    {
        // Create object
        this = memNew(sizeof(List));
        this->memContext = MEM_CONTEXT_NEW();
        this->itemSize = itemSize;
    }
    MEM_CONTEXT_NEW_END();

    FUNCTION_TEST_RETURN(this);
}

/***********************************************************************************************************************************
Add an item to the end of the list
***********************************************************************************************************************************/
List *
lstAdd(List *this, const void *item)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(LIST, this);
        FUNCTION_TEST_PARAM_P(VOID, item);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(item != NULL);

    lstInsert(this, lstSize(this), item);

    FUNCTION_TEST_RETURN(this);
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
Insert an item into the list
***********************************************************************************************************************************/
List *
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
    if (listIdx != lstSize(this))
    {
        memmove(
            this->list + ((listIdx + 1) * this->itemSize), this->list + (listIdx * this->itemSize),
            (lstSize(this) - listIdx) * this->itemSize);
    }

    // Copy item into the list
    memcpy(this->list + (listIdx * this->itemSize), item, this->itemSize);
    this->listSize++;

    FUNCTION_TEST_RETURN(this);
}

/***********************************************************************************************************************************
Remove an item from the list
***********************************************************************************************************************************/
List *
lstRemove(List *this, unsigned int listIdx)
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
List *
lstSort(List *this, int (*comparator)(const void *, const void*))
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(LIST, this);
        FUNCTION_TEST_PARAM(FUNCTIONP, comparator);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(comparator != NULL);

    qsort(this->list, this->listSize, this->itemSize, comparator);

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
