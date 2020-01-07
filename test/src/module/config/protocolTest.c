/***********************************************************************************************************************************
Test Configuration Protocol
***********************************************************************************************************************************/
#include "common/io/handleRead.h"
#include "common/io/handleWrite.h"
#include "protocol/client.h"
#include "protocol/server.h"

#include "common/harnessConfig.h"
#include "common/harnessFork.h"

/***********************************************************************************************************************************
Test run
***********************************************************************************************************************************/
void
testRun(void)
{
    FUNCTION_HARNESS_VOID();

    // *****************************************************************************************************************************
    if (testBegin("configProtocol() and configProtocolOption()"))
    {
        HARNESS_FORK_BEGIN()
        {
            HARNESS_FORK_CHILD_BEGIN(0, true)
            {
                IoRead *read = ioHandleReadNew(strNew("client read"), HARNESS_FORK_CHILD_READ(), 2000);
                ioReadOpen(read);
                IoWrite *write = ioHandleWriteNew(strNew("client write"), HARNESS_FORK_CHILD_WRITE());
                ioWriteOpen(write);

                StringList *argList = strLstNew();
                strLstAddZ(argList, "--stanza=test1");
                strLstAddZ(argList, "--repo1-host=repo-host");
                strLstAddZ(argList, "--repo1-host-user=repo-host-user");
                harnessCfgLoad(cfgCmdArchiveGet, argList);

                ProtocolServer *server = protocolServerNew(strNew("test"), strNew("config"), read, write);
                protocolServerHandlerAdd(server, configProtocol);
                protocolServerProcess(server);
            }
            HARNESS_FORK_CHILD_END();

            HARNESS_FORK_PARENT_BEGIN()
            {
                IoRead *read = ioHandleReadNew(strNew("server read"), HARNESS_FORK_PARENT_READ_PROCESS(0), 2000);
                ioReadOpen(read);
                IoWrite *write = ioHandleWriteNew(strNew("server write"), HARNESS_FORK_PARENT_WRITE_PROCESS(0));
                ioWriteOpen(write);

                ProtocolClient *client = protocolClientNew(strNew("test"), strNew("config"), read, write);

                VariantList *list = varLstNew();
                varLstAdd(list, varNewStr(strNew("repo1-host")));
                varLstAdd(list, varNewStr(strNew("repo1-host-user")));

                TEST_RESULT_STR_Z(
                    strLstJoin(strLstNewVarLst(configProtocolOption(client, list)), "|"), "repo-host|repo-host-user",
                    "get options");

                protocolClientFree(client);
            }
            HARNESS_FORK_PARENT_END();
        }
        HARNESS_FORK_END();
    }

    FUNCTION_HARNESS_RESULT_VOID();
}
