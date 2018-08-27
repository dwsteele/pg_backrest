/***********************************************************************************************************************************
Gzip Decompress
***********************************************************************************************************************************/
#include <stdio.h>
#include <zlib.h>

#include "common/debug.h"
#include "common/log.h"
#include "common/memContext.h"
#include "compress/gzip.h"
#include "compress/gzipDecompress.h"

/***********************************************************************************************************************************
Filter type constant
***********************************************************************************************************************************/
#define GZIP_DECOMPRESS_FILTER_TYPE                                 "gzipDecompress"

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
struct GzipDecompress
{
    MemContext *memContext;                                         // Context to store data
    z_stream *stream;                                               // Decompression stream state
    IoFilter *filter;                                               // Filter interface

    int result;                                                     // Result of last operation
    bool inputSame;                                                 // Is the same input required on the next process call?
    bool done;                                                      // Is decompression done?
};

/***********************************************************************************************************************************
New object
***********************************************************************************************************************************/
GzipDecompress *
gzipDecompressNew(bool raw)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(BOOL, raw);
    FUNCTION_DEBUG_END();

    GzipDecompress *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("GzipDecompress")
    {
        // Allocate state and set context
        this = memNew(sizeof(GzipDecompress));
        this->memContext = MEM_CONTEXT_NEW();

        // Create gzip stream
        this->stream = memNew(sizeof(z_stream));
        gzipError(this->result = inflateInit2(this->stream, gzipWindowBits(raw)));

        // Set free callback to ensure gzip context is freed
        memContextCallback(this->memContext, (MemContextCallback)gzipDecompressFree, this);

        // Create filter interface
        this->filter = ioFilterNew(
            strNew(GZIP_DECOMPRESS_FILTER_TYPE), this, (IoFilterDone)gzipDecompressDone, (IoFilterInputSame)gzipDecompressInputSame,
            NULL, (IoFilterProcessInOut)gzipDecompressProcess, NULL);
    }
    MEM_CONTEXT_NEW_END();

    FUNCTION_DEBUG_RESULT(GZIP_DECOMPRESS, this);
}

/***********************************************************************************************************************************
Decompress data
***********************************************************************************************************************************/
void
gzipDecompressProcess(GzipDecompress *this, const Buffer *compressed, Buffer *uncompressed)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(GZIP_DECOMPRESS, this);
        FUNCTION_DEBUG_PARAM(BUFFER, compressed);
        FUNCTION_DEBUG_PARAM(BUFFER, uncompressed);

        FUNCTION_DEBUG_ASSERT(this != NULL);
        FUNCTION_DEBUG_ASSERT(this->stream != NULL);
        FUNCTION_DEBUG_ASSERT(compressed != NULL);
        FUNCTION_DEBUG_ASSERT(uncompressed != NULL);
    FUNCTION_DEBUG_END();

    if (!this->inputSame)
    {
        this->stream->avail_in = (unsigned int)bufUsed(compressed);
        this->stream->next_in = bufPtr(compressed);
    }

    this->stream->avail_out = (unsigned int)bufRemains(uncompressed);
    this->stream->next_out = bufPtr(uncompressed) + bufUsed(uncompressed);

    this->result = gzipError(inflate(this->stream, Z_NO_FLUSH));

    // Set buffer used space
    bufUsedSet(uncompressed, bufSize(uncompressed) - (size_t)this->stream->avail_out);

    // Is decompression done?
    this->done = this->result == Z_STREAM_END;

    // Is the same input expected on the next call?
    this->inputSame = this->done ? false : this->stream->avail_in != 0;

    FUNCTION_DEBUG_RESULT_VOID();
}

/***********************************************************************************************************************************
Is decompress done?
***********************************************************************************************************************************/
bool
gzipDecompressDone(const GzipDecompress *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(GZIP_DECOMPRESS, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(BOOL, this->done);
}

/***********************************************************************************************************************************
Get filter interface
***********************************************************************************************************************************/
IoFilter *
gzipDecompressFilter(const GzipDecompress *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(GZIP_DECOMPRESS, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(IO_FILTER, this->filter);
}

/***********************************************************************************************************************************
Is the same input required on the next process call?
***********************************************************************************************************************************/
bool
gzipDecompressInputSame(const GzipDecompress *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(GZIP_DECOMPRESS, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(BOOL, this->inputSame);
}

/***********************************************************************************************************************************
Convert to a zero-terminated string for logging
***********************************************************************************************************************************/
size_t
gzipDecompressToLog(const GzipDecompress *this, char *buffer, size_t bufferSize)
{
    size_t result = 0;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        String *string = NULL;

        if (this == NULL)
            string = strNew("null");
        else
        {
            string = strNewFmt(
                "{inputSame: %s, done: %s, availIn: %u}", cvtBoolToConstZ(this->inputSame), cvtBoolToConstZ(this->done),
                this->stream != NULL ? this->stream->avail_in : 0);
        }

        result = (size_t)snprintf(buffer, bufferSize, "%s", strPtr(string));
    }
    MEM_CONTEXT_TEMP_END();

    return result;
}

/***********************************************************************************************************************************
Free memory
***********************************************************************************************************************************/
void
gzipDecompressFree(GzipDecompress *this)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(GZIP_DECOMPRESS, this);
    FUNCTION_DEBUG_END();

    if (this != NULL)
    {
        if (this->stream != NULL)
        {
            inflateEnd(this->stream);
            this->stream = NULL;
        }

        memContextFree(this->memContext);
    }

    FUNCTION_DEBUG_RESULT_VOID();
}
