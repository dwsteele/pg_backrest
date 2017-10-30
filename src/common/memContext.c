/***********************************************************************************************************************************
Memory Context Manager
***********************************************************************************************************************************/
#include <stdlib.h>
#include <string.h>

#include "common/errorType.h"
#include "common/memContext.h"

/***********************************************************************************************************************************
Memory context states
***********************************************************************************************************************************/
typedef enum {memContextStateFree = 0, memContextStateFreeing, memContextStateActive} MemContextState;

/***********************************************************************************************************************************
Contains information about the memory context
***********************************************************************************************************************************/
struct MemContext
{
    MemContextState state;                                          // Current state of the context
    const char name[MEM_CONTEXT_NAME_SIZE + 1];                     // Indicates what the context is being used for

    MemContext *contextParent;                                      // All contexts have a parent except top

    MemContext **contextChildList;                                  // List of contexts created in this context
    int contextChildListSize;                                       // Size of child context list (not the actual count of contexts)

    void **allocList;                                               // List of memory allocations created in this context
    int allocListSize;                                              // Size of alloc list (not the actual count of allocations)

    MemContextCallback callbackFunction;                            // Function to call before the context is freed
    void *callbackArgument;                                         // Argument to pass to callback function
};

/***********************************************************************************************************************************
Top context

The top context always exists and can never be freed.  All other contexts are children of the top context. The top context is
generally used to allocate memory that exists for the life of the program.
***********************************************************************************************************************************/
MemContext contextTop = {.state = memContextStateActive, .name = "TOP"};

/***********************************************************************************************************************************
Current context

All memory allocations will be done from the current context.  Initialized to top context at execution start.
***********************************************************************************************************************************/
MemContext *contextCurrent = &contextTop;

/***********************************************************************************************************************************
Wrapper around malloc()
***********************************************************************************************************************************/
static void *memAllocInternal(size_t size, bool zero)
{
    // Allocate memory
    void *buffer = malloc(size);

    // Error when malloc fails
    if (!buffer)
        ERROR_THROW(MemoryError, "unable to allocate %lu bytes", size);

    // Zero the memory when requested
    if (zero)
        memset(buffer, 0, size);

    // Return the buffer
    return buffer;
}

/***********************************************************************************************************************************
Wrapper around realloc()
***********************************************************************************************************************************/
static void *memReAllocInternal(void *bufferOld, size_t sizeOld, size_t sizeNew, bool zeroNew)
{
    // Allocate memory
    void *bufferNew = realloc(bufferOld, sizeNew);

    // Error when realloc fails
    if(!bufferNew)
        ERROR_THROW(MemoryError, "unable to reallocate %lu bytes", sizeNew);

    // Zero the new memory when requested - old memory is left untouched else why bother with a realloc?
    if (zeroNew)
        memset(bufferNew + sizeOld, 0, sizeNew - sizeOld);

    // Return the buffer
    return bufferNew;
}

/***********************************************************************************************************************************
Wrapper around free()
***********************************************************************************************************************************/
static void memFreeInternal(void *buffer)
{
    // Error if pointer is null
    if(!buffer)
        ERROR_THROW(MemoryError, "unable to free null pointer");

    // Free the buffer
    free(buffer);
}

/***********************************************************************************************************************************
Create a new memory context
***********************************************************************************************************************************/
MemContext *
memContextNew(const char *name)
{
    // Check context name length
    if (strlen(name) == 0 || strlen(name) > MEM_CONTEXT_NAME_SIZE)
        ERROR_THROW(AssertError, "context name length must be > 0 and <= %d", MEM_CONTEXT_NAME_SIZE);

    // Try to find space for the new context
    int contextIdx;

    for (contextIdx = 0; contextIdx < memContextCurrent()->contextChildListSize; contextIdx++)
        if (!memContextCurrent()->contextChildList[contextIdx] ||
            memContextCurrent()->contextChildList[contextIdx]->state == memContextStateFree)
        {
            break;
        }

    // If no space was found then allocate more
    if (contextIdx == memContextCurrent()->contextChildListSize)
    {
        // If no space has been allocated to the list
        if (memContextCurrent()->contextChildListSize == 0)
        {
            // Allocate memory before modifying anything else in case there is an error
            memContextCurrent()->contextChildList = memAllocInternal(sizeof(MemContext *) * MEM_CONTEXT_INITIAL_SIZE, true);

            // Set new list size
            memContextCurrent()->contextChildListSize = MEM_CONTEXT_INITIAL_SIZE;
        }
        // Else grow the list
        else
        {
            // Calculate new list size
            int contextChildListSizeNew = memContextCurrent()->contextChildListSize * 2;

            // ReAllocate memory before modifying anything else in case there is an error
            memContextCurrent()->contextChildList = memReAllocInternal(
                memContextCurrent()->contextChildList, sizeof(MemContext *) * memContextCurrent()->contextChildListSize,
                sizeof(MemContext *) * contextChildListSizeNew, true);

            // Set new list size
            memContextCurrent()->contextChildListSize = contextChildListSizeNew;
        }
    }

    // If the context has not been allocated yet
    if (!memContextCurrent()->contextChildList[contextIdx])
        memContextCurrent()->contextChildList[contextIdx] = memAllocInternal(sizeof(MemContext), true);

    // Get the context
    MemContext *this = memContextCurrent()->contextChildList[contextIdx];

    // Create initial space for allocations
    this->allocList = memAllocInternal(sizeof(void *) * MEM_CONTEXT_ALLOC_INITIAL_SIZE, true);
    this->allocListSize = MEM_CONTEXT_ALLOC_INITIAL_SIZE;

    // Set the context name
    strcpy((char *)this->name, name);

    // Set new context active
    this->state = memContextStateActive;

    // Set current context as the parent
    this->contextParent = memContextCurrent();

    // Return context
    return this;
}

/***********************************************************************************************************************************
Register a callback to be called just before the context is freed
***********************************************************************************************************************************/
void
memContextCallback(MemContext *this, void (*callbackFunction)(void *), void *callbackArgument)
{
    // Error if context is not active
    if (this->state != memContextStateActive)
        ERROR_THROW(AssertError, "cannot assign callback to inactive context");

    // Top context cannot have a callback
    if (this == memContextTop())
        ERROR_THROW(AssertError, "top context may not have a callback");

    // Error if callback has already been set - there may be valid use cases for this but error until one is found
    if (this->callbackFunction)
        ERROR_THROW(AssertError, "callback is already set for context '%s'", this->name);

    // Set callback function and argument
    this->callbackFunction = callbackFunction;
    this->callbackArgument = callbackArgument;
}

/***********************************************************************************************************************************
Allocate memory in the memory context and optionally zero it.
***********************************************************************************************************************************/
static void *
memContextAlloc(size_t size, bool zero)
{
    // Find space for the new allocation
    int allocIdx;

    for (allocIdx = 0; allocIdx < memContextCurrent()->allocListSize; allocIdx++)
        if (!memContextCurrent()->allocList[allocIdx])
            break;

    // If no space was found then allocate more
    if (allocIdx == memContextCurrent()->allocListSize)
    {
        // Only the top context will not have initial space for allocations
        if (memContextCurrent()->allocListSize == 0)
        {
            // Allocate memory before modifying anything else in case there is an error
            memContextCurrent()->allocList = memAllocInternal(sizeof(void *) * MEM_CONTEXT_ALLOC_INITIAL_SIZE, true);

            // Set new size
            memContextCurrent()->allocListSize = MEM_CONTEXT_ALLOC_INITIAL_SIZE;
        }
        // Else grow the list
        else
        {
            // Calculate new list size
            int allocListSizeNew = memContextCurrent()->allocListSize * 2;

            // ReAllocate memory before modifying anything else in case there is an error
            memContextCurrent()->allocList = memReAllocInternal(
                memContextCurrent()->allocList, sizeof(void *) * memContextCurrent()->contextChildListSize,
                sizeof(MemContext *) * allocListSizeNew, true);

            // Set new size
            memContextCurrent()->allocListSize = allocListSizeNew;
        }
    }

    // Allocate the memory
    memContextCurrent()->allocList[allocIdx] = memAllocInternal(size, zero);

    // Return buffer
    return memContextCurrent()->allocList[allocIdx];
}

/***********************************************************************************************************************************
Allocate zeroed memory in the memory context
***********************************************************************************************************************************/
void *
memNew(size_t size)
{
    return memContextAlloc(size, true);
}

/***********************************************************************************************************************************
Allocate memory in the memory context without initializing it
***********************************************************************************************************************************/
void *
memNewRaw(size_t size)
{
    return memContextAlloc(size, false);
}

/***********************************************************************************************************************************
Free a memory allocation in the context
***********************************************************************************************************************************/
void
memFree(void *buffer)
{
    // Error if buffer is null
    if (!buffer)
        ERROR_THROW(AssertError, "unable to free null allocation");

    // Find memory to free
    int allocIdx;

    for (allocIdx = 0; allocIdx < memContextCurrent()->allocListSize; allocIdx++)
        if (memContextCurrent()->allocList[allocIdx] == buffer)
            break;

    // Error if the buffer was not found
    if (allocIdx == memContextCurrent()->allocListSize)
        ERROR_THROW(AssertError, "unable to find allocation");

    // Free the buffer
    memFreeInternal(memContextCurrent()->allocList[allocIdx]);
    memContextCurrent()->allocList[allocIdx] = NULL;
}

/***********************************************************************************************************************************
Switch to the specified context and return the old context
***********************************************************************************************************************************/
MemContext *
memContextSwitch(MemContext *this)
{
    // Error if context is not active
    if (this->state != memContextStateActive)
        ERROR_THROW(AssertError, "cannot switch to inactive context");

    MemContext *memContextOld = memContextCurrent();
    contextCurrent = this;

    return memContextOld;
}

/***********************************************************************************************************************************
Return the top context
***********************************************************************************************************************************/
MemContext *
memContextTop()
{
    return &contextTop;
}

/***********************************************************************************************************************************
Return the current context
***********************************************************************************************************************************/
MemContext *
memContextCurrent()
{
    return contextCurrent;
}

/***********************************************************************************************************************************
Return the context name
***********************************************************************************************************************************/
const char *
memContextName(MemContext *this)
{
    // Error if context is not active
    if (this->state != memContextStateActive)
        ERROR_THROW(AssertError, "cannot get name for inactive context");

    return this->name;
}

/***********************************************************************************************************************************
memContextFree - free all memory used by the context and all child contexts
***********************************************************************************************************************************/
void
memContextFree(MemContext *this)
{
    // If context is already freeing then return if memContextFree() is called again - this can happen in callbacks
    if (this->state == memContextStateFreeing)
        return;

    // Top context cannot be freed
    if (this == memContextTop())
        ERROR_THROW(AssertError, "cannot free top context");

    // Current context cannot be freed
    if (this == memContextCurrent())
        ERROR_THROW(AssertError, "cannot free current context '%s'", this->name);

    // Error if context is not active
    if (this->state != memContextStateActive)
        ERROR_THROW(AssertError, "cannot free inactive context");

    // Free child contexts
    if (this->contextChildListSize > 0)
        for (int contextIdx = 0; contextIdx < this->contextChildListSize; contextIdx++)
            if (this->contextChildList[contextIdx] && this->contextChildList[contextIdx]->state == memContextStateActive)
                memContextFree(this->contextChildList[contextIdx]);

    // Set state to freeing now that there are no child contexts.  Child contexts might need to interact with their parent while
    // freeing so the parent needs to remain active until they are all gone.
    this->state = memContextStateFreeing;

    // Execute callback if defined
    if (this->callbackFunction)
        this->callbackFunction(this->callbackArgument);

    // Free child context allocations
    if (this->contextChildListSize > 0)
    {
        for (int contextIdx = 0; contextIdx < this->contextChildListSize; contextIdx++)
            if (this->contextChildList[contextIdx])
                memFreeInternal(this->contextChildList[contextIdx]);

        memFreeInternal(this->contextChildList);
    }

    // Free memory allocations
    if (this->allocListSize > 0)
    {
        for (int allocIdx = 0; allocIdx < this->allocListSize; allocIdx++)
            if (this->allocList[allocIdx])
                memFreeInternal(this->allocList[allocIdx]);

        memFreeInternal(this->allocList);
    }

    // Reset the memory context so it can be used again
    memset(this, 0, sizeof(MemContext));
}
