/***********************************************************************************************************************************
Storage File Write
***********************************************************************************************************************************/
#include "common/assert.h"
#include "common/debug.h"
#include "common/log.h"
#include "common/memContext.h"
#include "storage/fileWrite.h"

/***********************************************************************************************************************************
Storage file structure
***********************************************************************************************************************************/
struct StorageFileWrite
{
    MemContext *memContext;
    StorageFileWritePosix *fileDriver;
    IoWrite *io;
};

/***********************************************************************************************************************************
Create a new storage file

This object expects its context to be created in advance.  This is so the calling function can add whatever data it wants without
required multiple functions and contexts to make it safe.
***********************************************************************************************************************************/
StorageFileWrite *
storageFileWriteNew(
    const String *name, mode_t modeFile, mode_t modePath, bool noCreatePath, bool noSyncFile, bool noSyncPath, bool noAtomic)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(STRING, name);
        FUNCTION_DEBUG_PARAM(MODE, modeFile);
        FUNCTION_DEBUG_PARAM(MODE, modePath);
        FUNCTION_DEBUG_PARAM(BOOL, noCreatePath);
        FUNCTION_DEBUG_PARAM(BOOL, noSyncFile);
        FUNCTION_DEBUG_PARAM(BOOL, noSyncPath);
        FUNCTION_DEBUG_PARAM(BOOL, noAtomic);

        FUNCTION_TEST_ASSERT(name != NULL);
    FUNCTION_DEBUG_END();

    StorageFileWrite *this = NULL;

    ASSERT_DEBUG(name != NULL);

    MEM_CONTEXT_NEW_BEGIN("StorageFileWrite")
    {
        this = memNew(sizeof(StorageFileWrite));
        this->memContext = MEM_CONTEXT_NEW();

        this->fileDriver = storageFileWritePosixNew(name, modeFile, modePath, noCreatePath, noSyncFile, noSyncPath, noAtomic);

        this->io = ioWriteNew(
            this->fileDriver, (IoWriteOpen)storageFileWritePosixOpen, (IoWriteProcess)storageFileWritePosix,
            (IoWriteClose)storageFileWritePosixClose);
    }
    MEM_CONTEXT_NEW_END();

    FUNCTION_DEBUG_RESULT(STORAGE_FILE_WRITE, this);
}

/***********************************************************************************************************************************
Move the file object to a new context
***********************************************************************************************************************************/
StorageFileWrite *
storageFileWriteMove(StorageFileWrite *this, MemContext *parentNew)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_FILE_WRITE, this);
        FUNCTION_TEST_PARAM(MEM_CONTEXT, parentNew);

        FUNCTION_TEST_ASSERT(parentNew != NULL);
    FUNCTION_TEST_END();

    if (this != NULL)
        memContextMove(this->memContext, parentNew);

    FUNCTION_TEST_RESULT(STORAGE_FILE_WRITE, this);
}

/***********************************************************************************************************************************
Will the file be written atomically?

Atomic writes means the file will be complete or be missing.  Filesystems have different ways to accomplish this.
***********************************************************************************************************************************/
bool
storageFileWriteAtomic(const StorageFileWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_FILE_WRITE, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(BOOL, storageFileWritePosixAtomic(this->fileDriver));
}

/***********************************************************************************************************************************
Will the path be created if required?
***********************************************************************************************************************************/
bool
storageFileWriteCreatePath(const StorageFileWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_FILE_WRITE, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(BOOL, storageFileWritePosixCreatePath(this->fileDriver));
}

/***********************************************************************************************************************************
Get file driver
***********************************************************************************************************************************/
StorageFileWritePosix *
storageFileWriteFileDriver(const StorageFileWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_FILE_WRITE, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(STORAGE_FILE_WRITE_POSIX, this->fileDriver);
}

/***********************************************************************************************************************************
Get the IO object
***********************************************************************************************************************************/
IoWrite *
storageFileWriteIo(const StorageFileWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_FILE_WRITE, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(IO_WRITE, this->io);
}

/***********************************************************************************************************************************
Get file mode
***********************************************************************************************************************************/
mode_t
storageFileWriteModeFile(const StorageFileWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_FILE_WRITE, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(MODE, storageFileWritePosixModeFile(this->fileDriver));
}

/***********************************************************************************************************************************
Get path mode
***********************************************************************************************************************************/
mode_t
storageFileWriteModePath(const StorageFileWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_FILE_WRITE, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(MODE, storageFileWritePosixModePath(this->fileDriver));
}

/***********************************************************************************************************************************
Get file name
***********************************************************************************************************************************/
const String *
storageFileWriteName(const StorageFileWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_FILE_WRITE, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(CONST_STRING, storageFileWritePosixName(this->fileDriver));
}

/***********************************************************************************************************************************
Get file path
***********************************************************************************************************************************/
const String *
storageFileWritePath(const StorageFileWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_FILE_WRITE, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(CONST_STRING, storageFileWritePosixPath(this->fileDriver));
}

/***********************************************************************************************************************************
Will the file be synced after it is closed?
***********************************************************************************************************************************/
bool
storageFileWriteSyncFile(const StorageFileWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_FILE_WRITE, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(BOOL, storageFileWritePosixSyncFile(this->fileDriver));
}

/***********************************************************************************************************************************
Will the path be synced after the file is closed?
***********************************************************************************************************************************/
bool
storageFileWriteSyncPath(const StorageFileWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_FILE_WRITE, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(BOOL, storageFileWritePosixSyncPath(this->fileDriver));
}

/***********************************************************************************************************************************
Free the file
***********************************************************************************************************************************/
void
storageFileWriteFree(const StorageFileWrite *this)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(STORAGE_FILE_WRITE, this);
    FUNCTION_DEBUG_END();

    if (this != NULL)
        memContextFree(this->memContext);

    FUNCTION_DEBUG_RESULT_VOID();
}
