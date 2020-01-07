/***********************************************************************************************************************************
C Test Harness
***********************************************************************************************************************************/
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "common/harnessDebug.h"
#include "common/harnessTest.h"
#include "common/harnessLog.h"

#define TEST_LIST_SIZE                                              64

typedef struct TestData
{
    bool selected;
} TestData;

static TestData testList[TEST_LIST_SIZE];

static int testRun = 0;
static int testTotal = 0;
static bool testFirst = true;

static uint64_t timeMSecBegin;

static const char *testExeData = NULL;
static const char *testProjectExeData = NULL;
static bool testContainerData = false;
static unsigned int testIdxData = 0;
static uint64_t testScaleData = 1;
static const char *testPathData = NULL;
static const char *testDataPathData = NULL;
static const char *testRepoPathData = NULL;

static char testUserIdData[32];
static char testUserData[64];
static char testGroupIdData[32];
static char testGroupData[64];

/***********************************************************************************************************************************
Extern functions
***********************************************************************************************************************************/
#ifndef NO_LOG
    void harnessLogInit(void);
    void harnessLogFinal(void);
#endif

/***********************************************************************************************************************************
Initialize harness
***********************************************************************************************************************************/
void
hrnInit(
    const char *testExe, const char *testProjectExe, bool testContainer, unsigned int testIdx, uint64_t testScale,
    const char *testPath, const char *testDataPath, const char *testRepoPath)
{
    FUNCTION_HARNESS_VOID();

    // Set test configuration
    testExeData = testExe;
    testProjectExeData = testProjectExe;
    testContainerData = testContainer;
    testIdxData = testIdx;
    testScaleData = testScale;
    testPathData = testPath;
    testDataPathData = testDataPath;
    testRepoPathData = testRepoPath;

    // Set test user id
    snprintf(testUserIdData, sizeof(testUserIdData), "%u", getuid());

    // Set test user
    const char *testUserTemp = getpwuid(getuid())->pw_name;

    if (strlen(testUserTemp) > sizeof(testUserData) - 1)
    {
        fprintf(stderr, "ERROR: test user name must be less than %zu characters", sizeof(testUserData) - 1);
        fflush(stderr);
        exit(255);
    }

    strcpy(testUserData, testUserTemp);

    // Set test group id
    snprintf(testGroupIdData, sizeof(testGroupIdData), "%u", getgid());

    // Set test group
    const char *testGroupTemp = getgrgid(getgid())->gr_name;

    if (strlen(testGroupTemp) > sizeof(testGroupData) - 1)
    {
        fprintf(stderr, "ERROR: test group name must be less than %zu characters", sizeof(testGroupData) - 1);
        fflush(stderr);
        exit(255);
    }

    strcpy(testGroupData, testGroupTemp);

    FUNCTION_HARNESS_RESULT_VOID();
}

/***********************************************************************************************************************************
testAdd - add a new test
***********************************************************************************************************************************/
void
hrnAdd(int run, bool selected)
{
    FUNCTION_HARNESS_BEGIN();
        FUNCTION_HARNESS_PARAM(INT, run);
        FUNCTION_HARNESS_PARAM(BOOL, selected);
    FUNCTION_HARNESS_END();

    if (run != testTotal + 1)
    {
        fprintf(stderr, "ERROR: test run %d is not in order\n", run);
        fflush(stderr);
        exit(255);
    }

    testList[testTotal].selected = selected;
    testTotal++;

    FUNCTION_HARNESS_RESULT_VOID();
}

/***********************************************************************************************************************************
testBegin - should this test run?
***********************************************************************************************************************************/
bool
testBegin(const char *name)
{
    FUNCTION_HARNESS_BEGIN();
        FUNCTION_HARNESS_PARAM(STRINGZ, name);

        FUNCTION_HARNESS_ASSERT(name != NULL);
    FUNCTION_HARNESS_END();

    bool result = false;
    testRun++;

    if (testList[testRun - 1].selected)
    {
#ifndef NO_LOG
        if (!testFirst)
        {
            // Make sure there is nothing untested left in the log
            harnessLogFinal();

            // Clear out the test directory so the next test starts clean
            char buffer[2048];
            snprintf(buffer, sizeof(buffer), "%srm -rf %s/" "*", testContainer() ? "sudo " : "", testPath());

            if (system(buffer) != 0)
            {
                fprintf(stderr, "ERROR: unable to clear test path '%s'\n", testPath());
                fflush(stderr);
                exit(255);
            }

            // Clear out the data directory so the next test starts clean
            snprintf(buffer, sizeof(buffer), "%srm -rf %s/" "*", testContainer() ? "sudo " : "", testDataPath());

            if (system(buffer) != 0)
            {
                fprintf(stderr, "ERROR: unable to clear data path '%s'\n", testDataPath());
                fflush(stderr);
                exit(255);
            }

            // Clear any log replacements
            hrnLogReplaceClear();
        }
#endif
        // No longer the first test
        testFirst = false;

        if (testRun != 1)
            printf("\n");

        printf("run %03d - %s\n", testRun, name);
        fflush(stdout);

        timeMSecBegin = testTimeMSec();

#ifndef NO_LOG
        // Initialize logging
        harnessLogInit();
#endif

        result = true;
    }

    FUNCTION_HARNESS_RESULT(BOOL, result);
}

/***********************************************************************************************************************************
testComplete - make sure all expected tests ran
***********************************************************************************************************************************/
void
hrnComplete(void)
{
    FUNCTION_HARNESS_VOID();

#ifndef NO_LOG
    // Make sure there is nothing untested left in the log
    harnessLogFinal();
#endif

    // Check that all tests ran
    if (testRun != testTotal)
    {
        fprintf(stderr, "ERROR: expected %d tests but %d were run\n", testTotal, testRun);
        fflush(stderr);
        exit(255);
    }

    FUNCTION_HARNESS_RESULT_VOID();
}

/***********************************************************************************************************************************
Replace a substring with another string
***********************************************************************************************************************************/
static void
hrnReplaceStr(char *string, size_t bufferSize, const char *substring, const char *replace)
{
    FUNCTION_HARNESS_BEGIN();
        FUNCTION_HARNESS_PARAM(STRINGZ, string);
        FUNCTION_HARNESS_PARAM(SIZE, bufferSize);
        FUNCTION_HARNESS_PARAM(STRINGZ, substring);
        FUNCTION_HARNESS_PARAM(STRINGZ, replace);
    FUNCTION_HARNESS_END();

    ASSERT(string != NULL);
    ASSERT(substring != NULL);

    // Find substring
    char *begin = strstr(string, substring);

    while (begin != NULL)
    {
        // Find end of substring and calculate replace size difference
        char *end = begin + strlen(substring);
        int diff = (int)strlen(replace) - (int)strlen(substring);

        // Make sure we won't overflow the buffer
        CHECK((size_t)((int)strlen(string) + diff) < bufferSize - 1);

        // Move data from end of string enough to make room for the replacement and copy replacement
        memmove(end + diff, end, strlen(end) + 1);
        memcpy(begin, replace, strlen(replace));

        // Find next substring
        begin = strstr(begin + strlen(replace), substring);
    }

    FUNCTION_HARNESS_RESULT_VOID();
}

/**********************************************************************************************************************************/
char harnessReplaceKeyBuffer[256 * 1024];

const char *
hrnReplaceKey(const char *string)
{
    FUNCTION_HARNESS_BEGIN();
        FUNCTION_HARNESS_PARAM(STRINGZ, string);
    FUNCTION_HARNESS_END();

    ASSERT(string != NULL);

    // Make sure we won't overflow the buffer
    ASSERT(strlen(string) < sizeof(harnessReplaceKeyBuffer) - 1);

    strcpy(harnessReplaceKeyBuffer, string);
    hrnReplaceStr(harnessReplaceKeyBuffer, sizeof(harnessReplaceKeyBuffer), "{[path]}", testPath());
    hrnReplaceStr(harnessReplaceKeyBuffer, sizeof(harnessReplaceKeyBuffer), "{[path-data]}", testDataPath());
    hrnReplaceStr(harnessReplaceKeyBuffer, sizeof(harnessReplaceKeyBuffer), "{[user-id]}", testUserIdData);
    hrnReplaceStr(harnessReplaceKeyBuffer, sizeof(harnessReplaceKeyBuffer), "{[user]}", testUser());
    hrnReplaceStr(harnessReplaceKeyBuffer, sizeof(harnessReplaceKeyBuffer), "{[group-id]}", testGroupIdData);
    hrnReplaceStr(harnessReplaceKeyBuffer, sizeof(harnessReplaceKeyBuffer), "{[group]}", testGroup());
    hrnReplaceStr(harnessReplaceKeyBuffer, sizeof(harnessReplaceKeyBuffer), "{[project-exe]}", testProjectExe());

    FUNCTION_HARNESS_RESULT(STRINGZ, harnessReplaceKeyBuffer);
}

/**********************************************************************************************************************************/
void
hrnFileRead(const char *fileName, unsigned char *buffer, size_t bufferSize)
{
    int result = open(fileName, O_RDONLY, 0660);

    if (result == -1)
    {
        fprintf(stderr, "ERROR: unable to open '%s' for read\n", fileName);
        fflush(stderr);
        exit(255);
    }

    ssize_t bufferRead = read(result, buffer, bufferSize);

    if (bufferRead == -1)
    {
        fprintf(stderr, "ERROR: unable to read '%s'\n", fileName);
        fflush(stderr);
        exit(255);
    }

    buffer[bufferRead] = 0;

    close(result);
}

/**********************************************************************************************************************************/
void
hrnFileWrite(const char *fileName, const unsigned char *buffer, size_t bufferSize)
{
    int result = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0660);

    if (result == -1)
    {
        fprintf(stderr, "ERROR: unable to open '%s' for write\n", fileName);
        fflush(stderr);
        exit(255);
    }

    if (write(result, buffer, bufferSize) != (int)bufferSize)
    {
        fprintf(stderr, "ERROR: unable to write '%s'\n", fileName);
        fflush(stderr);
        exit(255);
    }

    close(result);
}

/**********************************************************************************************************************************/
char harnessDiffBuffer[256 * 1024];

const char *
hrnDiff(const char *expected, const char *actual)
{
    FUNCTION_HARNESS_BEGIN();
        FUNCTION_HARNESS_PARAM(STRINGZ, expected);
        FUNCTION_HARNESS_PARAM(STRINGZ, actual);
    FUNCTION_HARNESS_END();

    ASSERT(actual != NULL);

    // Write expected file
    char expectedFile[1024];
    snprintf(expectedFile, sizeof(expectedFile), "%s/diff.expected", testDataPath());
    hrnFileWrite(expectedFile, (unsigned char *)expected, strlen(expected));

    // Write actual file
    char actualFile[1024];
    snprintf(actualFile, sizeof(actualFile), "%s/diff.actual", testDataPath());
    hrnFileWrite(actualFile, (unsigned char *)actual, strlen(actual));

    // Perform diff
    char command[2560];
    snprintf(command, sizeof(command), "diff -u %s %s > %s/diff.result", expectedFile, actualFile, testDataPath());

    if (system(command) == 2)
    {
        fprintf(stderr, "ERROR: unable to execute '%s'\n", command);
        fflush(stderr);
        exit(255);
    }

    // Read result
    char resultFile[1024];
    snprintf(resultFile, sizeof(resultFile), "%s/diff.result", testDataPath());
    hrnFileRead(resultFile, (unsigned char *)harnessDiffBuffer, sizeof(harnessDiffBuffer));

    // Remove last linefeed from diff output
    harnessDiffBuffer[strlen(harnessDiffBuffer) - 1] = 0;

    FUNCTION_HARNESS_RESULT(STRINGZ, harnessDiffBuffer);
}

/***********************************************************************************************************************************
Getters
***********************************************************************************************************************************/
const char *
testExe(void)
{
    FUNCTION_HARNESS_VOID();
    FUNCTION_HARNESS_RESULT(STRINGZ, testExeData);
}

/**********************************************************************************************************************************/
const char *
testProjectExe(void)
{
    FUNCTION_HARNESS_VOID();
    FUNCTION_HARNESS_RESULT(STRINGZ, testProjectExeData);
}

/**********************************************************************************************************************************/
bool
testContainer(void)
{
    FUNCTION_HARNESS_VOID();
    FUNCTION_HARNESS_RESULT(BOOL, testContainerData);
}

/**********************************************************************************************************************************/
unsigned int
testIdx(void)
{
    FUNCTION_HARNESS_VOID();
    FUNCTION_HARNESS_RESULT(UINT, testIdxData);
}

/**********************************************************************************************************************************/
uint64_t
testScale(void)
{
    FUNCTION_HARNESS_VOID();
    FUNCTION_HARNESS_RESULT(UINT64, testScaleData);
}

/**********************************************************************************************************************************/
const char *
testPath(void)
{
    FUNCTION_HARNESS_VOID();
    FUNCTION_HARNESS_RESULT(STRINGZ, testPathData);
}

/**********************************************************************************************************************************/
const char *
testDataPath(void)
{
    FUNCTION_HARNESS_VOID();
    FUNCTION_HARNESS_RESULT(STRINGZ, testDataPathData);
}

/**********************************************************************************************************************************/
const char *
testRepoPath(void)
{
    FUNCTION_HARNESS_VOID();
    FUNCTION_HARNESS_RESULT(STRINGZ, testRepoPathData);
}

/**********************************************************************************************************************************/
const char *
testUser(void)
{
    return testUserData;
}

/**********************************************************************************************************************************/
const char *
testGroup(void)
{
    return testGroupData;
}

/**********************************************************************************************************************************/
uint64_t
testTimeMSec(void)
{
    FUNCTION_HARNESS_VOID();

    struct timeval currentTime;
    gettimeofday(&currentTime, NULL);

    FUNCTION_HARNESS_RESULT(UINT64, ((uint64_t)currentTime.tv_sec * 1000) + (uint64_t)currentTime.tv_usec / 1000);
}

/**********************************************************************************************************************************/
uint64_t
testTimeMSecBegin(void)
{
    FUNCTION_HARNESS_VOID();

    FUNCTION_HARNESS_RESULT(UINT64, timeMSecBegin);
}
