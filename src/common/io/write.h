/***********************************************************************************************************************************
IO Write Interface

Objects that write to some IO destination (file, socket, etc.) are implemented using this interface.  All objects are required to
implement IoWriteProcess and can optionally implement IoWriteOpen or IoWriteClose.  IoWriteOpen and IoWriteClose can be used to
allocate/open or deallocate/free resources.  An example of an IoWrite object is IoBufferWrite.
***********************************************************************************************************************************/
#ifndef COMMON_IO_WRITE_H
#define COMMON_IO_WRITE_H

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
#define IO_WRITE_TYPE                                               IoWrite
#define IO_WRITE_PREFIX                                             ioWrite

typedef struct IoWrite IoWrite;

#include "common/io/filter/group.h"
#include "common/type/buffer.h"

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
// Open the IO
void ioWriteOpen(IoWrite *this);

// Write data to IO and process filters
void ioWrite(IoWrite *this, const Buffer *buffer);

// Write linefeed-terminated buffer
void ioWriteLine(IoWrite *this, const Buffer *buffer);

// Write string
void ioWriteStr(IoWrite *this, const String *string);

// Write linefeed-terminated string
void ioWriteStrLine(IoWrite *this, const String *string);

// Flush any data in the output buffer. This does not end writing and will not work if filters are present.
void ioWriteFlush(IoWrite *this);

// Close the IO and write any additional data that has not been written yet
void ioWriteClose(IoWrite *this);

/***********************************************************************************************************************************
Getters/Setters
***********************************************************************************************************************************/
// Filter group. Filters must be set before open and cannot be reset
IoFilterGroup *ioWriteFilterGroup(const IoWrite *this);

// Handle (file descriptor) for the write object. Not all write objects have a handle and -1 will be returned in that case.
int ioWriteHandle(const IoWrite *this);

/***********************************************************************************************************************************
Destructor
***********************************************************************************************************************************/
void ioWriteFree(IoWrite *this);

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
#define FUNCTION_LOG_IO_WRITE_TYPE                                                                                                 \
    IoWrite *
#define FUNCTION_LOG_IO_WRITE_FORMAT(value, buffer, bufferSize)                                                                    \
    objToLog(value, "IoWrite", buffer, bufferSize)

#endif
