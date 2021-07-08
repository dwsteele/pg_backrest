/***********************************************************************************************************************************
Test Command Control
***********************************************************************************************************************************/
#include "common/harnessConfig.h"
#include "common/harnessFork.h"
#include "common/harnessStorage.h"
#include "common/io/fdRead.h"
#include "common/io/fdWrite.h"
#include "storage/posix/storage.h"

/***********************************************************************************************************************************
Test Run
***********************************************************************************************************************************/
void
testRun(void)
{
    FUNCTION_HARNESS_VOID();

    // Create default storage object for testing
    const Storage *const hrnStorage = storagePosixNewP(HRN_PATH_STR, .write = true);

    // *****************************************************************************************************************************
    if (testBegin("lockStopFileName()"))
    {
        // Load configuration so lock path is set
        StringList *argList = strLstNew();
        hrnCfgArgRawZ(argList, cfgOptStanza, "db");
        hrnCfgArgRawZ(argList, cfgOptPgPath, "/path/to/pg");
        HRN_CFG_LOAD(cfgCmdArchiveGet, argList);

        TEST_RESULT_STR_Z(lockStopFileName(NULL), HRN_PATH "/lock/all" STOP_FILE_EXT, "stop file for all stanzas");
        TEST_RESULT_STR_Z(lockStopFileName(STRDEF("db")), HRN_PATH "/lock/db" STOP_FILE_EXT, "stop file for one stanza");
    }

    // *****************************************************************************************************************************
    if (testBegin("lockStopTest(), cmdStart()"))
    {
        StringList *argList = strLstNew();
        HRN_CFG_LOAD(cfgCmdStart, argList);

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("no stanza, no stop file");

        TEST_RESULT_VOID(lockStopTest(), "no stop files without stanza");
        TEST_RESULT_VOID(cmdStart(), "cmdStart - no stanza, no stop files");
        TEST_RESULT_LOG("P00   WARN: stop file does not exist");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("no stanza, stop file exists");

        HRN_STORAGE_PUT_EMPTY(hrnStorage, "lock/all" STOP_FILE_EXT, .comment = "create stop file");
        TEST_RESULT_VOID(cmdStart(), "cmdStart - no stanza, stop file exists");
        TEST_STORAGE_LIST_EMPTY(hrnStorage, "lock", .comment = "stop file removed");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("stanza, no stop file");

        argList = strLstNew();
        hrnCfgArgRawZ(argList, cfgOptStanza, "db");
        HRN_CFG_LOAD(cfgCmdStart, argList);

        TEST_RESULT_VOID(lockStopTest(), "no stop files with stanza");
        TEST_RESULT_VOID(cmdStart(), "cmdStart - stanza, no stop files");
        TEST_RESULT_LOG("P00   WARN: stop file does not exist for stanza db");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("stanza, stop file exists");

        HRN_STORAGE_PUT_EMPTY(hrnStorage, "lock/all" STOP_FILE_EXT);
        TEST_ERROR(lockStopTest(), StopError, "stop file exists for all stanzas");

        HRN_STORAGE_PUT_EMPTY(hrnStorage, "lock/db" STOP_FILE_EXT);
        TEST_ERROR(lockStopTest(), StopError, "stop file exists for stanza db");

        TEST_RESULT_VOID(cmdStart(), "cmdStart - stanza, stop file exists");
        TEST_STORAGE_LIST(hrnStorage, "lock", "all" STOP_FILE_EXT "\n", .comment = "only stanza stop file removed");
    }

    // *****************************************************************************************************************************
    if (testBegin("cmdStop()"))
    {
        StringList *argList = strLstNew();
        HRN_CFG_LOAD(cfgCmdStop, argList);

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("path/file info");

        TEST_RESULT_VOID(cmdStop(), "no stanza, create stop file");
        StorageInfo info = {0};
        TEST_ASSIGN(info, storageInfoP(hrnStorage, STRDEF("lock")), "get path info");
        TEST_RESULT_INT(info.mode, 0770, "check path mode");
        TEST_STORAGE_EXISTS(hrnStorage, "lock/all" STOP_FILE_EXT, .comment = "all stop file created");
        TEST_ASSIGN(info, storageInfoP(hrnStorage, STRDEF("lock/all" STOP_FILE_EXT)), "get file info");
        TEST_RESULT_INT(info.mode, 0640, "check file mode");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("stop file already exists");

        TEST_RESULT_VOID(cmdStop(), "no stanza, stop file already exists");
        TEST_RESULT_LOG("P00   WARN: stop file already exists for all stanzas");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("stop file error");

        HRN_STORAGE_REMOVE(hrnStorage, "lock/all" STOP_FILE_EXT, .errorOnMissing = true, .comment = "remove stop file");
        HRN_STORAGE_MODE(hrnStorage, "lock", .mode = 0444);
        TEST_ERROR(
            cmdStop(), FileOpenError, "unable to get info for path/file '" HRN_PATH "/lock/all.stop': [13] Permission denied");
        HRN_STORAGE_MODE(hrnStorage, "lock", .mode = 0700);

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("stanza stop file create");

        hrnCfgArgRawZ(argList, cfgOptStanza, "db");
        HRN_CFG_LOAD(cfgCmdStop, argList);

        HRN_STORAGE_PATH_REMOVE(hrnStorage, "lock", .recurse = true, .errorOnMissing = true, .comment = "remove the lock path");
        TEST_RESULT_VOID(cmdStop(), "stanza, create stop file");
        TEST_STORAGE_LIST(
            hrnStorage, "lock", "db" STOP_FILE_EXT "\n",
            .comment = "lock path and file created, only stanza stop exists in lock path");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("stanza stop file already exists");

        TEST_RESULT_VOID(cmdStop(), "stanza, stop file already exists");
        TEST_RESULT_LOG("P00   WARN: stop file already exists for stanza db");
        HRN_STORAGE_REMOVE(hrnStorage, "lock/db" STOP_FILE_EXT, .errorOnMissing = true, .comment = "remove stanza stop file");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("stanza stop file create with force");

        hrnCfgArgRawBool(argList, cfgOptForce, true);
        HRN_CFG_LOAD(cfgCmdStop, argList);
        TEST_RESULT_VOID(cmdStop(), "stanza, create stop file, force");
        TEST_STORAGE_EXISTS(hrnStorage, "lock/db" STOP_FILE_EXT, .remove = true, .comment = "stanza stop file created, remove");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("unable to open lock file");

        HRN_STORAGE_PUT_EMPTY(
            hrnStorage, "lock/bad" LOCK_FILE_EXT, .modeFile = 0222, .comment = "create a lock file that cannot be opened");
        TEST_RESULT_VOID(cmdStop(), "stanza, create stop file but unable to open lock file");
        TEST_STORAGE_EXISTS(hrnStorage, "lock/db" STOP_FILE_EXT, .comment = "stanza stop file created");
        TEST_RESULT_LOG("P00   WARN: unable to open lock file " HRN_PATH "/lock/bad" LOCK_FILE_EXT);
        HRN_STORAGE_PATH_REMOVE(hrnStorage, "lock", .recurse = true, .errorOnMissing = true, .comment = "remove the lock path");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("lock file removal");

        HRN_STORAGE_PUT_EMPTY(hrnStorage, "lock/empty" LOCK_FILE_EXT, .comment = "create empty lock file");
        TEST_RESULT_VOID(cmdStop(), "stanza, create stop file, force - empty lock file");
        TEST_STORAGE_LIST(
            hrnStorage, "lock", "db" STOP_FILE_EXT "\n",
            .comment = "stanza stop file created, no other process lock, lock file was removed");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("empty lock file with another process lock, processId == NULL");

        HRN_STORAGE_REMOVE(hrnStorage, "lock/db" STOP_FILE_EXT, .errorOnMissing = true, .comment = "remove stanza stop file");
        HRN_STORAGE_PUT_EMPTY(hrnStorage, "lock/empty" LOCK_FILE_EXT, .comment = "create empty lock file");

        HARNESS_FORK_BEGIN()
        {
            HARNESS_FORK_CHILD_BEGIN(0, true)
            {
                IoRead *read = ioFdReadNew(STRDEF("child read"), HARNESS_FORK_CHILD_READ(), 2000);
                ioReadOpen(read);
                IoWrite *write = ioFdWriteNew(STRDEF("child write"), HARNESS_FORK_CHILD_WRITE(), 2000);
                ioWriteOpen(write);

                int lockFd = open(HRN_PATH "/lock/empty" LOCK_FILE_EXT, O_RDONLY, 0);
                TEST_RESULT_BOOL(lockFd != -1, true, "file descriptor acquired");
                TEST_RESULT_INT(flock(lockFd, LOCK_EX | LOCK_NB), 0, "lock the empty file");

                // Let the parent know the lock has been acquired and wait for the parent to allow lock release
                ioWriteStrLine(write, strNew());
                // All writes are buffered so need to flush because buffer is not full
                ioWriteFlush(write);
                // Wait for a linefeed from the parent ioWriteLine below
                ioReadLine(read);

                // Parent removed the file so just close the file descriptor
                close(lockFd);
            }
            HARNESS_FORK_CHILD_END();

            HARNESS_FORK_PARENT_BEGIN()
            {
                IoRead *read = ioFdReadNew(STRDEF("parent read"), HARNESS_FORK_PARENT_READ_PROCESS(0), 2000);
                ioReadOpen(read);
                IoWrite *write = ioFdWriteNew(STRDEF("parent write"), HARNESS_FORK_PARENT_WRITE_PROCESS(0), 2000);
                ioWriteOpen(write);

                // Wait for the child to acquire the lock
                ioReadLine(read);

                TEST_RESULT_VOID(
                    cmdStop(), "stanza, create stop file, force - empty lock file with another process lock, processId == NULL");
                TEST_STORAGE_LIST(
                    hrnStorage, "lock", "db" STOP_FILE_EXT "\n", .comment = "stop file created, lock file was removed");

                // Notify the child to release the lock
                ioWriteLine(write, bufNew(0));
                ioWriteFlush(write);
            }
            HARNESS_FORK_PARENT_END();
        }
        HARNESS_FORK_END();

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("not empty lock file with another process lock, processId size trimmed to 0");

        HRN_STORAGE_REMOVE(hrnStorage, "lock/db" STOP_FILE_EXT, .errorOnMissing = true, .comment = "remove stanza stop file");
        HRN_STORAGE_PUT_Z(hrnStorage, "lock/empty" LOCK_FILE_EXT, " ", .comment = "create non-empty lock file");

        HARNESS_FORK_BEGIN()
        {
            HARNESS_FORK_CHILD_BEGIN(0, true)
            {
                IoRead *read = ioFdReadNew(STRDEF("child read"), HARNESS_FORK_CHILD_READ(), 2000);
                ioReadOpen(read);
                IoWrite *write = ioFdWriteNew(STRDEF("child write"), HARNESS_FORK_CHILD_WRITE(), 2000);
                ioWriteOpen(write);

                int lockFd = open(HRN_PATH "/lock/empty" LOCK_FILE_EXT, O_RDONLY, 0);
                TEST_RESULT_BOOL(lockFd != -1, true, "file descriptor acquired");
                TEST_RESULT_INT(flock(lockFd, LOCK_EX | LOCK_NB), 0, "lock the non-empty file");

                // Let the parent know the lock has been acquired and wait for the parent to allow lock release
                ioWriteStrLine(write, strNew());
                // All writes are buffered so need to flush because buffer is not full
                ioWriteFlush(write);
                // Wait for a linefeed from the parent ioWriteLine below
                ioReadLine(read);

                // Parent removed the file so just close the file descriptor
                close(lockFd);
            }
            HARNESS_FORK_CHILD_END();

            HARNESS_FORK_PARENT_BEGIN()
            {
                IoRead *read = ioFdReadNew(STRDEF("parent read"), HARNESS_FORK_PARENT_READ_PROCESS(0), 2000);
                ioReadOpen(read);
                IoWrite *write = ioFdWriteNew(STRDEF("parent write"), HARNESS_FORK_PARENT_WRITE_PROCESS(0), 2000);
                ioWriteOpen(write);

                // Wait for the child to acquire the lock
                ioReadLine(read);

                TEST_RESULT_VOID(
                    cmdStop(), "stanza, create stop file, force - empty lock file with another process lock, processId size 0");
                TEST_STORAGE_LIST(
                    hrnStorage, "lock", "db" STOP_FILE_EXT "\n", .comment = "stop file created, lock file was removed");

                // Notify the child to release the lock
                ioWriteLine(write, bufNew(0));
                ioWriteFlush(write);
            }
            HARNESS_FORK_PARENT_END();
        }
        HARNESS_FORK_END();

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("lock file with another process lock, processId is valid");

        HRN_STORAGE_REMOVE(hrnStorage, "lock/db" STOP_FILE_EXT, .errorOnMissing = true, .comment = "remove stanza stop file");
        HARNESS_FORK_BEGIN()
        {
            HARNESS_FORK_CHILD_BEGIN(0, true)
            {
                IoRead *read = ioFdReadNew(STRDEF("child read"), HARNESS_FORK_CHILD_READ(), 2000);
                ioReadOpen(read);
                IoWrite *write = ioFdWriteNew(STRDEF("child write"), HARNESS_FORK_CHILD_WRITE(), 2000);
                ioWriteOpen(write);

                TEST_RESULT_BOOL(
                    lockAcquire(STRDEF(HRN_PATH "/lock"), cfgOptionStr(cfgOptStanza), cfgOptionStr(cfgOptExecId), 0, 30000, true),
                    true, "child process acquires lock");

                // Let the parent know the lock has been acquired and wait for the parent to allow lock release
                ioWriteStrLine(write, strNew());
                // All writes are buffered so need to flush because buffer is not full
                ioWriteFlush(write);
                // Wait for a linefeed from the parent but it will not arrive before the process is terminated
                ioReadLine(read);
            }
            HARNESS_FORK_CHILD_END();

            HARNESS_FORK_PARENT_BEGIN()
            {
                IoRead *read = ioFdReadNew(STRDEF("parent read"), HARNESS_FORK_PARENT_READ_PROCESS(0), 2000);
                ioReadOpen(read);
                IoWrite *write = ioFdWriteNew(STRDEF("parent write"), HARNESS_FORK_PARENT_WRITE_PROCESS(0), 2000);
                ioWriteOpen(write);

                // Wait for the child to acquire the lock
                ioReadLine(read);

                TEST_RESULT_VOID(
                    cmdStop(), "stanza, create stop file, force - lock file with another process lock, processId is valid");

                TEST_RESULT_LOG_FMT("P00   INFO: sent term signal to process %d", HARNESS_FORK_PROCESS_ID(0));
            }
            HARNESS_FORK_PARENT_END();
        }
        HARNESS_FORK_END();

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("lock file with another process lock, processId is invalid");

        HRN_STORAGE_REMOVE(hrnStorage, "lock/db" STOP_FILE_EXT, .errorOnMissing = true, .comment = "remove stanza stop file");
        HRN_STORAGE_PUT_Z(hrnStorage, "lock/badpid" LOCK_FILE_EXT, "-32768", .comment = "create lock file with invalid PID");

        HARNESS_FORK_BEGIN()
        {
            HARNESS_FORK_CHILD_BEGIN(0, true)
            {
                IoRead *read = ioFdReadNew(STRDEF("child read"), HARNESS_FORK_CHILD_READ(), 2000);
                ioReadOpen(read);
                IoWrite *write = ioFdWriteNew(STRDEF("child write"), HARNESS_FORK_CHILD_WRITE(), 2000);
                ioWriteOpen(write);

                int lockFd = open(HRN_PATH "/lock/badpid" LOCK_FILE_EXT, O_RDONLY, 0);
                TEST_RESULT_BOOL(lockFd != -1, true, "file descriptor acquired");
                TEST_RESULT_INT(flock(lockFd, LOCK_EX | LOCK_NB), 0, "lock the badpid file");

                // Let the parent know the lock has been acquired and wait for the parent to allow lock release
                ioWriteStrLine(write, strNew());
                // All writes are buffered so need to flush because buffer is not full
                ioWriteFlush(write);
                // Wait for a linefeed from the parent ioWriteLine below
                ioReadLine(read);

                // Remove the file and close the file descriptor
                HRN_STORAGE_REMOVE(hrnStorage, "lock/badpid" LOCK_FILE_EXT);
                close(lockFd);
            }
            HARNESS_FORK_CHILD_END();

            HARNESS_FORK_PARENT_BEGIN()
            {
                IoRead *read = ioFdReadNew(STRDEF("parent read"), HARNESS_FORK_PARENT_READ_PROCESS(0), 2000);
                ioReadOpen(read);
                IoWrite *write = ioFdWriteNew(STRDEF("parent write"), HARNESS_FORK_PARENT_WRITE_PROCESS(0), 2000);
                ioWriteOpen(write);

                // Wait for the child to acquire the lock
                ioReadLine(read);

                TEST_RESULT_VOID(
                    cmdStop(), "stanza, create stop file, force - lock file with another process lock, processId is invalid");
                TEST_RESULT_LOG("P00   WARN: unable to send term signal to process -32768");
                TEST_STORAGE_EXISTS(hrnStorage, "lock/db" STOP_FILE_EXT, .comment = "stanza stop file not removed");

                // Notify the child to release the lock
                ioWriteLine(write, bufNew(0));
                ioWriteFlush(write);
            }
            HARNESS_FORK_PARENT_END();
        }
        HARNESS_FORK_END();
    }

    FUNCTION_HARNESS_RETURN_VOID();
}
