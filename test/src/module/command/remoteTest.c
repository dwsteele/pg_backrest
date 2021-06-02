/***********************************************************************************************************************************
Test Remote Command
***********************************************************************************************************************************/
#include "common/io/fdRead.h"
#include "common/io/fdWrite.h"
#include "protocol/client.h"
#include "protocol/server.h"
#include "storage/posix/storage.h"

#include "common/harnessConfig.h"
#include "common/harnessFork.h"

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
    if (testBegin("cmdRemote()"))
    {
        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("no lock is required because process is > 0 (not the main remote)");

        HARNESS_FORK_BEGIN()
        {
            HARNESS_FORK_CHILD_BEGIN(0, true)
            {
                StringList *argList = strLstNew();
                strLstAddZ(argList, "--stanza=test1");
                strLstAddZ(argList, "--process=1");
                hrnCfgArgRawStrId(argList, cfgOptRemoteType, protocolStorageTypeRepo);
                HRN_CFG_LOAD(cfgCmdInfo, argList, .role = cfgCmdRoleRemote);

                cmdRemote(HARNESS_FORK_CHILD_READ(), HARNESS_FORK_CHILD_WRITE());
            }
            HARNESS_FORK_CHILD_END();

            HARNESS_FORK_PARENT_BEGIN()
            {
                IoRead *read = ioFdReadNew(STRDEF("server read"), HARNESS_FORK_PARENT_READ_PROCESS(0), 2000);
                ioReadOpen(read);
                IoWrite *write = ioFdWriteNew(STRDEF("server write"), HARNESS_FORK_PARENT_WRITE_PROCESS(0), 2000);
                ioWriteOpen(write);

                ProtocolClient *client = protocolClientNew(STRDEF("test"), PROTOCOL_SERVICE_REMOTE_STR, read, write);
                protocolClientNoOp(client);
                protocolClientFree(client);
            }
            HARNESS_FORK_PARENT_END();
        }
        HARNESS_FORK_END();

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("no remote lock is required for this command");

        HARNESS_FORK_BEGIN()
        {
            HARNESS_FORK_CHILD_BEGIN(0, true)
            {
                StringList *argList = strLstNew();
                strLstAddZ(argList, "--process=0");
                hrnCfgArgRawStrId(argList, cfgOptRemoteType, protocolStorageTypeRepo);
                strLstAddZ(argList, "--lock-path=/bogus");
                strLstAddZ(argList, "--" CFGOPT_STANZA "=test");
                hrnCfgArgRawZ(argList, cfgOptPgPath, "/path/to/pg");
                HRN_CFG_LOAD(cfgCmdArchiveGet, argList, .role = cfgCmdRoleRemote, .noStd = true);

                cmdRemote(HARNESS_FORK_CHILD_READ(), HARNESS_FORK_CHILD_WRITE());
            }
            HARNESS_FORK_CHILD_END();

            HARNESS_FORK_PARENT_BEGIN()
            {
                IoRead *read = ioFdReadNew(STRDEF("server read"), HARNESS_FORK_PARENT_READ_PROCESS(0), 2000);
                ioReadOpen(read);
                IoWrite *write = ioFdWriteNew(STRDEF("server write"), HARNESS_FORK_PARENT_WRITE_PROCESS(0), 2000);
                ioWriteOpen(write);

                ProtocolClient *client = NULL;
                TEST_ASSIGN(client, protocolClientNew(STRDEF("test"), PROTOCOL_SERVICE_REMOTE_STR, read, write), "create client");
                protocolClientNoOp(client);
                protocolClientFree(client);
            }
            HARNESS_FORK_PARENT_END();
        }
        HARNESS_FORK_END();

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("remote lock is required but lock path is invalid");

        HARNESS_FORK_BEGIN()
        {
            HARNESS_FORK_CHILD_BEGIN(0, true)
            {
                StringList *argList = strLstNew();
                strLstAddZ(argList, "--stanza=test");
                strLstAddZ(argList, "--process=0");
                hrnCfgArgRawStrId(argList, cfgOptRemoteType, protocolStorageTypeRepo);
                strLstAddZ(argList, "--lock-path=/bogus");
                HRN_CFG_LOAD(cfgCmdArchivePush, argList, .role = cfgCmdRoleRemote, .noStd = true);

                cmdRemote(HARNESS_FORK_CHILD_READ(), HARNESS_FORK_CHILD_WRITE());
            }
            HARNESS_FORK_CHILD_END();

            HARNESS_FORK_PARENT_BEGIN()
            {
                IoRead *read = ioFdReadNew(STRDEF("server read"), HARNESS_FORK_PARENT_READ_PROCESS(0), 2000);
                ioReadOpen(read);
                IoWrite *write = ioFdWriteNew(STRDEF("server write"), HARNESS_FORK_PARENT_WRITE_PROCESS(0), 2000);
                ioWriteOpen(write);

                TEST_ERROR(
                    protocolClientNew(STRDEF("test"), PROTOCOL_SERVICE_REMOTE_STR, read, write), PathCreateError,
                    "raised from test: unable to create path '/bogus': [13] Permission denied");
            }
            HARNESS_FORK_PARENT_END();
        }
        HARNESS_FORK_END();

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("remote lock is required and succeeds");

        HARNESS_FORK_BEGIN()
        {
            HARNESS_FORK_CHILD_BEGIN(0, true)
            {
                StringList *argList = strLstNew();
                strLstAddZ(argList, "--stanza=test");
                strLstAddZ(argList, "--process=0");
                hrnCfgArgRawStrId(argList, cfgOptRemoteType, protocolStorageTypeRepo);
                hrnCfgArgRawZ(argList, cfgOptRepo, "1");
                HRN_CFG_LOAD(cfgCmdArchivePush, argList, .role = cfgCmdRoleRemote);

                cmdRemote(HARNESS_FORK_CHILD_READ(), HARNESS_FORK_CHILD_WRITE());
            }
            HARNESS_FORK_CHILD_END();

            HARNESS_FORK_PARENT_BEGIN()
            {
                IoRead *read = ioFdReadNew(STRDEF("server read"), HARNESS_FORK_PARENT_READ_PROCESS(0), 2000);
                ioReadOpen(read);
                IoWrite *write = ioFdWriteNew(STRDEF("server write"), HARNESS_FORK_PARENT_WRITE_PROCESS(0), 2000);
                ioWriteOpen(write);

                ProtocolClient *client = NULL;
                TEST_ASSIGN(client, protocolClientNew(STRDEF("test"), PROTOCOL_SERVICE_REMOTE_STR, read, write), "create client");
                protocolClientNoOp(client);

                TEST_RESULT_BOOL(
                    storageExistsP(storagePosixNewP(HRN_PATH_STR), STRDEF("lock/test-archive" LOCK_FILE_EXT)),
                    true, "lock exists");

                protocolClientFree(client);
            }
            HARNESS_FORK_PARENT_END();
        }
        HARNESS_FORK_END();

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("remote lock is required but stop file exists");

        HARNESS_FORK_BEGIN()
        {
            HARNESS_FORK_CHILD_BEGIN(0, true)
            {
                StringList *argList = strLstNew();
                strLstAddZ(argList, "--stanza=test");
                strLstAddZ(argList, "--process=0");
                hrnCfgArgRawStrId(argList, cfgOptRemoteType, protocolStorageTypeRepo);
                HRN_CFG_LOAD(cfgCmdArchivePush, argList, .role = cfgCmdRoleRemote);

                cmdRemote(HARNESS_FORK_CHILD_READ(), HARNESS_FORK_CHILD_WRITE());
            }
            HARNESS_FORK_CHILD_END();

            HARNESS_FORK_PARENT_BEGIN()
            {
                IoRead *read = ioFdReadNew(STRDEF("server read"), HARNESS_FORK_PARENT_READ_PROCESS(0), 2000);
                ioReadOpen(read);
                IoWrite *write = ioFdWriteNew(STRDEF("server write"), HARNESS_FORK_PARENT_WRITE_PROCESS(0), 2000);
                ioWriteOpen(write);

                storagePutP(storageNewWriteP(hrnStorage, STRDEF("lock/all" STOP_FILE_EXT)), NULL);

                TEST_ERROR(
                    protocolClientNew(STRDEF("test"), PROTOCOL_SERVICE_REMOTE_STR, read, write), StopError,
                    "raised from test: stop file exists for all stanzas");

                storageRemoveP(hrnStorage, STRDEF("lock/all" STOP_FILE_EXT));
            }
            HARNESS_FORK_PARENT_END();
        }
        HARNESS_FORK_END();
    }

    FUNCTION_HARNESS_RETURN_VOID();
}
