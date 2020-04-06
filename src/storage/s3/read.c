/***********************************************************************************************************************************
S3 Storage Read
***********************************************************************************************************************************/
#include "build.auto.h"

#include <fcntl.h>
#include <unistd.h>

#include "common/debug.h"
#include "common/io/http/client.h"
#include "common/io/read.intern.h"
#include "common/log.h"
#include "common/memContext.h"
#include "common/type/object.h"
#include "storage/s3/read.h"
#include "storage/read.intern.h"

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
#define STORAGE_READ_S3_TYPE                                        StorageReadS3
#define STORAGE_READ_S3_PREFIX                                      storageReadS3

typedef struct StorageReadS3
{
    MemContext *memContext;                                         // Object mem context
    StorageReadInterface interface;                                 // Interface
    StorageS3 *storage;                                             // Storage that created this object

    HttpClient *httpClient;                                         // Http client for requests
} StorageReadS3;

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
#define FUNCTION_LOG_STORAGE_READ_S3_TYPE                                                                                          \
    StorageReadS3 *
#define FUNCTION_LOG_STORAGE_READ_S3_FORMAT(value, buffer, bufferSize)                                                             \
    objToLog(value, "StorageReadS3", buffer, bufferSize)

/***********************************************************************************************************************************
Mark http client as done so it can be reused
***********************************************************************************************************************************/
OBJECT_DEFINE_FREE_RESOURCE_BEGIN(STORAGE_READ_S3, LOG, logLevelTrace)
{
    httpClientDone(this->httpClient);
}
OBJECT_DEFINE_FREE_RESOURCE_END(LOG);

/***********************************************************************************************************************************
Open the file
***********************************************************************************************************************************/
static bool
storageReadS3Open(THIS_VOID)
{
    THIS(StorageReadS3);

    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(STORAGE_READ_S3, this);
    FUNCTION_LOG_END();

    ASSERT(this != NULL);
    ASSERT(this->httpClient == NULL);

    bool result = false;

    // Request the file
    this->httpClient = storageS3Request(this->storage, HTTP_VERB_GET_STR, this->interface.name, NULL, NULL, false, true).httpClient;

    if (httpClientResponseCodeOk(this->httpClient))
    {
        memContextCallbackSet(this->memContext, storageReadS3FreeResource, this);
        result = true;
    }
    // Else error unless ignore missing
    else if (!this->interface.ignoreMissing)
        THROW_FMT(FileMissingError, "unable to open '%s': No such file or directory", strPtr(this->interface.name));

    FUNCTION_LOG_RETURN(BOOL, result);
}

/***********************************************************************************************************************************
Read from a file
***********************************************************************************************************************************/
static size_t
storageReadS3(THIS_VOID, Buffer *buffer, bool block)
{
    THIS(StorageReadS3);

    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(STORAGE_READ_S3, this);
        FUNCTION_LOG_PARAM(BUFFER, buffer);
        FUNCTION_LOG_PARAM(BOOL, block);
    FUNCTION_LOG_END();

    ASSERT(this != NULL && this->httpClient != NULL);
    ASSERT(httpClientIoRead(this->httpClient) != NULL);
    ASSERT(buffer != NULL && !bufFull(buffer));

    FUNCTION_LOG_RETURN(SIZE, ioRead(httpClientIoRead(this->httpClient), buffer));
}

/***********************************************************************************************************************************
Close the file
***********************************************************************************************************************************/
static void
storageReadS3Close(THIS_VOID)
{
    THIS(StorageReadS3);

    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(STORAGE_READ_S3, this);
    FUNCTION_LOG_END();

    ASSERT(this != NULL);
    ASSERT(this->httpClient != NULL);

    memContextCallbackClear(this->memContext);
    storageReadS3FreeResource(this);
    this->httpClient = NULL;

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Has file reached EOF?
***********************************************************************************************************************************/
static bool
storageReadS3Eof(THIS_VOID)
{
    THIS(StorageReadS3);

    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_READ_S3, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL && this->httpClient != NULL);
    ASSERT(httpClientIoRead(this->httpClient) != NULL);

    FUNCTION_TEST_RETURN(ioReadEof(httpClientIoRead(this->httpClient)));
}

/**********************************************************************************************************************************/
StorageRead *
storageReadS3New(StorageS3 *storage, const String *name, bool ignoreMissing)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(STORAGE_S3, storage);
        FUNCTION_LOG_PARAM(STRING, name);
        FUNCTION_LOG_PARAM(BOOL, ignoreMissing);
    FUNCTION_LOG_END();

    ASSERT(storage != NULL);
    ASSERT(name != NULL);

    StorageRead *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("StorageReadS3")
    {
        StorageReadS3 *driver = memNew(sizeof(StorageReadS3));

        *driver = (StorageReadS3)
        {
            .memContext = MEM_CONTEXT_NEW(),
            .storage = storage,

            .interface = (StorageReadInterface)
            {
                .type = STORAGE_S3_TYPE_STR,
                .name = strDup(name),
                .ignoreMissing = ignoreMissing,

                .ioInterface = (IoReadInterface)
                {
                    .close = storageReadS3Close,
                    .eof = storageReadS3Eof,
                    .open = storageReadS3Open,
                    .read = storageReadS3,
                },
            },
        };

        this = storageReadNew(driver, &driver->interface);
    }
    MEM_CONTEXT_NEW_END();

    FUNCTION_LOG_RETURN(STORAGE_READ, this);
}
