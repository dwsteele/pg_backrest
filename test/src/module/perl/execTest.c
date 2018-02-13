/***********************************************************************************************************************************
Test Perl Exec
***********************************************************************************************************************************/
#include "config/config.h"

#define TEST_ENV_EXE                                                "/usr/bin/env"
#define TEST_PERL_EXE                                               "perl"
#define TEST_BACKREST_EXE                                           "/path/to/pgbackrest"
#define TEST_PERL_MAIN                                                                                                             \
    "-MpgBackRest::Main|-e|pgBackRest::Main::main('" TEST_BACKREST_EXE ""

/***********************************************************************************************************************************
Test Run
***********************************************************************************************************************************/
void
testRun()
{
    // -----------------------------------------------------------------------------------------------------------------------------
    if (testBegin("perlCommand()"))
    {
        // -------------------------------------------------------------------------------------------------------------------------
        cfgInit();
        cfgCommandSet(cfgCmdInfo);
        cfgExeSet(strNew(TEST_BACKREST_EXE));
        cfgOptionSet(cfgOptPerlBin, cfgSourceParam, varNewStrZ("/usr/bin/perl"));

        TEST_RESULT_STR(
            strPtr(strLstJoin(perlCommand(), "|")),
            "/usr/bin/perl|" TEST_PERL_MAIN "','info','{}')|[NULL]", "custom command with no options");

        cfgOptionSet(cfgOptPerlBin, cfgSourceParam, NULL);

        TEST_RESULT_STR(
            strPtr(strLstJoin(perlCommand(), "|")),
            TEST_ENV_EXE "|" TEST_PERL_EXE "|" TEST_PERL_MAIN "','info','{}')|[NULL]", "command with no options");

        // -------------------------------------------------------------------------------------------------------------------------
        cfgInit();
        cfgCommandSet(cfgCmdBackup);
        cfgExeSet(strNew(TEST_BACKREST_EXE));

        cfgOptionValidSet(cfgOptCompress, true);
        cfgOptionSet(cfgOptCompress, cfgSourceParam, varNewBool(true));

        cfgOptionValidSet(cfgOptOnline, true);
        cfgOptionNegateSet(cfgOptOnline, true);
        cfgOptionSet(cfgOptOnline, cfgSourceParam, varNewBool(false));

        cfgOptionValidSet(cfgOptPgHost, true);
        cfgOptionResetSet(cfgOptPgHost, true);

        cfgOptionValidSet(cfgOptProtocolTimeout, true);
        cfgOptionSet(cfgOptProtocolTimeout, cfgSourceParam, varNewDbl(1.1));

        cfgOptionValidSet(cfgOptArchiveQueueMax, true);
        cfgOptionSet(cfgOptArchiveQueueMax, cfgSourceParam, varNewInt64(999999999999));

        cfgOptionValidSet(cfgOptCompressLevel, true);
        cfgOptionSet(cfgOptCompressLevel, cfgSourceConfig, varNewInt(3));

        cfgOptionValidSet(cfgOptStanza, true);
        cfgOptionSet(cfgOptStanza, cfgSourceDefault, varNewStr(strNew("db")));

        TEST_RESULT_STR(
            strPtr(strLstJoin(perlCommand(), "|")),
            TEST_ENV_EXE "|" TEST_PERL_EXE "|" TEST_PERL_MAIN "','backup','{"
            "\"archive-queue-max\":{\"source\":\"param\",\"value\":999999999999},"
            "\"compress\":{\"source\":\"param\",\"value\":true},"
            "\"compress-level\":{\"source\":\"config\",\"value\":3},"
            "\"online\":{\"source\":\"param\",\"negate\":true},"
            "\"pg1-host\":{\"reset\":true},"
            "\"protocol-timeout\":{\"source\":\"param\",\"value\":1.1},"
            "\"stanza\":{\"value\":\"db\"}"
            "}')|[NULL]",
            "simple options");

        // -------------------------------------------------------------------------------------------------------------------------
        cfgInit();
        cfgCommandHelpSet(true);
        cfgCommandSet(cfgCmdRestore);
        cfgExeSet(strNew(TEST_BACKREST_EXE));

        cfgOptionValidSet(cfgOptDbInclude, true);
        StringList *includeList = strLstNew();
        strLstAdd(includeList, strNew("db1"));
        strLstAdd(includeList, strNew("db2"));
        cfgOptionSet(cfgOptDbInclude, cfgSourceParam, varNewVarLst(varLstNewStrLst(includeList)));

        cfgOptionValidSet(cfgOptRecoveryOption, true);
        // !!! WHY DO WE STILL NEED TO CREATE THE VAR KV EMPTY?
        Variant *recoveryVar = varNewKv();
        KeyValue *recoveryKv = varKv(recoveryVar);
        kvPut(recoveryKv, varNewStr(strNew("standby_mode")), varNewStr(strNew("on")));
        kvPut(recoveryKv, varNewStr(strNew("primary_conn_info")), varNewStr(strNew("blah")));
        cfgOptionSet(cfgOptRecoveryOption, cfgSourceParam, recoveryVar);

        StringList *commandParamList = strLstNew();
        strLstAdd(commandParamList, strNew("param1"));
        strLstAdd(commandParamList, strNew("param2"));
        cfgCommandParamSet(commandParamList);

        cfgOptionValidSet(cfgOptPerlOption, true);
        StringList *perlList = strLstNew();
        strLstAdd(perlList, strNew("-I."));
        strLstAdd(perlList, strNew("-MDevel::Cover=-silent,1"));
        cfgOptionSet(cfgOptPerlOption, cfgSourceParam, varNewVarLst(varLstNewStrLst(perlList)));

        TEST_RESULT_STR(
            strPtr(strLstJoin(perlCommand(), "|")),
            TEST_ENV_EXE "|" TEST_PERL_EXE "|-I.|-MDevel::Cover=-silent,1|" TEST_PERL_MAIN "','restore','{"
            "\"db-include\":{\"source\":\"param\",\"value\":{\"db1\":true,\"db2\":true}},"
            "\"perl-option\":{\"source\":\"param\",\"value\":{\"-I.\":true,\"-MDevel::Cover=-silent,1\":true}},"
            "\"recovery-option\":{\"source\":\"param\",\"value\":{\"standby_mode\":\"on\",\"primary_conn_info\":\"blah\"}}"
            "}','param1','param2')|[NULL]", "complex options");
    }

    // -----------------------------------------------------------------------------------------------------------------------------
    if (testBegin("perlExec()"))
    {
        StringList *param = strLstAdd(strLstAdd(strLstNew(), strNew(BOGUS_STR)), NULL);

        TEST_ERROR(perlExec(param), AssertError, "unable to exec BOGUS: No such file or directory");
    }
}
