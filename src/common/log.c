/***********************************************************************************************************************************
Log Handler
***********************************************************************************************************************************/
#include "build.auto.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

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
DEBUG_UNIT_EXTERN LogLevel logLevelAny = logLevelError;

// Log file handles
DEBUG_UNIT_EXTERN int logHandleStdOut = STDOUT_FILENO;
DEBUG_UNIT_EXTERN int logHandleStdErr = STDERR_FILENO;
DEBUG_UNIT_EXTERN int logHandleFile = -1;

// Has the log file banner been written yet?
DEBUG_UNIT_EXTERN bool logFileBanner = false;

// Is the timestamp printed in the log?
DEBUG_UNIT_EXTERN bool logTimestamp = false;

// Size of the process id field
DEBUG_UNIT_EXTERN int logProcessSize = 2;

/***********************************************************************************************************************************
Test Asserts
***********************************************************************************************************************************/
#define ASSERT_LOG_LEVEL(logLevel)                                                                                                 \
    ASSERT(logLevel >= LOG_LEVEL_MIN && logLevel <= LOG_LEVEL_MAX)

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
    FUNCTION_TEST_END();

    ASSERT(logLevel != NULL);

    LogLevel result = logLevelOff;

    // Search for the log level
    for (; result < LOG_LEVEL_TOTAL; result++)
        if (strcasecmp(logLevel, logLevelList[result]) == 0)
            break;

    // If the log level was not found
    if (result == LOG_LEVEL_TOTAL)
        THROW_FMT(AssertError, "log level '%s' not found", logLevel);

    FUNCTION_TEST_RETURN(result);
}

const char *
logLevelStr(LogLevel logLevel)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, logLevel);
    FUNCTION_TEST_END();

    ASSERT(logLevel <= LOG_LEVEL_MAX);

    FUNCTION_TEST_RETURN(logLevelList[logLevel]);
}

/***********************************************************************************************************************************
Check if a log level will be logged to any output

This is useful for log messages that are expensive to generate and should be skipped if they will be discarded.
***********************************************************************************************************************************/
DEBUG_UNIT_EXTERN void
logAnySet(void)
{
    FUNCTION_TEST_VOID();

    logLevelAny = logLevelStdOut;

    if (logLevelStdErr > logLevelAny)
        logLevelAny = logLevelStdErr;

    if (logLevelFile > logLevelAny && logHandleFile != -1)
        logLevelAny = logLevelFile;

    FUNCTION_TEST_RETURN_VOID();
}

bool
logAny(LogLevel logLevel)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, logLevel);
    FUNCTION_TEST_END();

    ASSERT_LOG_LEVEL(logLevel);

    FUNCTION_TEST_RETURN(logLevel <= logLevelAny);
}

/***********************************************************************************************************************************
Initialize the log system
***********************************************************************************************************************************/
void
logInit(
    LogLevel logLevelStdOutParam, LogLevel logLevelStdErrParam, LogLevel logLevelFileParam, bool logTimestampParam,
    unsigned int logProcessMax)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, logLevelStdOutParam);
        FUNCTION_TEST_PARAM(ENUM, logLevelStdErrParam);
        FUNCTION_TEST_PARAM(ENUM, logLevelFileParam);
        FUNCTION_TEST_PARAM(BOOL, logTimestampParam);
    FUNCTION_TEST_END();

    ASSERT(logLevelStdOutParam <= LOG_LEVEL_MAX);
    ASSERT(logLevelStdErrParam <= LOG_LEVEL_MAX);
    ASSERT(logLevelFileParam <= LOG_LEVEL_MAX);
    ASSERT(logProcessMax <= 999);

    logLevelStdOut = logLevelStdOutParam;
    logLevelStdErr = logLevelStdErrParam;
    logLevelFile = logLevelFileParam;
    logTimestamp = logTimestampParam;
    logProcessSize = logProcessMax > 99 ? 3 : 2;

    logAnySet();

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Close the log file
***********************************************************************************************************************************/
static void
logFileClose(void)
{
    FUNCTION_TEST_VOID();

    // Close the file handle if it is open
    if (logHandleFile != -1)
    {
        close(logHandleFile);
        logHandleFile = -1;
    }

    logAnySet();

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Set the log file

Returns true if file logging is off or the log file was successfully opened, false if the log file could not be opened.
***********************************************************************************************************************************/
bool
logFileSet(const char *logFile)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRINGZ, logFile);
    FUNCTION_TEST_END();

    ASSERT(logFile != NULL);

    // Close the log file if it is already open
    logFileClose();

    // Only open the file if there is a chance to log something
    bool result = true;

    if (logLevelFile != logLevelOff)
    {
        // Open the file and handle errors
        logHandleFile = open(logFile, O_CREAT | O_APPEND | O_WRONLY, 0750);

        if (logHandleFile == -1)
        {
            int errNo = errno;
            LOG_WARN("unable to open log file '%s': %s\nNOTE: process will continue without log file.", logFile, strerror(errNo));
            result = false;
        }

        // Output the banner on first log message
        logFileBanner = false;

        logAnySet();
    }

    logAnySet();

    FUNCTION_TEST_RETURN(result);
}

/***********************************************************************************************************************************
Close the log system
***********************************************************************************************************************************/
void
logClose(void)
{
    FUNCTION_TEST_VOID();

    // Disable all logging
    logInit(logLevelOff, logLevelOff, logLevelOff, false, 1);

    // Close the log file if it is open
    logFileClose();

    FUNCTION_TEST_RETURN_VOID();
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
    FUNCTION_TEST_END();

    ASSERT_LOG_LEVEL(logLevel);
    ASSERT_LOG_LEVEL(logRangeMin);
    ASSERT_LOG_LEVEL(logRangeMax);
    ASSERT(logRangeMin <= logRangeMax);

    FUNCTION_TEST_RETURN(logLevel >= logRangeMin && logLevel <= logRangeMax);
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
    FUNCTION_TEST_END();

    ASSERT(handle != -1);
    ASSERT(message != NULL);
    ASSERT(messageSize != 0);
    ASSERT(errorDetail != NULL);

    if ((size_t)write(handle, message, messageSize) != messageSize)
        THROW_SYS_ERROR_FMT(FileWriteError, "unable to write %s", errorDetail);

    FUNCTION_TEST_RETURN_VOID();
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
    FUNCTION_TEST_END();

    ASSERT(handle != -1);
    ASSERT(message != NULL);
    ASSERT(indentSize > 0 && indentSize < sizeof(indentBuffer));
    ASSERT(errorDetail != NULL);

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

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
General log function
***********************************************************************************************************************************/
void
logInternal(
    LogLevel logLevel, LogLevel logRangeMin,  LogLevel logRangeMax, unsigned int processId, const char *fileName,
    const char *functionName, int code, const char *format, ...)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, logLevel);
        FUNCTION_TEST_PARAM(ENUM, logRangeMin);
        FUNCTION_TEST_PARAM(ENUM, logRangeMax);
        FUNCTION_TEST_PARAM(STRINGZ, fileName);
        FUNCTION_TEST_PARAM(STRINGZ, functionName);
        FUNCTION_TEST_PARAM(INT, code);
        FUNCTION_TEST_PARAM(STRINGZ, format);
    FUNCTION_TEST_END();

    ASSERT_LOG_LEVEL(logLevel);
    ASSERT_LOG_LEVEL(logRangeMin);
    ASSERT_LOG_LEVEL(logRangeMax);
    ASSERT(logRangeMin <= logRangeMax);
    ASSERT(fileName != NULL);
    ASSERT(functionName != NULL);
    ASSERT(
        (code == 0 && logLevel > logLevelError) || (logLevel == logLevelError && code != errorTypeCode(&AssertError)) ||
        (logLevel == logLevelAssert && code == errorTypeCode(&AssertError)));
    ASSERT(format != NULL);

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
    bufferPos += (size_t)snprintf(
        logBuffer + bufferPos, sizeof(logBuffer) - bufferPos, "P%0*u %*s: ", logProcessSize, processId, 6, logLevelStr(logLevel));

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
    if (logLevel <= logLevelStdErr)
    {
        if (logRange(logLevelStdErr, logRangeMin, logRangeMax))
            logWriteIndent(logHandleStdErr, logBufferStdErr, indentSize - (size_t)(logBufferStdErr - logBuffer), "log to stderr");
    }
    else if (logLevel <= logLevelStdOut && logRange(logLevelStdOut, logRangeMin, logRangeMax))
        logWriteIndent(logHandleStdOut, logBuffer, indentSize, "log to stdout");

    // Log to file
    if (logLevel <= logLevelFile && logHandleFile != -1 && logRange(logLevelFile, logRangeMin, logRangeMax))
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

    FUNCTION_TEST_RETURN_VOID();
}
