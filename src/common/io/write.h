/***********************************************************************************************************************************
IO Write Interface

Objects that write to some IO destination (file, socket, etc.) are implemented using this interface.  All objects are required to
implement IoWriteProcess and can optionally implement IoWriteOpen or IoWriteClose.  IoWriteOpen and IoWriteClose can be used to
allocate/open or deallocate/free resources.  An example of an IoWrite object is IoBufferRead.
***********************************************************************************************************************************/
#ifndef COMMON_IO_WRITE_H
#define COMMON_IO_WRITE_H

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
typedef struct IoWrite IoWrite;

#include "common/io/filter/group.h"
#include "common/type/buffer.h"

/***********************************************************************************************************************************
Function pointer types
***********************************************************************************************************************************/
typedef void (*IoWriteOpen)(void *driver);
typedef void (*IoWriteProcess)(void *driver, const Buffer *buffer);
typedef void (*IoWriteClose)(void *driver);

/***********************************************************************************************************************************
Constructor
***********************************************************************************************************************************/
IoWrite *ioWriteNew(void *driver, IoWriteOpen open, IoWriteProcess write, IoWriteClose close);

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
void ioWriteOpen(IoWrite *this);
void ioWrite(IoWrite *this, const Buffer *buffer);
void ioWriteClose(IoWrite *this);

/***********************************************************************************************************************************
Getters/Setters
***********************************************************************************************************************************/
const IoFilterGroup *ioWriteFilterGroup(const IoWrite *this);
void ioWriteFilterGroupSet(IoWrite *this, IoFilterGroup *filterGroup);

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
#define FUNCTION_DEBUG_IO_WRITE_TYPE                                                                                               \
    IoWrite *
#define FUNCTION_DEBUG_IO_WRITE_FORMAT(value, buffer, bufferSize)                                                                  \
    objToLog(value, "IoWrite", buffer, bufferSize)

#endif
