/***********************************************************************************************************************************
IO Read Interface Internal
***********************************************************************************************************************************/
#ifndef COMMON_IO_READ_INTERN_H
#define COMMON_IO_READ_INTERN_H

#include "common/io/read.h"

/***********************************************************************************************************************************
Constructor
***********************************************************************************************************************************/
typedef bool (*IoReadInterfaceEof)(void *driver);
typedef void (*IoReadInterfaceClose)(void *driver);
typedef bool (*IoReadInterfaceOpen)(void *driver);
typedef size_t (*IoReadInterfaceRead)(void *driver, Buffer *buffer);

typedef struct IoReadInterface
{
    IoReadInterfaceEof eof;
    IoReadInterfaceClose close;
    IoReadInterfaceOpen open;
    IoReadInterfaceRead read;
} IoReadInterface;

#define ioReadNewP(driver, ...)                                                                                                    \
    ioReadNew(driver, (IoReadInterface){__VA_ARGS__})

IoRead *ioReadNew(void *driver, IoReadInterface interface);

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
#define FUNCTION_DEBUG_IO_READ_INTERFACE_TYPE                                                                                      \
    IoReadInterface
#define FUNCTION_DEBUG_IO_READ_INTERFACE_FORMAT(value, buffer, bufferSize)                                                         \
    objToLog(&value, "IoReadInterface", buffer, bufferSize)

#endif
