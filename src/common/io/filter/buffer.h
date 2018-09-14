/***********************************************************************************************************************************
IO Buffer Filter

Move data from the input buffer to the output buffer without overflowing the output buffer.  Automatically used as the last filter
in a FilterGroup if the last filter is not already an InOut filter, so there is no reason to add it manually to a FilterGroup.
***********************************************************************************************************************************/
#ifndef COMMON_IO_FILTER_BUFFER_H
#define COMMON_IO_FILTER_BUFFER_H

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
typedef struct IoBuffer IoBuffer;

#include "common/io/filter/filter.h"
#include "common/type/buffer.h"

/***********************************************************************************************************************************
Constructor
***********************************************************************************************************************************/
IoBuffer *ioBufferNew(void);

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
void ioBufferProcess(IoBuffer *this, const Buffer *input, Buffer *output);

/***********************************************************************************************************************************
Getters
***********************************************************************************************************************************/
IoFilter *ioBufferFilter(const IoBuffer *this);
bool ioBufferInputSame(const IoBuffer *this);

/***********************************************************************************************************************************
Destructor
***********************************************************************************************************************************/
void ioBufferFree(IoBuffer *this);

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
String *ioBufferToLog(const IoBuffer *this);

#define FUNCTION_DEBUG_IO_BUFFER_TYPE                                                                                              \
    IoBuffer *
#define FUNCTION_DEBUG_IO_BUFFER_FORMAT(value, buffer, bufferSize)                                                                 \
    FUNCTION_DEBUG_STRING_OBJECT_FORMAT(value, ioBufferToLog, buffer, bufferSize)

#endif
