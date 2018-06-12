/***********************************************************************************************************************************
Log Handler
***********************************************************************************************************************************/
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "common/assert.h"
#include "common/debug.h"
#include "common/error.h"
#include "common/log.h"
#include "common/time.h"

/***********************************************************************************************************************************
Module variables
***********************************************************************************************************************************/
// Log levels
DEBUG_UNIT_EXTERN LogLevel logLevelStdOut = logLevelError;
DEBUG_UNIT_EXTERN LogLevel logLevelStdErr = logLevelError;
DEBUG_UNIT_EXTERN LogLevel logLevelFile = logLevelOff;

// Log file handles
DEBUG_UNIT_EXTERN int logHandleStdOut = STDOUT_FILENO;
DEBUG_UNIT_EXTERN int logHandleStdErr = STDERR_FILENO;
DEBUG_UNIT_EXTERN int logHandleFile = -1;

// Has the log file banner been written yet?
static bool logFileBanner = false;

// Is the timestamp printed in the log?
DEBUG_UNIT_EXTERN bool logTimestamp = false;

/***********************************************************************************************************************************
Test Asserts
***********************************************************************************************************************************/
#define FUNCTION_TEST_ASSERT_LOG_LEVEL(logLevel)                                                                                   \
    FUNCTION_TEST_ASSERT(logLevel >= LOG_LEVEL_MIN && logLevel <= LOG_LEVEL_MAX)

/***********************************************************************************************************************************
Log buffer -- used to format log header and message
***********************************************************************************************************************************/
static char logBuffer[LOG_BUFFER_SIZE];

/***********************************************************************************************************************************
Convert log level to string and vice versa
***********************************************************************************************************************************/
#define LOG_LEVEL_TOTAL                                             (LOG_LEVEL_MAX + 1)

static const char *logLevelList[LOG_LEVEL_TOTAL] =
{
    "OFF",
    "ASSERT",
    "ERROR",
    "PROTOCOL",
    "WARN",
    "INFO",
    "DETAIL",
    "DEBUG",
    "TRACE",
};

LogLevel
logLevelEnum(const char *logLevel)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRINGZ, logLevel);

        FUNCTION_TEST_ASSERT(logLevel != NULL);
    FUNCTION_TEST_END();

    LogLevel result = logLevelOff;

    // Search for the log level
    for (; result < LOG_LEVEL_TOTAL; result++)
        if (strcasecmp(logLevel, logLevelList[result]) == 0)
            break;

    // If the log level was not found
    if (result == LOG_LEVEL_TOTAL)
        THROW_FMT(AssertError, "log level '%s' not found", logLevel);

    FUNCTION_TEST_RESULT(ENUM, result);
}

const char *
logLevelStr(LogLevel logLevel)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, logLevel);

        FUNCTION_TEST_ASSERT(logLevel <= LOG_LEVEL_MAX);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(STRINGZ, logLevelList[logLevel]);
}

/***********************************************************************************************************************************
Initialize the log system
***********************************************************************************************************************************/
void
logInit(LogLevel logLevelStdOutParam, LogLevel logLevelStdErrParam, LogLevel logLevelFileParam, bool logTimestampParam)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, logLevelStdOutParam);
        FUNCTION_TEST_PARAM(ENUM, logLevelStdErrParam);
        FUNCTION_TEST_PARAM(ENUM, logLevelFileParam);
        FUNCTION_TEST_PARAM(BOOL, logTimestampParam);

        FUNCTION_TEST_ASSERT(logLevelStdOutParam <= LOG_LEVEL_MAX);
        FUNCTION_TEST_ASSERT(logLevelStdErrParam <= LOG_LEVEL_MAX);
        FUNCTION_TEST_ASSERT(logLevelFileParam <= LOG_LEVEL_MAX);
    FUNCTION_TEST_END();

    logLevelStdOut = logLevelStdOutParam;
    logLevelStdErr = logLevelStdErrParam;
    logLevelFile = logLevelFileParam;
    logTimestamp = logTimestampParam;

    FUNCTION_TEST_RESULT_VOID();
}

/***********************************************************************************************************************************
Set the log file
***********************************************************************************************************************************/
void
logFileSet(const char *logFile)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRINGZ, logFile);

        FUNCTION_TEST_ASSERT(logFile != NULL);
    FUNCTION_TEST_END();

    // Close the file handle if it is already open
    if (logHandleFile != -1)
    {
        close(logHandleFile);
        logHandleFile = -1;
    }

    // Only open the file if there is a chance to log something
    if (logLevelFile != logLevelOff)
    {
        // Open the file and handle errors
        logHandleFile = open(logFile, O_CREAT | O_APPEND | O_WRONLY, 0750);

        if (logHandleFile == -1)
        {
            int errNo = errno;
            LOG_WARN("unable to open log file '%s': %s\nNOTE: process will continue without log file.", logFile, strerror(errNo));
        };

        // Output the banner on first log message
        logFileBanner = false;
    }

    FUNCTION_TEST_RESULT_VOID();
}

/***********************************************************************************************************************************
Check if a log level will be logged to any output

This is useful for log messages that are expensive to generate and should be skipped if they will be discarded.
***********************************************************************************************************************************/
static bool
logWillFile(LogLevel logLevel)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, logLevel);

        FUNCTION_TEST_ASSERT_LOG_LEVEL(logLevel);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(ENUM, logLevel <= logLevelFile && logHandleFile != -1);
}

static bool
logWillStdErr(LogLevel logLevel)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, logLevel);

        FUNCTION_TEST_ASSERT_LOG_LEVEL(logLevel);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(ENUM, logLevel <= logLevelStdErr);
}

static bool
logWillStdOut(LogLevel logLevel)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, logLevel);

        FUNCTION_TEST_ASSERT_LOG_LEVEL(logLevel);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(ENUM, logLevel <= logLevelStdOut);
}

bool
logWill(LogLevel logLevel)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, logLevel);

        FUNCTION_TEST_ASSERT_LOG_LEVEL(logLevel);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(BOOL, logWillStdOut(logLevel) || logWillStdErr(logLevel) || logWillFile(logLevel));
}

/***********************************************************************************************************************************
Determine if the log level is in the specified range
***********************************************************************************************************************************/
static bool
logRange(LogLevel logLevel, LogLevel logRangeMin, LogLevel logRangeMax)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, logLevel);
        FUNCTION_TEST_PARAM(ENUM, logRangeMin);
        FUNCTION_TEST_PARAM(ENUM, logRangeMax);

        FUNCTION_TEST_ASSERT_LOG_LEVEL(logLevel);
        FUNCTION_TEST_ASSERT_LOG_LEVEL(logRangeMin);
        FUNCTION_TEST_ASSERT_LOG_LEVEL(logRangeMax);
        FUNCTION_TEST_ASSERT(logRangeMin <= logRangeMax);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(BOOL, logLevel >= logRangeMin && logLevel <= logRangeMax);
}

/***********************************************************************************************************************************
Internal write function that handles errors
***********************************************************************************************************************************/
static void
logWrite(int handle, const char *message, size_t messageSize, const char *errorDetail)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INT, handle);
        FUNCTION_TEST_PARAM(STRINGZ, message);
        FUNCTION_TEST_PARAM(SIZE, messageSize);
        FUNCTION_TEST_PARAM(STRINGZ, errorDetail);

        FUNCTION_TEST_ASSERT(handle != -1);
        FUNCTION_TEST_ASSERT(message != NULL);
        FUNCTION_TEST_ASSERT(messageSize != 0);
        FUNCTION_TEST_ASSERT(errorDetail != NULL);
    FUNCTION_TEST_END();

    if ((size_t)write(handle, message, messageSize) != messageSize)
        THROW_SYS_ERROR_FMT(FileWriteError, "unable to write %s", errorDetail);

    FUNCTION_TEST_RESULT_VOID();
}

/***********************************************************************************************************************************
Write out log message and indent subsequent lines
***********************************************************************************************************************************/
static void
logWriteIndent(int handle, const char *message, size_t indentSize, const char *errorDetail)
{
    // Indent buffer -- used to write out indent space without having to loop
    static const char indentBuffer[] = "                                                                                          ";

    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INT, handle);
        FUNCTION_TEST_PARAM(STRINGZ, message);
        FUNCTION_TEST_PARAM(SIZE, indentSize);
        FUNCTION_TEST_PARAM(STRINGZ, errorDetail);

        FUNCTION_TEST_ASSERT(handle != -1);
        FUNCTION_TEST_ASSERT(message != NULL);
        FUNCTION_TEST_ASSERT(indentSize > 0 && indentSize < sizeof(indentBuffer));
        FUNCTION_TEST_ASSERT(errorDetail != NULL);
    FUNCTION_TEST_END();

    // Indent all lines after the first
    const char *linefeedPtr = strchr(message, '\n');
    bool first = true;

    while (linefeedPtr != NULL)
    {
        if (!first)
            logWrite(handle, indentBuffer, indentSize, errorDetail);
        else
            first = false;

        logWrite(handle, message, (size_t)(linefeedPtr - message + 1), errorDetail);
        message += (size_t)(linefeedPtr - message + 1);

        linefeedPtr = strchr(message, '\n');
    }

    FUNCTION_TEST_RESULT_VOID();
}

/***********************************************************************************************************************************
General log function
***********************************************************************************************************************************/
void
logInternal(
    LogLevel logLevel, LogLevel logRangeMin,  LogLevel logRangeMax, const char *fileName, const char *functionName, int code,
    const char *format, ...)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, logLevel);
        FUNCTION_TEST_PARAM(ENUM, logRangeMin);
        FUNCTION_TEST_PARAM(ENUM, logRangeMax);
        FUNCTION_TEST_PARAM(STRINGZ, fileName);
        FUNCTION_TEST_PARAM(STRINGZ, functionName);
        FUNCTION_TEST_PARAM(INT, code);
        FUNCTION_TEST_PARAM(STRINGZ, format);

        FUNCTION_TEST_ASSERT_LOG_LEVEL(logLevel);
        FUNCTION_TEST_ASSERT_LOG_LEVEL(logRangeMin);
        FUNCTION_TEST_ASSERT_LOG_LEVEL(logRangeMax);
        FUNCTION_TEST_ASSERT(logRangeMin <= logRangeMax);
        FUNCTION_TEST_ASSERT(fileName != NULL);
        FUNCTION_TEST_ASSERT(functionName != NULL);
        FUNCTION_TEST_ASSERT(
            (code == 0 && logLevel > logLevelError) || (logLevel == logLevelError && code != errorTypeCode(&AssertError)) ||
            (logLevel == logLevelAssert && code == errorTypeCode(&AssertError)));
        FUNCTION_TEST_ASSERT(format != NULL);
    FUNCTION_TEST_END();

    size_t bufferPos = 0;   // Current position in the buffer

    // Add time
    if (logTimestamp)
    {
        TimeMSec logTimeMSec = timeMSec();
        time_t logTimeSec = (time_t)(logTimeMSec / MSEC_PER_SEC);

        bufferPos += strftime(logBuffer + bufferPos, sizeof(logBuffer) - bufferPos, "%Y-%m-%d %H:%M:%S", localtime(&logTimeSec));
        bufferPos += (size_t)snprintf(
            logBuffer + bufferPos, sizeof(logBuffer) - bufferPos, ".%03d ", (int)(logTimeMSec % 1000));
    }

    // Add process and aligned log level
    bufferPos += (size_t)snprintf(logBuffer + bufferPos, sizeof(logBuffer) - bufferPos, "P00 %*s: ", 6, logLevelStr(logLevel));

    // When writing to stderr the timestamp, process, and log level alignment will be skipped
    char *logBufferStdErr = logBuffer + bufferPos - strlen(logLevelStr(logLevel)) - 2;

    // Set the indent size -- this will need to be adjusted for stderr
    size_t indentSize = bufferPos;

    // Add error code
    if (code != 0)
        bufferPos += (size_t)snprintf(logBuffer + bufferPos, sizeof(logBuffer) - bufferPos, "[%03d]: ", code);

    // Add debug info
    if (logLevel >= logLevelDebug)
    {
        // Adding padding for debug and trace levels
        for (unsigned int paddingIdx = 0; paddingIdx < ((logLevel - logLevelDebug + 1) * 4); paddingIdx++)
        {
            logBuffer[bufferPos++] = ' ';
            indentSize++;
        }

        bufferPos += (size_t)snprintf(
            logBuffer + bufferPos, LOG_BUFFER_SIZE - bufferPos, "%.*s::%s: ", (int)strlen(fileName) - 2, fileName,
            functionName);
    }

    // Format message -- this will need to be indented later
    va_list argumentList;
    va_start(argumentList, format);
    bufferPos += (size_t)vsnprintf(logBuffer + bufferPos, LOG_BUFFER_SIZE - bufferPos, format, argumentList);
    va_end(argumentList);

    // Add linefeed
    logBuffer[bufferPos++] = '\n';
    logBuffer[bufferPos] = 0;

    // Determine where to log the message based on log-level-stderr
    if (logWillStdErr(logLevel))
    {
        if (logRange(logLevelStdErr, logRangeMin, logRangeMax))
            logWriteIndent(logHandleStdErr, logBufferStdErr, indentSize - (size_t)(logBufferStdErr - logBuffer), "log to stderr");
    }
    else if (logWillStdOut(logLevel) && logRange(logLevelStdOut, logRangeMin, logRangeMax))
        logWriteIndent(logHandleStdOut, logBuffer, indentSize, "log to stdout");

    // Log to file
    if (logWillFile(logLevel) && logRange(logLevelFile, logRangeMin, logRangeMax))
    {
        // If the banner has not been written
        if (!logFileBanner)
        {
            // Add a blank line if the file already has content
            if (lseek(logHandleFile, 0, SEEK_END) > 0)
                logWrite(logHandleFile, "\n", 1, "banner spacing to file");

            // Write process start banner
            const char *banner = "-------------------PROCESS START-------------------\n";

            logWrite(logHandleFile, banner, strlen(banner), "banner to file");

            // Mark banner as written
            logFileBanner = true;
        }

        logWriteIndent(logHandleFile, logBuffer, indentSize, "log to file");
    }

    FUNCTION_TEST_RESULT_VOID();
}
