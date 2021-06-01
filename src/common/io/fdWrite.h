/***********************************************************************************************************************************
File Descriptor Io Write

Write to a file descriptor using the IoWrite interface.
***********************************************************************************************************************************/
#ifndef COMMON_IO_FDWRITE_H
#define COMMON_IO_FDWRITE_H

#include "common/io/write.h"
#include "common/time.h"

/***********************************************************************************************************************************
Constructors
***********************************************************************************************************************************/
IoWrite *ioFdWriteNew(const String *name, int fd, TimeMSec timeout);

/***********************************************************************************************************************************
Helper functions
***********************************************************************************************************************************/
// Write a string to the specified file descriptor
void ioFdWriteOneStr(int fd, const String *string);

#endif
