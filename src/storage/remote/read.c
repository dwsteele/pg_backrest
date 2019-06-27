/***********************************************************************************************************************************
Remote Storage Read
***********************************************************************************************************************************/
#include "build.auto.h"

#include <fcntl.h>
#include <unistd.h>

#include "common/compress/gzip/compress.h"
#include "common/compress/gzip/decompress.h"
#include "common/debug.h"
#include "common/io/read.intern.h"
#include "common/log.h"
#include "common/memContext.h"
#include "common/object.h"
#include "common/type/convert.h"
#include "storage/remote/protocol.h"
#include "storage/remote/read.h"
#include "storage/read.intern.h"

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
typedef struct StorageReadRemote
{
    MemContext *memContext;                                         // Object mem context
    StorageReadInterface interface;                                 // Interface
    StorageRemote *storage;                                         // Storage that created this object
    StorageRead *read;                                              // Storage read interface

    ProtocolClient *client;                                         // Protocol client for requests
    size_t remaining;                                               // Bytes remaining to be read in block
    bool eof;                                                       // Has the file reached eof?

#ifdef DEBUG
    uint64_t protocolReadBytes;                                     // How many bytes were read from the protocol layer?
#endif
} StorageReadRemote;

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
#define FUNCTION_LOG_STORAGE_READ_REMOTE_TYPE                                                                                      \
    StorageReadRemote *
#define FUNCTION_LOG_STORAGE_READ_REMOTE_FORMAT(value, buffer, bufferSize)                                                         \
    objToLog(value, "StorageReadRemote", buffer, bufferSize)

/***********************************************************************************************************************************
Open the file
***********************************************************************************************************************************/
static bool
storageReadRemoteOpen(THIS_VOID)
{
    THIS(StorageReadRemote);

    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(STORAGE_READ_REMOTE, this);
    FUNCTION_LOG_END();

    ASSERT(this != NULL);

    bool result = false;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        IoFilterGroup *filterGroup = ioFilterGroupNew();

        // If the file is compressible add compression filter on the remote
        if (this->interface.compressible)
            ioFilterGroupAdd(filterGroup, gzipCompressNew((int)this->interface.compressLevel, true));

        ProtocolCommand *command = protocolCommandNew(PROTOCOL_COMMAND_STORAGE_OPEN_READ_STR);
        protocolCommandParamAdd(command, VARSTR(this->interface.name));
        protocolCommandParamAdd(command, VARBOOL(this->interface.ignoreMissing));
        protocolCommandParamAdd(command, ioFilterGroupParamAll(filterGroup));

        // If the file is compressible add decompression filter locally
        if (this->interface.compressible)
        {
            // Since we can't insert filters yet we'll just error if there are already filters in the list
            CHECK(ioFilterGroupSize(ioReadFilterGroup(storageReadIo(this->read))) == 0);

            ioFilterGroupAdd(ioReadFilterGroup(storageReadIo(this->read)), gzipDecompressNew(true));
        }

        result = varBool(protocolClientExecute(this->client, command, true));
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(BOOL, result);
}

/***********************************************************************************************************************************
Read from a file
***********************************************************************************************************************************/
static size_t
storageReadRemote(THIS_VOID, Buffer *buffer, bool block)
{
    THIS(StorageReadRemote);

    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(STORAGE_READ_REMOTE, this);
        FUNCTION_LOG_PARAM(BUFFER, buffer);
        FUNCTION_LOG_PARAM(BOOL, block);
    FUNCTION_LOG_END();

    ASSERT(this != NULL);
    ASSERT(buffer != NULL && !bufFull(buffer));

    size_t result = 0;

    // Read if eof has not been reached
    if (!this->eof)
    {
        do
        {
            // If no bytes remaining then read a new block
            if (this->remaining == 0)
            {
                MEM_CONTEXT_TEMP_BEGIN()
                {
                    this->remaining = (size_t)storageRemoteProtocolBlockSize(ioReadLine(protocolClientIoRead(this->client)));

                    if (this->remaining == 0)
                        this->eof = true;

#ifdef DEBUG
                    this->protocolReadBytes += this->remaining;
#endif
                }
                MEM_CONTEXT_TEMP_END();
            }

            // Read if not eof
            if (!this->eof)
            {
                // If the buffer can contain all remaining bytes
                if (bufRemains(buffer) >= this->remaining)
                {
                    bufLimitSet(buffer, bufUsed(buffer) + this->remaining);
                    ioRead(protocolClientIoRead(this->client), buffer);
                    bufLimitClear(buffer);
                    this->remaining = 0;
                }
                // Else read what we can
                else
                    this->remaining -= ioRead(protocolClientIoRead(this->client), buffer);
            }
        }
        while (!this->eof && !bufFull(buffer));
    }

    FUNCTION_LOG_RETURN(SIZE, result);
}

/***********************************************************************************************************************************
Has file reached EOF?
***********************************************************************************************************************************/
static bool
storageReadRemoteEof(THIS_VOID)
{
    THIS(StorageReadRemote);

    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_READ_REMOTE, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->eof);
}

/***********************************************************************************************************************************
New object
***********************************************************************************************************************************/
StorageRead *
storageReadRemoteNew(
    StorageRemote *storage, ProtocolClient *client, const String *name, bool ignoreMissing, bool compressible,
    unsigned int compressLevel)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(STORAGE_REMOTE, storage);
        FUNCTION_LOG_PARAM(PROTOCOL_CLIENT, client);
        FUNCTION_LOG_PARAM(STRING, name);
        FUNCTION_LOG_PARAM(BOOL, ignoreMissing);
        FUNCTION_LOG_PARAM(BOOL, compressible);
        FUNCTION_LOG_PARAM(UINT, compressLevel);
    FUNCTION_LOG_END();

    ASSERT(storage != NULL);
    ASSERT(client != NULL);
    ASSERT(name != NULL);

    StorageReadRemote *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("StorageReadRemote")
    {
        this = memNew(sizeof(StorageReadRemote));
        this->memContext = MEM_CONTEXT_NEW();

        this->interface = (StorageReadInterface)
        {
            .type = STORAGE_REMOTE_TYPE_STR,
            .name = strDup(name),
            .compressible = compressible,
            .compressLevel = compressLevel,
            .ignoreMissing = ignoreMissing,

            .ioInterface = (IoReadInterface)
            {
                .eof = storageReadRemoteEof,
                .open = storageReadRemoteOpen,
                .read = storageReadRemote,
            },
        };

        this->storage = storage;
        this->client = client;

        this->read = storageReadNew(this, &this->interface);
    }
    MEM_CONTEXT_NEW_END();

    ASSERT(this != NULL);
    FUNCTION_LOG_RETURN(STORAGE_READ, this->read);
}
