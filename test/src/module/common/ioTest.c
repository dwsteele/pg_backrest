/***********************************************************************************************************************************
Test IO
***********************************************************************************************************************************/
#include <fcntl.h>

#include "common/assert.h"

/***********************************************************************************************************************************
Test functions for IoRead that are not covered by testing the IoBufferRead object
***********************************************************************************************************************************/
static bool
testIoReadOpen(void *driver)
{
    if (driver == (void *)998)
        return false;

    return true;
}

static size_t
testIoRead(void *driver, Buffer *buffer)
{
    ASSERT(driver == (void *)999);
    bufCat(buffer, bufNewZ("Z"));
    return 1;
}

static bool testIoReadCloseCalled = false;

static void
testIoReadClose(void *driver)
{
    ASSERT(driver == (void *)999);
    testIoReadCloseCalled = true;
}

/***********************************************************************************************************************************
Test functions for IoWrite that are not covered by testing the IoBufferWrite object
***********************************************************************************************************************************/
static bool testIoWriteOpenCalled = false;

static void
testIoWriteOpen(void *driver)
{
    ASSERT(driver == (void *)999);
    testIoWriteOpenCalled = true;
}

static void
testIoWrite(void *driver, const Buffer *buffer)
{
    ASSERT(driver == (void *)999);
    ASSERT(strEq(strNewBuf(buffer), strNew("ABC")));
}

static bool testIoWriteCloseCalled = false;

static void
testIoWriteClose(void *driver)
{
    ASSERT(driver == (void *)999);
    testIoWriteCloseCalled = true;
}

/***********************************************************************************************************************************
Test filter that counts total bytes
***********************************************************************************************************************************/
typedef struct IoTestFilterSize
{
    MemContext *memContext;
    size_t size;
    IoFilter *filter;
} IoTestFilterSize;

static void
ioTestFilterSizeProcess(IoTestFilterSize *this, const Buffer *buffer)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(VOIDP, this);
        FUNCTION_DEBUG_PARAM(BUFFER, buffer);
        FUNCTION_DEBUG_PARAM(STRING, ioFilterType(this->filter));
        FUNCTION_DEBUG_PARAM(SIZE, this->size);

        FUNCTION_TEST_ASSERT(this != NULL);
        FUNCTION_TEST_ASSERT(buffer != NULL);
    FUNCTION_DEBUG_END();

    this->size += bufUsed(buffer);

    FUNCTION_DEBUG_RESULT_VOID();
}

static const Variant *
ioTestFilterSizeResult(IoTestFilterSize *this)
{
    return varNewUInt64(this->size);
}

static IoTestFilterSize *
ioTestFilterSizeNew(const char *type)
{
    IoTestFilterSize *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("IoTestFilterSize")
    {
        this = memNew(sizeof(IoTestFilterSize));
        this->memContext = MEM_CONTEXT_NEW();

        this->filter = ioFilterNewP(
            strNew(type), this, .in = (IoFilterInterfaceProcessIn)ioTestFilterSizeProcess,
            .result = (IoFilterInterfaceResult)ioTestFilterSizeResult);
    }
    MEM_CONTEXT_NEW_END();

    return this;
}

/***********************************************************************************************************************************
Test filter to double input to the output.  It can also flush out a variable number of bytes at the end.
***********************************************************************************************************************************/
typedef struct IoTestFilterDouble
{
    MemContext *memContext;
    unsigned int flushTotal;
    Buffer *doubleBuffer;
    IoFilter *bufferFilter;
    IoFilter *filter;
} IoTestFilterDouble;

static void
ioTestFilterDoubleProcess(IoTestFilterDouble *this, const Buffer *input, Buffer *output)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(VOIDP, this);
        FUNCTION_DEBUG_PARAM(BUFFER, input);
        FUNCTION_DEBUG_PARAM(BUFFER, output);

        FUNCTION_TEST_ASSERT(this != NULL);
        FUNCTION_TEST_ASSERT(output != NULL && bufRemains(output) > 0);
    FUNCTION_DEBUG_END();

    if (input == NULL)
    {
        bufCat(output, bufNewC(1, "X"));
        this->flushTotal--;
    }
    else
    {
        if (this->doubleBuffer == NULL)
        {
            this->doubleBuffer = bufNew(bufUsed(input) * 2);
            unsigned char *inputPtr = bufPtr(input);
            unsigned char *bufferPtr = bufPtr(this->doubleBuffer);

            for (unsigned int charIdx = 0; charIdx < bufUsed(input); charIdx++)
            {
                bufferPtr[charIdx * 2] = inputPtr[charIdx];
                bufferPtr[charIdx * 2 + 1] = inputPtr[charIdx];
            }

            bufUsedSet(this->doubleBuffer, bufSize(this->doubleBuffer));
        }

        ioFilterProcessInOut(this->bufferFilter, this->doubleBuffer, output);

        if (!ioFilterInputSame(this->bufferFilter))
            this->doubleBuffer = NULL;
    }

    FUNCTION_DEBUG_RESULT_VOID();
}

static bool
ioTestFilterDoubleDone(IoTestFilterDouble *this)
{
    return this->flushTotal == 0;
}

static bool
ioTestFilterDoubleInputSame(IoTestFilterDouble *this)
{
    return ioFilterInputSame(this->bufferFilter);
}

static IoTestFilterDouble *
ioTestFilterDoubleNew(const char *type, unsigned int flushTotal)
{
    IoTestFilterDouble *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("IoTestFilterDouble")
    {
        this = memNew(sizeof(IoTestFilterDouble));
        this->memContext = MEM_CONTEXT_NEW();
        this->bufferFilter = ioBufferFilter(ioBufferNew());
        this->flushTotal = flushTotal;

        this->filter = ioFilterNewP(
            strNew(type), this, .done = (IoFilterInterfaceDone)ioTestFilterDoubleDone,
            .inOut = (IoFilterInterfaceProcessInOut)ioTestFilterDoubleProcess,
            .inputSame = (IoFilterInterfaceInputSame)ioTestFilterDoubleInputSame);
    }
    MEM_CONTEXT_NEW_END();

    return this;
}

/***********************************************************************************************************************************
Test Run
***********************************************************************************************************************************/
void
testRun(void)
{
    FUNCTION_HARNESS_VOID();

    // *****************************************************************************************************************************
    if (testBegin("ioBufferSize() and ioBufferSizeSet()"))
    {
        TEST_RESULT_SIZE(ioBufferSize(), 65536, "check initial buffer size");
        TEST_RESULT_VOID(ioBufferSizeSet(16384), "set buffer size");
        TEST_RESULT_SIZE(ioBufferSize(), 16384, "check buffer size");
    }

    // *****************************************************************************************************************************
    if (testBegin("IoRead, IoBufferRead, IoBuffer, IoSize, IoFilter, and IoFilterGroup"))
    {
        IoRead *read = NULL;
        Buffer *buffer = bufNew(2);
        ioBufferSizeSet(2);

        TEST_ASSIGN(
            read,
            ioReadNewP((void *)998, .close = (IoReadInterfaceClose)testIoReadClose, .open = (IoReadInterfaceOpen)testIoReadOpen,
                .read = (IoReadInterfaceRead)testIoRead),
            "create io read object");

        TEST_RESULT_BOOL(ioReadOpen(read), false, "    open io object");

        TEST_ASSIGN(
            read,
            ioReadNewP((void *)999, .close = (IoReadInterfaceClose)testIoReadClose, .open = (IoReadInterfaceOpen)testIoReadOpen,
                .read = (IoReadInterfaceRead)testIoRead),
            "create io read object");

        TEST_RESULT_BOOL(ioReadOpen(read), true, "    open io object");
        TEST_RESULT_SIZE(ioRead(read, buffer), 2, "    read 2 bytes");
        TEST_RESULT_BOOL(ioReadEof(read), false, "    no eof");
        TEST_RESULT_VOID(ioReadClose(read), "    close io object");
        TEST_RESULT_BOOL(testIoReadCloseCalled, true, "    check io object closed");

        // -------------------------------------------------------------------------------------------------------------------------
        IoBufferRead *bufferRead = NULL;
        ioBufferSizeSet(2);
        buffer = bufNew(2);
        Buffer *bufferOriginal = bufNewZ("123");

        MEM_CONTEXT_TEMP_BEGIN()
        {
            TEST_ASSIGN(bufferRead, ioBufferReadNew(bufferOriginal), "create buffer read object");
            TEST_RESULT_VOID(ioBufferReadMove(bufferRead, MEM_CONTEXT_OLD()), "    move object to new context");
            TEST_RESULT_VOID(ioBufferReadMove(NULL, MEM_CONTEXT_OLD()), "    move NULL object to new context");
        }
        MEM_CONTEXT_TEMP_END();

        IoFilterGroup *filterGroup = NULL;
        TEST_ASSIGN(filterGroup, ioFilterGroupNew(), "    create new filter group");
        IoSize *sizeFilter = ioSizeNew();
        TEST_RESULT_VOID(ioFilterGroupAdd(filterGroup, ioSizeFilter(sizeFilter)), "    add filter to filter group");
        TEST_RESULT_VOID(
            ioFilterGroupAdd(filterGroup, ioTestFilterDoubleNew("double", 1)->filter), "    add filter to filter group");
        TEST_RESULT_VOID(ioFilterGroupAdd(filterGroup, ioSizeFilter(ioSizeNew())), "    add filter to filter group");
        IoBuffer *bufferFilter = ioBufferNew();
        TEST_RESULT_VOID(ioFilterGroupAdd(filterGroup, ioBufferFilter(bufferFilter)), "    add filter to filter group");
        TEST_RESULT_VOID(ioReadFilterGroupSet(ioBufferReadIo(bufferRead), filterGroup), "    add filter group to read io");
        TEST_RESULT_PTR(ioFilterMove(NULL, memContextTop()), NULL, "    move NULL filter to top context");

        TEST_RESULT_BOOL(ioReadOpen(ioBufferReadIo(bufferRead)), true, "    open");
        TEST_RESULT_BOOL(ioReadEof(ioBufferReadIo(bufferRead)), false, "    not eof");
        TEST_RESULT_SIZE(ioRead(ioBufferReadIo(bufferRead), buffer), 2, "    read 2 bytes");
        TEST_RESULT_SIZE(ioRead(ioBufferReadIo(bufferRead), buffer), 0, "    read 0 bytes (full buffer)");
        TEST_RESULT_STR(strPtr(strNewBuf(buffer)), "11", "    check read");
        TEST_RESULT_STR(strPtr(ioFilterType(ioSizeFilter(sizeFilter))), "size", "check filter type");
        TEST_RESULT_BOOL(ioReadEof(ioBufferReadIo(bufferRead)), false, "    not eof");

        TEST_RESULT_VOID(bufUsedZero(buffer), "    zero buffer");
        TEST_RESULT_SIZE(ioRead(ioBufferReadIo(bufferRead), buffer), 2, "    read 2 bytes");
        TEST_RESULT_STR(strPtr(strNewBuf(buffer)), "22", "    check read");

        TEST_ASSIGN(buffer, bufNew(3), "change output buffer size to 3");
        TEST_RESULT_SIZE(ioRead(ioBufferReadIo(bufferRead), buffer), 3, "    read 3 bytes");
        TEST_RESULT_STR(strPtr(strNewBuf(buffer)), "33X", "    check read");

        TEST_RESULT_VOID(bufUsedZero(buffer), "    zero buffer");
        TEST_RESULT_BOOL(ioReadEof(ioBufferReadIo(bufferRead)), true, "    eof");
        TEST_RESULT_BOOL(ioBufferRead(bufferRead, buffer), 0, "    eof from driver");
        TEST_RESULT_SIZE(ioRead(ioBufferReadIo(bufferRead), buffer), 0, "    read 0 bytes");
        TEST_RESULT_VOID(ioReadClose(ioBufferReadIo(bufferRead)), " close buffer read object");

        TEST_RESULT_PTR(ioReadFilterGroup(ioBufferReadIo(bufferRead)), filterGroup, "    check filter group");
        TEST_RESULT_UINT(
            varUInt64(varLstGet(varVarLst(ioFilterGroupResult(filterGroup, ioFilterType(ioSizeFilter(sizeFilter)))), 0)), 3,
            "    check filter result");
        TEST_RESULT_PTR(ioFilterGroupResult(filterGroup, strNew("double")), NULL, "    check filter result is NULL");
        TEST_RESULT_UINT(
            varUInt64(varLstGet(varVarLst(ioFilterGroupResult(filterGroup, ioFilterType(ioSizeFilter(sizeFilter)))), 1)), 7,
            "    check filter result");

        TEST_RESULT_VOID(ioBufferReadFree(bufferRead), "    free buffer read object");
        TEST_RESULT_VOID(ioBufferReadFree(NULL), "    free NULL buffer read object");

        TEST_RESULT_VOID(ioSizeFree(sizeFilter), "    free size filter object");
        TEST_RESULT_VOID(ioSizeFree(NULL), "    free null size filter object");

        TEST_RESULT_VOID(ioBufferFree(bufferFilter), "    free buffer filter object");
        TEST_RESULT_VOID(ioBufferFree(NULL), "    free null buffer filter object");

        TEST_RESULT_VOID(ioFilterGroupFree(filterGroup), "    free filter group object");
        TEST_RESULT_VOID(ioFilterGroupFree(NULL), "    free NULL filter group object");
    }

    // *****************************************************************************************************************************
    if (testBegin("IoWrite, IoBufferWrite, IoBuffer, IoSize, IoFilter, and IoFilterGroup"))
    {
        IoWrite *write = NULL;
        ioBufferSizeSet(3);

        TEST_ASSIGN(
            write,
            ioWriteNewP(
                (void *)999, .close = (IoWriteInterfaceClose)testIoWriteClose, .open = (IoWriteInterfaceOpen)testIoWriteOpen,
                .write = (IoWriteInterfaceWrite)testIoWrite),
            "create io write object");

        TEST_RESULT_VOID(ioWriteOpen(write), "    open io object");
        TEST_RESULT_BOOL(testIoWriteOpenCalled, true, "    check io object open");
        TEST_RESULT_VOID(ioWrite(write, bufNewZ("ABC")), "    write 3 bytes");
        TEST_RESULT_VOID(ioWriteClose(write), "    close io object");
        TEST_RESULT_BOOL(testIoWriteCloseCalled, true, "    check io object closed");

        // -------------------------------------------------------------------------------------------------------------------------
        ioBufferSizeSet(3);
        IoBufferWrite *bufferWrite = NULL;
        Buffer *buffer = bufNew(0);

        MEM_CONTEXT_TEMP_BEGIN()
        {
            TEST_ASSIGN(bufferWrite, ioBufferWriteNew(buffer), "create buffer write object");
            TEST_RESULT_VOID(ioBufferWriteMove(bufferWrite, MEM_CONTEXT_OLD()), "    move object to new context");
            TEST_RESULT_VOID(ioBufferWriteMove(NULL, MEM_CONTEXT_OLD()), "    move NULL object to new context");
        }
        MEM_CONTEXT_TEMP_END();

        IoFilterGroup *filterGroup = NULL;
        TEST_ASSIGN(filterGroup, ioFilterGroupNew(), "    create new filter group");
        IoSize *sizeFilter = ioSizeNew();
        TEST_RESULT_VOID(ioFilterGroupAdd(filterGroup, ioSizeFilter(sizeFilter)), "    add filter to filter group");
        TEST_RESULT_VOID(
            ioFilterGroupAdd(filterGroup, ioTestFilterDoubleNew("double", 3)->filter), "    add filter to filter group");
        TEST_RESULT_VOID(ioFilterGroupAdd(filterGroup, ioTestFilterSizeNew("size2")->filter), "    add filter to filter group");
        TEST_RESULT_VOID(ioWriteFilterGroupSet(ioBufferWriteIo(bufferWrite), filterGroup), "    add filter group to write io");

        TEST_RESULT_VOID(ioWriteOpen(ioBufferWriteIo(bufferWrite)), "    open buffer write object");
        TEST_RESULT_VOID(ioWrite(ioBufferWriteIo(bufferWrite), bufNewZ("ABC")), "    write 3 bytes");
        TEST_RESULT_VOID(ioWrite(ioBufferWriteIo(bufferWrite), bufNew(0)), "    write 0 bytes");
        TEST_RESULT_VOID(ioWrite(ioBufferWriteIo(bufferWrite), NULL), "    write 0 bytes");
        TEST_RESULT_STR(strPtr(strNewBuf(buffer)), "AABBCC", "    check write");

        TEST_RESULT_VOID(ioWrite(ioBufferWriteIo(bufferWrite), bufNewZ("12345")), "    write 4 bytes");
        TEST_RESULT_STR(strPtr(strNewBuf(buffer)), "AABBCC112233445", "    check write");

        TEST_RESULT_VOID(ioWriteClose(ioBufferWriteIo(bufferWrite)), " close buffer write object");
        TEST_RESULT_STR(strPtr(strNewBuf(buffer)), "AABBCC1122334455XXX", "    check write after close");

        TEST_RESULT_PTR(ioWriteFilterGroup(ioBufferWriteIo(bufferWrite)), filterGroup, "    check filter group");
        TEST_RESULT_UINT(
            varUInt64(ioFilterGroupResult(filterGroup, ioFilterType(ioSizeFilter(sizeFilter)))), 8, "    check filter result");
        TEST_RESULT_UINT(varUInt64(ioFilterGroupResult(filterGroup, strNew("size2"))), 19, "    check filter result");

        TEST_RESULT_VOID(ioBufferWriteFree(bufferWrite), "    free buffer write object");
        TEST_RESULT_VOID(ioBufferWriteFree(NULL), "    free NULL buffer write object");
    }

    // *****************************************************************************************************************************
    if (testBegin("ioHandleWriteOneStr()"))
    {
        TEST_ERROR(
            ioHandleWriteOneStr(999999, strNew("test")), FileWriteError,
            "unable to write to 4 byte(s) to handle: [9] Bad file descriptor");

        // -------------------------------------------------------------------------------------------------------------------------
        String *fileName = strNewFmt("%s/test.txt", testPath());
        int fileHandle = open(strPtr(fileName), O_CREAT | O_TRUNC | O_WRONLY, 0700);

        TEST_RESULT_VOID(ioHandleWriteOneStr(fileHandle, strNew("test1\ntest2")), "write string to file");
    }

    FUNCTION_HARNESS_RESULT_VOID();
}
