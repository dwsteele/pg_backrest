/***********************************************************************************************************************************
Test Configuration Parse
***********************************************************************************************************************************/
#define TEST_BACKREST_EXE                                           "pgbackrest"

#define TEST_COMMAND_ARCHIVE_GET                                    "archive-get"
#define TEST_COMMAND_BACKUP                                         "backup"
#define TEST_COMMAND_HELP                                           "help"
#define TEST_COMMAND_RESTORE                                        "restore"
#define TEST_COMMAND_VERSION                                        "version"

/***********************************************************************************************************************************
Expose log internal data for unit testing/debugging
***********************************************************************************************************************************/
extern LogLevel logLevelStdOut;
extern LogLevel logLevelStdErr;
extern LogLevel logLevelFile;

/***********************************************************************************************************************************
Option find test -- this is done a lot in the deprecated tests
***********************************************************************************************************************************/
static void
testOptionFind(const char *option, unsigned int value)
{
    // If not testing for a missing option, then add the option offset that is already added to each option in the list
    if (value != 0)
        value |= PARSE_OPTION_FLAG;

    TEST_RESULT_INT(optionList[optionFind(strNew(option))].val, value, "check %s", option);
}

/***********************************************************************************************************************************
Test run
***********************************************************************************************************************************/
void
testRun(void)
{
    FUNCTION_HARNESS_VOID();

    // config and config-include-path options
    // -----------------------------------------------------------------------------------------------------------------------------
    if (testBegin("cfgFileLoad()"))
    {
        StringList *argList = NULL;
        String *configFile = strNewFmt("%s/test.config", testPath());

        String *configIncludePath = strNewFmt("%s/conf.d", testPath());
        mkdir(strPtr(configIncludePath), 0750);

        // Check old config file constant
        // -------------------------------------------------------------------------------------------------------------------------
        TEST_RESULT_STR(PGBACKREST_CONFIG_ORIG_PATH_FILE, "/etc/pgbackrest.conf", "check old config path");

        // Confirm same behavior with multiple config include files
        //--------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNewFmt("--config=%s", strPtr(configFile)));
        strLstAdd(argList, strNewFmt("--config-include-path=%s", strPtr(configIncludePath)));
        strLstAdd(argList, strNew("--no-online"));
        strLstAdd(argList, strNew("--reset-pg1-host"));
        strLstAdd(argList, strNew("--reset-backup-standby"));
        strLstAdd(argList, strNew(TEST_COMMAND_BACKUP));

        storagePut(
            storageNewWriteNP(storageLocalWrite(), configFile), bufNewStr(
                strNew(
                    "[global]\n"
                    "compress-level=3\n"
                    "spool-path=/path/to/spool\n")));

        storagePut(
            storageNewWriteNP(
                    storageLocalWrite(), strNewFmt("%s/global-backup.conf", strPtr(configIncludePath))), bufNewStr(
                    strNew(
                        "[global:backup]\n"
                        "repo1-hardlink=y\n"
                        "bogus=bogus\n"
                        "no-compress=y\n"
                        "reset-compress=y\n"
                        "archive-copy=y\n"
                        "online=y\n"
                        "pg1-path=/not/path/to/db\n"
                        "backup-standby=y\n"
                        "buffer-size=65536\n")));

        storagePut(
            storageNewWriteNP(storageLocalWrite(), strNewFmt("%s/db-backup.conf", strPtr(configIncludePath))), bufNewStr(
                strNew(
                    "[db:backup]\n"
                    "compress=n\n"
                    "recovery-option=a=b\n")));

        storagePut(
            storageNewWriteNP(storageLocalWrite(), strNewFmt("%s/stanza.db.conf", strPtr(configIncludePath))), bufNewStr(
                strNew(
                    "[db]\n"
                    "pg1-host=db\n"
                    "pg1-path=/path/to/db\n"
                    "recovery-option=c=d\n")));

        TEST_RESULT_VOID(
            configParse(strLstSize(argList), strLstPtr(argList), false), TEST_COMMAND_BACKUP " command with config-include");
        harnessLogResult(
            strPtr(
                strNew(
                    "P00   WARN: configuration file contains option 'recovery-option' invalid for section 'db:backup'\n"
                    "P00   WARN: configuration file contains invalid option 'bogus'\n"
                    "P00   WARN: configuration file contains negate option 'no-compress'\n"
                    "P00   WARN: configuration file contains reset option 'reset-compress'\n"
                    "P00   WARN: configuration file contains command-line only option 'online'\n"
                    "P00   WARN: configuration file contains stanza-only option 'pg1-path' in global section 'global:backup'")));

        TEST_RESULT_BOOL(cfgOptionTest(cfgOptPgHost), false, "    pg1-host is not set (command line reset override)");
        TEST_RESULT_STR(strPtr(cfgOptionStr(cfgOptPgPath)), "/path/to/db", "    pg1-path is set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptPgPath), cfgSourceConfig, "    pg1-path is source config");
        TEST_RESULT_BOOL(cfgOptionBool(cfgOptCompress), false, "    compress not is set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptCompress), cfgSourceConfig, "    compress is source config");
        TEST_RESULT_BOOL(cfgOptionTest(cfgOptArchiveCheck), false, "    archive-check is not set");
        TEST_RESULT_BOOL(cfgOptionTest(cfgOptArchiveCopy), false, "    archive-copy is not set");
        TEST_RESULT_BOOL(cfgOptionBool(cfgOptRepoHardlink), true, "    repo-hardlink is set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptRepoHardlink), cfgSourceConfig, "    repo-hardlink is source config");
        TEST_RESULT_INT(cfgOptionInt(cfgOptCompressLevel), 3, "    compress-level is set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptCompressLevel), cfgSourceConfig, "    compress-level is source config");
        TEST_RESULT_BOOL(cfgOptionBool(cfgOptBackupStandby), false, "    backup-standby not is set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptBackupStandby), cfgSourceDefault, "    backup-standby is source default");
        TEST_RESULT_BOOL(cfgOptionInt64(cfgOptBufferSize), 65536, "    buffer-size is set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptBufferSize), cfgSourceConfig, "    backup-standby is source config");

        // Rename conf files - ensure read of conf extension only is attempted
        //--------------------------------------------------------------------------------------------------------------------------
        rename(strPtr(strNewFmt("%s/db-backup.conf", strPtr(configIncludePath))),
            strPtr(strNewFmt("%s/db-backup.conf.save", strPtr(configIncludePath))));
        rename(strPtr(strNewFmt("%s/global-backup.conf", strPtr(configIncludePath))),
            strPtr(strNewFmt("%s/global-backup.confsave", strPtr(configIncludePath))));

        // Set up defaults
        String *backupCmdDefConfigValue = strNew(cfgDefOptionDefault(
            cfgCommandDefIdFromId(cfgCommandId(TEST_COMMAND_BACKUP)), cfgOptionDefIdFromId(cfgOptConfig)));
        String *backupCmdDefConfigInclPathValue = strNew(cfgDefOptionDefault(
                cfgCommandDefIdFromId(cfgCommandId(TEST_COMMAND_BACKUP)), cfgOptionDefIdFromId(cfgOptConfigIncludePath)));
        String *oldConfigDefault = strNewFmt("%s%s", testPath(), PGBACKREST_CONFIG_ORIG_PATH_FILE);

        // Create the option structure and initialize with 0
        ParseOption parseOptionList[CFG_OPTION_TOTAL];
        memset(&parseOptionList, 0, sizeof(parseOptionList));

        parseOptionList[cfgOptConfig].found = true;
        parseOptionList[cfgOptConfig].source = cfgSourceParam;
        parseOptionList[cfgOptConfig].valueList = strLstAdd(strLstNew(), configFile);

        parseOptionList[cfgOptConfigIncludePath].found = true;
        parseOptionList[cfgOptConfigIncludePath].source = cfgSourceParam;
        parseOptionList[cfgOptConfigIncludePath].valueList = strLstAdd(strLstNew(), configIncludePath);

        TEST_RESULT_STR(strPtr(cfgFileLoad(parseOptionList, backupCmdDefConfigValue,
            backupCmdDefConfigInclPathValue, oldConfigDefault)),
            "[global]\n"
            "compress-level=3\n"
            "spool-path=/path/to/spool\n"
            "\n"
            "[db]\n"
            "pg1-host=db\n"
            "pg1-path=/path/to/db\n"
            "recovery-option=c=d\n",
            "config-include-path with .conf files and non-.conf files");

        // Pass invalid values
        //--------------------------------------------------------------------------------------------------------------------------
        // --config valid, --config-include-path invalid (does not exist)
        parseOptionList[cfgOptConfigIncludePath].valueList = strLstAddZ(strLstNew(), BOGUS_STR);
        TEST_ERROR(
            cfgFileLoad(parseOptionList, backupCmdDefConfigValue,
                backupCmdDefConfigInclPathValue, oldConfigDefault), PathOpenError,
                "unable to open path '/BOGUS' for read: [2] No such file or directory");

        // --config-include-path valid, --config invalid (does not exist)
        parseOptionList[cfgOptConfigIncludePath].valueList = strLstAdd(strLstNew(), configIncludePath);
        parseOptionList[cfgOptConfig].valueList = strLstAdd(strLstNew(), strNewFmt("%s/%s", testPath(), BOGUS_STR));

        TEST_ERROR(
            cfgFileLoad(parseOptionList, backupCmdDefConfigValue,
                backupCmdDefConfigInclPathValue, oldConfigDefault), FileOpenError,
                strPtr(strNewFmt("unable to open '%s/%s' for read: [2] No such file or directory", testPath(), BOGUS_STR)));

        strLstFree(parseOptionList[cfgOptConfig].valueList);
        strLstFree(parseOptionList[cfgOptConfigIncludePath].valueList);

        // Neither config nor config-include-path passed as parameter (defaults but none exist)
        //--------------------------------------------------------------------------------------------------------------------------
        parseOptionList[cfgOptConfig].found = false;
        parseOptionList[cfgOptConfig].source = cfgSourceDefault;
        parseOptionList[cfgOptConfigIncludePath].found = false;
        parseOptionList[cfgOptConfigIncludePath].source = cfgSourceDefault;

        TEST_RESULT_STR(strPtr(cfgFileLoad(parseOptionList, backupCmdDefConfigValue,
            backupCmdDefConfigInclPathValue, oldConfigDefault)),
            NULL,
            "config default, config-include-path default but nothing to read");

        // Config not passed as parameter - config does not exist. config-include-path passed as parameter - only include read
        //--------------------------------------------------------------------------------------------------------------------------
        parseOptionList[cfgOptConfig].found = false;
        parseOptionList[cfgOptConfig].source = cfgSourceDefault;
        parseOptionList[cfgOptConfigIncludePath].found = true;
        parseOptionList[cfgOptConfigIncludePath].source = cfgSourceParam;
        parseOptionList[cfgOptConfigIncludePath].valueList = strLstAdd(strLstNew(), configIncludePath);

        TEST_RESULT_STR(strPtr(cfgFileLoad(parseOptionList, backupCmdDefConfigValue,
            backupCmdDefConfigInclPathValue, oldConfigDefault)),
            "[db]\n"
            "pg1-host=db\n"
            "pg1-path=/path/to/db\n"
            "recovery-option=c=d\n",
            "config default and doesn't exist, config-include-path passed read");

        // config and config-include-path are "default" with files existing. Config file exists in both currrent default and old
        // default location - old location ignored.
        //--------------------------------------------------------------------------------------------------------------------------
        parseOptionList[cfgOptConfig].found = false;
        parseOptionList[cfgOptConfig].source = cfgSourceDefault;
        parseOptionList[cfgOptConfigIncludePath].found = false;
        parseOptionList[cfgOptConfigIncludePath].source = cfgSourceDefault;

        mkdir(strPtr(strPath(oldConfigDefault)), 0750);
        storagePut(
            storageNewWriteNP(
                    storageLocalWrite(), oldConfigDefault), bufNewStr(
                    strNew(
                        "[global:backup]\n"
                        "buffer-size=65536\n")));

        // Pass actual location of config files as "default" - not setting valueList above, so these are the only possible values
        // to choose.
        TEST_RESULT_STR(strPtr(cfgFileLoad(parseOptionList, configFile,  configIncludePath,
            oldConfigDefault)),
            "[global]\n"
            "compress-level=3\n"
            "spool-path=/path/to/spool\n"
            "\n"
            "[db]\n"
            "pg1-host=db\n"
            "pg1-path=/path/to/db\n"
            "recovery-option=c=d\n",
            "config and config-include-path default, files appended, original config not read");

        // Config not passed as parameter - config does not exist in new default but does exist in old default. config-include-path
        // passed as parameter - both include and old default read
        //--------------------------------------------------------------------------------------------------------------------------
        parseOptionList[cfgOptConfig].found = false;
        parseOptionList[cfgOptConfig].source = cfgSourceDefault;
        parseOptionList[cfgOptConfigIncludePath].found = true;
        parseOptionList[cfgOptConfigIncludePath].source = cfgSourceParam;
        parseOptionList[cfgOptConfigIncludePath].valueList = strLstAdd(strLstNew(), configIncludePath);

        TEST_RESULT_STR(strPtr(cfgFileLoad(parseOptionList, backupCmdDefConfigValue,
            backupCmdDefConfigInclPathValue, oldConfigDefault)),
            "[global:backup]\n"
            "buffer-size=65536\n"
            "\n"
            "[db]\n"
            "pg1-host=db\n"
            "pg1-path=/path/to/db\n"
            "recovery-option=c=d\n",
            "config old default read, config-include-path passed read");

        // --no-config and config-include-path passed as parameter (files read only from include path and not current or old config)
        //--------------------------------------------------------------------------------------------------------------------------
        parseOptionList[cfgOptConfig].found = true;
        parseOptionList[cfgOptConfig].source = cfgSourceParam;
        parseOptionList[cfgOptConfig].negate = true;
        parseOptionList[cfgOptConfigIncludePath].found = true;
        parseOptionList[cfgOptConfigIncludePath].source = cfgSourceParam;
        parseOptionList[cfgOptConfigIncludePath].valueList = strLstAdd(strLstNew(), configIncludePath);

        TEST_RESULT_STR(strPtr(cfgFileLoad(parseOptionList, backupCmdDefConfigValue,
            backupCmdDefConfigInclPathValue, oldConfigDefault)),
            "[db]\n"
            "pg1-host=db\n"
            "pg1-path=/path/to/db\n"
            "recovery-option=c=d\n",
            "--no-config, only config-include-path read");

        // --no-config and config-include-path default exists with files - nothing to read
        //--------------------------------------------------------------------------------------------------------------------------
        parseOptionList[cfgOptConfig].found = true;
        parseOptionList[cfgOptConfig].source = cfgSourceParam;
        parseOptionList[cfgOptConfig].negate = true;
        parseOptionList[cfgOptConfigIncludePath].found = false;
        parseOptionList[cfgOptConfigIncludePath].source = cfgSourceDefault;

        TEST_RESULT_STR(strPtr(cfgFileLoad(parseOptionList, backupCmdDefConfigValue,
            configIncludePath, oldConfigDefault)),
            NULL,
            "--no-config, config-include-path default, nothing read");

        // config passed and config-include-path default exists with files - only config read
        //--------------------------------------------------------------------------------------------------------------------------
        parseOptionList[cfgOptConfig].found = true;
        parseOptionList[cfgOptConfig].source = cfgSourceParam;
        parseOptionList[cfgOptConfig].negate = false;
        parseOptionList[cfgOptConfig].valueList = strLstAdd(strLstNew(), configFile);
        parseOptionList[cfgOptConfigIncludePath].found = false;
        parseOptionList[cfgOptConfigIncludePath].source = cfgSourceDefault;

        TEST_RESULT_STR(strPtr(cfgFileLoad(parseOptionList, backupCmdDefConfigValue,
            configIncludePath, oldConfigDefault)),
            "[global]\n"
            "compress-level=3\n"
            "spool-path=/path/to/spool\n",
            "config param specified, config-include-path default, only config read");

        // config new default and config-include-path passed - both exists with files. config-include-path & new config default read
        //--------------------------------------------------------------------------------------------------------------------------
        parseOptionList[cfgOptConfig].found = false;
        parseOptionList[cfgOptConfig].source = cfgSourceDefault;
        parseOptionList[cfgOptConfigIncludePath].found = true;
        parseOptionList[cfgOptConfigIncludePath].source = cfgSourceParam;
        parseOptionList[cfgOptConfigIncludePath].valueList = strLstAdd(strLstNew(), configIncludePath);

        TEST_RESULT_STR(strPtr(cfgFileLoad(parseOptionList, configFile,
            backupCmdDefConfigInclPathValue, oldConfigDefault)),
            "[global]\n"
            "compress-level=3\n"
            "spool-path=/path/to/spool\n"
            "\n"
            "[db]\n"
            "pg1-host=db\n"
            "pg1-path=/path/to/db\n"
            "recovery-option=c=d\n",
            "config new default exists with files, config-include-path passed, defualt config and config-include-path read");

        // config and config-include-path are "default".
        //--------------------------------------------------------------------------------------------------------------------------
        parseOptionList[cfgOptConfig].found = false;
        parseOptionList[cfgOptConfig].source = cfgSourceDefault;
        parseOptionList[cfgOptConfigIncludePath].found = false;
        parseOptionList[cfgOptConfigIncludePath].source = cfgSourceDefault;

        // File exists in old default config location but not in current default.
        TEST_RESULT_STR(strPtr(cfgFileLoad(parseOptionList, backupCmdDefConfigValue,
            configIncludePath, oldConfigDefault)),
            "[global:backup]\n"
            "buffer-size=65536\n"
            "\n"
            "[db]\n"
            "pg1-host=db\n"
            "pg1-path=/path/to/db\n"
            "recovery-option=c=d\n",
            "config-path override - config-include-path files and old config default read since no config in current path");

        // Pass --config-path
        parseOptionList[cfgOptConfigPath].found = true;
        parseOptionList[cfgOptConfigPath].source = cfgSourceParam;
        parseOptionList[cfgOptConfigPath].valueList = strLstAddZ(strLstNew(), testPath());

        // Override default paths for config and config-include-path - but no pgbackrest.conf file in override path only in old
        // default so ignored
        TEST_RESULT_STR(strPtr(cfgFileLoad(parseOptionList, backupCmdDefConfigValue,
            backupCmdDefConfigInclPathValue, oldConfigDefault)),
            "[db]\n"
            "pg1-host=db\n"
            "pg1-path=/path/to/db\n"
            "recovery-option=c=d\n",
            "config-path override: config-include-path files read but config not read - does not exist");

        // Pass --config
        parseOptionList[cfgOptConfig].found = true;
        parseOptionList[cfgOptConfig].source = cfgSourceParam;
        parseOptionList[cfgOptConfig].negate = false;
        parseOptionList[cfgOptConfig].valueList = strLstAdd(strLstNew(), configFile);

        // Passing --config and --config-path - default config-include-path overwritten and config is required and is loaded and
        // config-include-path files will attempt to be loaded but not required
        TEST_RESULT_STR(strPtr(cfgFileLoad(parseOptionList, backupCmdDefConfigValue,
            backupCmdDefConfigInclPathValue, oldConfigDefault)),
            "[global]\n"
            "compress-level=3\n"
            "spool-path=/path/to/spool\n"
            "\n"
            "[db]\n"
            "pg1-host=db\n"
            "pg1-path=/path/to/db\n"
            "recovery-option=c=d\n",
            "config-path changed config-include-path default - files exist, config and config includes read");

        parseOptionList[cfgOptConfigPath].valueList = strLstAddZ(strLstNew(), BOGUS_STR);

        // Passing --config and bogus --config-path - default config-include-path overwritten, config is required and is loaded and
        // config-include-path files will attempt to be loaded but doesn't exist - no error since not required
        TEST_RESULT_STR(strPtr(cfgFileLoad(parseOptionList, backupCmdDefConfigValue,
            backupCmdDefConfigInclPathValue, oldConfigDefault)),
            "[global]\n"
            "compress-level=3\n"
            "spool-path=/path/to/spool\n",
            "config-path changed config-include-path default but directory does not exist - only config read");

        // Copy the configFile to pgbackrest.conf (default is /etc/pgbackrest/pgbackrest.conf and new value is testPath so copy the
        // config file (that was not read in the previous test) to pgbackrest.conf so it will be read by the override
        TEST_RESULT_INT(
            system(
                strPtr(strNewFmt("cp %s %s", strPtr(configFile), strPtr(strNewFmt("%s/pgbackrest.conf", testPath()))))), 0,
                "copy configFile to pgbackrest.conf");

        parseOptionList[cfgOptConfig].found = false;
        parseOptionList[cfgOptConfig].source = cfgSourceDefault;
        parseOptionList[cfgOptConfig].negate = false;
        parseOptionList[cfgOptConfigIncludePath].found = false;
        parseOptionList[cfgOptConfigIncludePath].source = cfgSourceDefault;

        parseOptionList[cfgOptConfigPath].valueList = strLstAddZ(strLstNew(), testPath());

        // Override default paths for config and config-include-path with --config-path
        TEST_RESULT_STR(strPtr(cfgFileLoad(parseOptionList, backupCmdDefConfigValue,
            backupCmdDefConfigInclPathValue, oldConfigDefault)),
            "[global]\n"
            "compress-level=3\n"
            "spool-path=/path/to/spool\n"
            "\n"
            "[db]\n"
            "pg1-host=db\n"
            "pg1-path=/path/to/db\n"
            "recovery-option=c=d\n",
            "config-path override: config-include-path and config file read");

        // Clear config-path
        parseOptionList[cfgOptConfigPath].found = false;
        parseOptionList[cfgOptConfigPath].source = cfgSourceDefault;

        // config default and config-include-path passed - but no config files in the include path - only in the default path
        // rm command is split below because code counter is confused by what looks like a comment.
        //--------------------------------------------------------------------------------------------------------------------------
        TEST_RESULT_INT(system(strPtr(strNewFmt("sudo rm -rf %s/" "*", strPtr(configIncludePath)))), 0, "remove all include files");

        parseOptionList[cfgOptConfig].found = false;
        parseOptionList[cfgOptConfig].source = cfgSourceDefault;
        parseOptionList[cfgOptConfigIncludePath].found = true;
        parseOptionList[cfgOptConfigIncludePath].source = cfgSourceParam;
        parseOptionList[cfgOptConfigIncludePath].valueList = strLstAdd(strLstNew(), configIncludePath);

        TEST_RESULT_STR(strPtr(cfgFileLoad(parseOptionList, configFile,
            backupCmdDefConfigInclPathValue, oldConfigDefault)),
            "[global]\n"
            "compress-level=3\n"
            "spool-path=/path/to/spool\n",
            "config default exists with files but config-include-path path passed is empty - only config read");

        // config default and config-include-path passed - only empty file in the include path and nothing in either config defaults
        //--------------------------------------------------------------------------------------------------------------------------
        TEST_RESULT_INT(system(strPtr(strNewFmt("touch %s", strPtr(strNewFmt("%s/empty.conf", strPtr(configIncludePath)))))), 0,
            "add empty conf file to include directory");

        parseOptionList[cfgOptConfig].found = false;
        parseOptionList[cfgOptConfig].source = cfgSourceDefault;
        parseOptionList[cfgOptConfigIncludePath].found = true;
        parseOptionList[cfgOptConfigIncludePath].source = cfgSourceParam;
        parseOptionList[cfgOptConfigIncludePath].valueList = strLstAdd(strLstNew(), configIncludePath);

        TEST_RESULT_STR(strPtr(cfgFileLoad(parseOptionList, backupCmdDefConfigValue,
            backupCmdDefConfigInclPathValue, backupCmdDefConfigValue)),
            NULL,
            "config default does not exist, config-include-path passed but only empty conf file - nothing read");
    }

    // -----------------------------------------------------------------------------------------------------------------------------
    if (testBegin("convertToByte()"))
    {
        double valueDbl = 0;
        String *value = strNew("10.0");

        TEST_ERROR(convertToByte(&value, &valueDbl), FormatError, "value '10.0' is not valid");
        strTrunc(value, strChr(value, '.'));
        strCat(value, "K2");
        TEST_ERROR(convertToByte(&value, &valueDbl), FormatError, "value '10K2' is not valid");
        strTrunc(value, strChr(value, '1'));
        strCat(value, "ab");
        TEST_ERROR(convertToByte(&value, &valueDbl), FormatError, "value 'ab' is not valid");

        strTrunc(value, strChr(value, 'a'));
        strCat(value, "10");
        convertToByte(&value, &valueDbl);
        TEST_RESULT_DOUBLE(valueDbl, 10, "valueDbl no character identifier - straight to bytes");
        TEST_RESULT_STR(strPtr(value), "10", "value no character identifier - straight to bytes");

        strCat(value, "B");
        convertToByte(&value, &valueDbl);
        TEST_RESULT_DOUBLE(valueDbl, 10, "valueDbl B to bytes");
        TEST_RESULT_STR(strPtr(value), "10", "value B to bytes");

        strCat(value, "Kb");
        convertToByte(&value, &valueDbl);
        TEST_RESULT_DOUBLE(valueDbl, 10240, "valueDbl KB to bytes");
        TEST_RESULT_STR(strPtr(value), "10240", "value KB to bytes");

        strTrunc(value, strChr(value, '2'));
        strCat(value, "k");
        convertToByte(&value, &valueDbl);
        TEST_RESULT_DOUBLE(valueDbl, 10240, "valueDbl k to bytes");
        TEST_RESULT_STR(strPtr(value), "10240", "value k to bytes");

        strCat(value, "pB");
        convertToByte(&value, &valueDbl);
        TEST_RESULT_DOUBLE(valueDbl, 11529215046068469760U, "valueDbl Pb to bytes");
        TEST_RESULT_STR(strPtr(value), "11529215046068469760", "value Pb to bytes");

        strTrunc(value, strChr(value, '5'));
        strCat(value, "GB");
        convertToByte(&value, &valueDbl);
        TEST_RESULT_DOUBLE(valueDbl, 11811160064U, "valueDbl GB to bytes");
        TEST_RESULT_STR(strPtr(value), "11811160064", "value GB to bytes");

        strTrunc(value, strChr(value, '8'));
        strCat(value, "g");
        convertToByte(&value, &valueDbl);
        TEST_RESULT_DOUBLE(valueDbl, 11811160064U, "valueDbl g to bytes");
        TEST_RESULT_STR(strPtr(value), "11811160064", "value g to bytes");

        strTrunc(value, strChr(value, '8'));
        strCat(value, "T");
        convertToByte(&value, &valueDbl);
        TEST_RESULT_DOUBLE(valueDbl, 12094627905536U, "valueDbl T to bytes");
        TEST_RESULT_STR(strPtr(value), "12094627905536", "value T to bytes");

        strTrunc(value, strChr(value, '0'));
        strCat(value, "tb");
        convertToByte(&value, &valueDbl);
        TEST_RESULT_DOUBLE(valueDbl, 13194139533312U, "valueDbl tb to bytes");
        TEST_RESULT_STR(strPtr(value), "13194139533312", "value tb to bytes");

        strTrunc(value, strChr(value, '3'));
        strCat(value, "0m");
        convertToByte(&value, &valueDbl);
        TEST_RESULT_DOUBLE(valueDbl, 10485760, "valueDbl m to bytes");
        TEST_RESULT_STR(strPtr(value), "10485760", "value m to bytes");

        strCat(value, "mb");
        convertToByte(&value, &valueDbl);
        TEST_RESULT_DOUBLE(valueDbl, 10995116277760U, "valueDbl mb to bytes");
        TEST_RESULT_STR(strPtr(value), "10995116277760", "value mb to bytes");

        strTrunc(value, strChr(value, '0'));
        strCat(value, "99999999999999999999p");
        convertToByte(&value, &valueDbl);
        TEST_RESULT_STR(strPtr(value), "225179981368524800000000000000000000", "value really large  to bytes");

    }

    // -----------------------------------------------------------------------------------------------------------------------------
    if (testBegin("configParse()"))
    {
        StringList *argList = NULL;
        String *configFile = strNewFmt("%s/test.config", testPath());

        TEST_RESULT_INT(
            sizeof(optionResolveOrder) / sizeof(ConfigOption), CFG_OPTION_TOTAL,
            "check that the option resolve list contains an entry for every option");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew(BOGUS_STR));
        TEST_ERROR(configParse(strLstSize(argList), strLstPtr(argList), false), CommandInvalidError, "invalid command 'BOGUS'");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--" BOGUS_STR));
        TEST_ERROR(configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError, "invalid option '--BOGUS'");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-host"));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            "option '--pg1-host' requires argument");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--no-online"));
        strLstAdd(argList, strNew("--no-online"));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            "option 'online' is negated multiple times");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--reset-pg1-host"));
        strLstAdd(argList, strNew("--reset-pg1-host"));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            "option 'pg1-host' is reset multiple times");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--no-config"));
        strLstAdd(argList, strNew("--config=/etc/config"));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            "option 'config' cannot be set and negated");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--reset-log-path"));
        strLstAdd(argList, strNew("--log-path=/var/log"));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            "option 'log-path' cannot be set and reset");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--no-compress"));
        strLstAdd(argList, strNew("--reset-compress"));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            "option 'compress' cannot be negated and reset");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--reset-compress"));
        strLstAdd(argList, strNew("--no-compress"));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            "option 'compress' cannot be negated and reset");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew(TEST_COMMAND_BACKUP));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--compress-level=3"));
        strLstAdd(argList, strNew("--compress-level=3"));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            "option 'compress-level' cannot have multiple arguments");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--online"));
        TEST_ERROR(configParse(strLstSize(argList), strLstPtr(argList), false), CommandRequiredError, "no command found");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew(TEST_COMMAND_BACKUP));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--manifest-save-threshold=123Q"));
        TEST_ERROR(configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidValueError,
            "'123Q' is not valid for 'manifest-save-threshold' option");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew(TEST_COMMAND_BACKUP));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--manifest-save-threshold=199999999999999999999p"));
        TEST_ERROR(configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidValueError,
            "'225179981368524800000000000000000000' is out of range for 'manifest-save-threshold' option");

        // Local and remove commands should not modify log levels during parsing
        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew("pgbackrest"));
        strLstAdd(argList, strNew("--host-id=1"));
        strLstAdd(argList, strNew("--process=1"));
        strLstAdd(argList, strNew("--command=backup"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--type=backup"));
        strLstAdd(argList, strNew("--log-level-stderr=info"));
        strLstAdd(argList, strNew("local"));

        logLevelStdOut = logLevelError;
        logLevelStdErr = logLevelError;
        TEST_RESULT_VOID(configParse(strLstSize(argList), strLstPtr(argList), false), "load local config");
        TEST_RESULT_INT(logLevelStdOut, logLevelError, "console logging is error");
        TEST_RESULT_INT(logLevelStdErr, logLevelError, "stderr logging is error");

        argList = strLstNew();
        strLstAdd(argList, strNew("pgbackrest"));
        strLstAdd(argList, strNew("--process=1"));
        strLstAdd(argList, strNew("--command=backup"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--type=backup"));
        strLstAdd(argList, strNew("--log-level-stderr=info"));
        strLstAdd(argList, strNew("remote"));

        logLevelStdOut = logLevelError;
        logLevelStdErr = logLevelError;
        TEST_RESULT_VOID(configParse(strLstSize(argList), strLstPtr(argList), false), "load remote config");
        TEST_RESULT_INT(logLevelStdOut, logLevelError, "console logging is error");
        TEST_RESULT_INT(logLevelStdErr, logLevelError, "stderr logging is error");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew(TEST_COMMAND_BACKUP));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionRequiredError,
            "backup command requires option: pg1-path\nHINT: does this stanza exist?");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew(TEST_COMMAND_BACKUP));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionRequiredError,
            "backup command requires option: stanza");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--repo1-type=s3"));
        strLstAdd(argList, strNew("--repo1-s3-key=xxx"));
        strLstAdd(argList, strNew("--repo1-s3-bucket=xxx"));
        strLstAdd(argList, strNew("--repo1-s3-endpoint=xxx"));
        strLstAdd(argList, strNew(TEST_COMMAND_BACKUP));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            "option 'repo1-s3-key' is not allowed on the command-line\n"
            "HINT: this option could expose secrets in the process list.\n"
            "HINT: specify the option in '/etc/pgbackrest/pgbackrest.conf' instead.");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--no-config"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--repo1-s3-host=xxx"));
        strLstAdd(argList, strNew(TEST_COMMAND_BACKUP));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            "option 'repo1-s3-host' not valid without option 'repo1-type' = 's3'");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--repo1-host-user=xxx"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew(TEST_COMMAND_RESTORE));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            "option 'repo1-host-user' not valid without option 'repo1-host'");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--force"));
        strLstAdd(argList, strNew(TEST_COMMAND_BACKUP));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            "option 'force' not valid without option 'no-online'");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--test-delay=1"));
        strLstAdd(argList, strNew(TEST_COMMAND_BACKUP));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            "option 'test-delay' not valid without option 'test'");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--recovery-option=a=b"));
        strLstAdd(argList, strNew(TEST_COMMAND_BACKUP));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            "option 'recovery-option' not valid for command 'backup'");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--target-exclusive"));
        strLstAdd(argList, strNew(TEST_COMMAND_RESTORE));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            "option 'target-exclusive' not valid without option 'type' in ('time', 'xid')");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--type=bogus"));
        strLstAdd(argList, strNew(TEST_COMMAND_RESTORE));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidValueError,
            "'bogus' is not allowed for 'type' option");

        // Lower and upper bounds for integer ranges
        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--process-max=0"));
        strLstAdd(argList, strNew(TEST_COMMAND_RESTORE));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidValueError,
            "'0' is out of range for 'process-max' option");

        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--process-max=65536"));
        strLstAdd(argList, strNew(TEST_COMMAND_RESTORE));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidValueError,
            "'65536' is out of range for 'process-max' option");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--process-max=bogus"));
        strLstAdd(argList, strNew(TEST_COMMAND_RESTORE));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidValueError,
            "'bogus' is not valid for 'process-max' option");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--protocol-timeout=.01"));
        strLstAdd(argList, strNew(TEST_COMMAND_RESTORE));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidValueError,
            "'.01' is out of range for 'protocol-timeout' option");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--protocol-timeout=bogus"));
        strLstAdd(argList, strNew(TEST_COMMAND_RESTORE));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidValueError,
            "'bogus' is not valid for 'protocol-timeout' option");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNewFmt("--config=%s", strPtr(configFile)));
        strLstAdd(argList, strNew(TEST_COMMAND_BACKUP));

        storagePutNP(storageNewWriteNP(storageLocalWrite(), configFile), bufNewStr(strNew(
            "[global]\n"
            "compress=bogus\n"
        )));

        TEST_ERROR(configParse(
            strLstSize(argList), strLstPtr(argList), false), OptionInvalidError, "boolean option 'compress' must be 'y' or 'n'");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNewFmt("--config=%s", strPtr(configFile)));
        strLstAdd(argList, strNew(TEST_COMMAND_BACKUP));

        storagePutNP(storageNewWriteNP(storageLocalWrite(), configFile), bufNewStr(strNew(
            "[global]\n"
            "compress=\n"
        )));

        TEST_ERROR(configParse(
            strLstSize(argList), strLstPtr(argList), false), OptionInvalidValueError,
            "section 'global', key 'compress' must have a value");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNewFmt("--config=%s", strPtr(configFile)));
        strLstAdd(argList, strNew(TEST_COMMAND_BACKUP));

        storagePutNP(storageNewWriteNP(storageLocalWrite(), configFile), bufNewStr(strNew(
            "[db]\n"
            "pg1-path=/path/to/db\n"
            "db-path=/also/path/to/db\n"
        )));

        TEST_ERROR(configParse(
            strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            strPtr(strNew("configuration file contains duplicate options ('db-path', 'pg1-path') in section '[db]'")));

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNewFmt("--config=%s", strPtr(configFile)));
        strLstAdd(argList, strNew(TEST_COMMAND_BACKUP));

        storagePutNP(storageNewWriteNP(storageLocalWrite(), configFile), bufNewStr(strNew(
            "[db]\n"
            "pg1-path=/path/to/db\n"
            "pg1-path=/also/path/to/db\n"
        )));

        TEST_ERROR(configParse(strLstSize(argList), strLstPtr(argList), false),
            OptionInvalidError,
            "option 'pg1-path' cannot have multiple arguments");

        // Test that log levels are set correctly when reset is enabled, then set them back to harness defaults
        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));

        logLevelStdOut = logLevelOff;
        logLevelStdErr = logLevelOff;
        TEST_RESULT_VOID(configParse(strLstSize(argList), strLstPtr(argList), true), "no command");
        TEST_RESULT_BOOL(cfgCommandHelp(), true, "    help is set");
        TEST_RESULT_INT(cfgCommand(), cfgCmdNone, "    command is none");
        TEST_RESULT_INT(logLevelStdOut, logLevelWarn, "console logging is warn");
        TEST_RESULT_INT(logLevelStdErr, logLevelWarn, "stderr logging is warn");
        harnessLogLevelReset();

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("help"));

        TEST_RESULT_VOID(configParse(strLstSize(argList), strLstPtr(argList), false), "help command");
        TEST_RESULT_BOOL(cfgCommandHelp(), true, "    help is set");
        TEST_RESULT_INT(cfgCommand(), cfgCmdHelp, "    command is help");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("help"));
        strLstAdd(argList, strNew("version"));

        TEST_RESULT_VOID(configParse(strLstSize(argList), strLstPtr(argList), false), "help for version command");
        TEST_RESULT_BOOL(cfgCommandHelp(), true, "    help is set");
        TEST_RESULT_INT(cfgCommand(), cfgCmdVersion, "    command is version");

        // Help should not fail on missing options
        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("help"));
        strLstAdd(argList, strNew("backup"));

        TEST_RESULT_VOID(configParse(strLstSize(argList), strLstPtr(argList), false), "help for backup command");
        TEST_RESULT_BOOL(cfgCommandHelp(), true, "    help is set");
        TEST_RESULT_INT(cfgCommand(), cfgCmdBackup, "    command is backup");
        TEST_RESULT_BOOL(cfgOptionValid(cfgOptPgPath), true, "    pg1-path is valid");
        TEST_RESULT_PTR(cfgOption(cfgOptPgPath), NULL, "    pg1-path is not set");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew(TEST_COMMAND_ARCHIVE_GET));
        strLstAdd(argList, strNew("000000010000000200000003"));
        strLstAdd(argList, strNew("/path/to/wal/RECOVERYWAL"));
        TEST_RESULT_VOID(configParse(strLstSize(argList), strLstPtr(argList), false), "command arguments");
        TEST_RESULT_STR(
            strPtr(strLstJoin(cfgCommandParam(), "|")), "000000010000000200000003|/path/to/wal/RECOVERYWAL",
            "    check command arguments");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--no-online"));
        strLstAdd(argList, strNew("--no-config"));
        strLstAdd(argList, strNew(TEST_COMMAND_BACKUP));
        TEST_RESULT_VOID(configParse(strLstSize(argList), strLstPtr(argList), false), TEST_COMMAND_BACKUP " command");
        TEST_RESULT_INT(cfgCommand(), cfgCmdBackup, "    command is " TEST_COMMAND_BACKUP);

        TEST_RESULT_STR(strPtr(cfgExe()), TEST_BACKREST_EXE, "    exe is set");

        TEST_RESULT_BOOL(cfgOptionTest(cfgOptConfig), false, "    config is not set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptConfig), cfgSourceParam, "    config is source param");
        TEST_RESULT_BOOL(cfgOptionNegate(cfgOptConfig), true, "    config is negated");
        TEST_RESULT_INT(cfgOptionSource(cfgOptStanza), cfgSourceParam, "    stanza is source param");
        TEST_RESULT_STR(strPtr(cfgOptionStr(cfgOptStanza)), "db", "    stanza is set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptStanza), cfgSourceParam, "    stanza is source param");
        TEST_RESULT_STR(strPtr(cfgOptionStr(cfgOptPgPath)), "/path/to/db", "    pg1-path is set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptPgPath), cfgSourceParam, "    pg1-path is source param");
        TEST_RESULT_BOOL(cfgOptionBool(cfgOptOnline), false, "    online is not set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptOnline), cfgSourceParam, "    online is source default");
        TEST_RESULT_BOOL(cfgOptionBool(cfgOptCompress), true, "    compress is set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptCompress), cfgSourceDefault, "    compress is source default");
        TEST_RESULT_INT(cfgOptionInt(cfgOptBufferSize), 4194304, "    buffer-size is set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptBufferSize), cfgSourceDefault, "    buffer-size is source default");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNewFmt("--config=%s", strPtr(configFile)));
        strLstAdd(argList, strNew("--no-online"));
        strLstAdd(argList, strNew("--reset-pg1-host"));
        strLstAdd(argList, strNew("--reset-backup-standby"));
        strLstAdd(argList, strNew(TEST_COMMAND_BACKUP));

        storagePutNP(storageNewWriteNP(storageLocalWrite(), configFile), bufNewStr(strNew(
            "[global]\n"
            "compress-level=3\n"
            "spool-path=/path/to/spool\n"
            "\n"
            "[global:backup]\n"
            "repo1-hardlink=y\n"
            "bogus=bogus\n"
            "no-compress=y\n"
            "reset-compress=y\n"
            "archive-copy=y\n"
            "online=y\n"
            "pg1-path=/not/path/to/db\n"
            "backup-standby=y\n"
            "buffer-size=65536\n"
            "\n"
            "[db:backup]\n"
            "compress=n\n"
            "recovery-option=a=b\n"
            "\n"
            "[db]\n"
            "pg1-host=db\n"
            "pg1-path=/path/to/db\n"
            "recovery-option=c=d\n"
        )));

        TEST_RESULT_VOID(configParse(strLstSize(argList), strLstPtr(argList), false), TEST_COMMAND_BACKUP " command");
        harnessLogResult(
            strPtr(
                strNew(
                    "P00   WARN: configuration file contains option 'recovery-option' invalid for section 'db:backup'\n"
                    "P00   WARN: configuration file contains invalid option 'bogus'\n"
                    "P00   WARN: configuration file contains negate option 'no-compress'\n"
                    "P00   WARN: configuration file contains reset option 'reset-compress'\n"
                    "P00   WARN: configuration file contains command-line only option 'online'\n"
                    "P00   WARN: configuration file contains stanza-only option 'pg1-path' in global section 'global:backup'")));

        TEST_RESULT_BOOL(cfgOptionTest(cfgOptPgHost), false, "    pg1-host is not set (command line reset override)");
        TEST_RESULT_STR(strPtr(cfgOptionStr(cfgOptPgPath)), "/path/to/db", "    pg1-path is set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptPgPath), cfgSourceConfig, "    pg1-path is source config");
        TEST_RESULT_BOOL(cfgOptionBool(cfgOptCompress), false, "    compress not is set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptCompress), cfgSourceConfig, "    compress is source config");
        TEST_RESULT_BOOL(cfgOptionTest(cfgOptArchiveCheck), false, "    archive-check is not set");
        TEST_RESULT_BOOL(cfgOptionTest(cfgOptArchiveCopy), false, "    archive-copy is not set");
        TEST_RESULT_BOOL(cfgOptionBool(cfgOptRepoHardlink), true, "    repo-hardlink is set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptRepoHardlink), cfgSourceConfig, "    repo-hardlink is source config");
        TEST_RESULT_INT(cfgOptionInt(cfgOptCompressLevel), 3, "    compress-level is set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptCompressLevel), cfgSourceConfig, "    compress-level is source config");
        TEST_RESULT_BOOL(cfgOptionBool(cfgOptBackupStandby), false, "    backup-standby not is set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptBackupStandby), cfgSourceDefault, "    backup-standby is source default");
        TEST_RESULT_BOOL(cfgOptionInt64(cfgOptBufferSize), 65536, "    buffer-size is set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptBufferSize), cfgSourceConfig, "    backup-standby is source config");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNewFmt("--config=%s", strPtr(configFile)));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew("--archive-push-queue-max=4503599627370496"));
        strLstAdd(argList, strNew("--buffer-size=2MB"));
        strLstAdd(argList, strNew("archive-push"));

        storagePutNP(storageNewWriteNP(storageLocalWrite(), configFile), bufNewStr(strNew(
            "[global]\n"
            "spool-path=/path/to/spool\n"
        )));

        TEST_RESULT_VOID(configParse(strLstSize(argList), strLstPtr(argList), false), "archive-push command");

        TEST_RESULT_INT(cfgOptionInt64(cfgOptArchivePushQueueMax), 4503599627370496, "archive-push-queue-max is set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptArchivePushQueueMax), cfgSourceParam, "    archive-push-queue-max is source config");
        TEST_RESULT_INT(cfgOptionInt64(cfgOptBufferSize), 2097152, "buffer-size is set to bytes from MB");
        TEST_RESULT_INT(cfgOptionSource(cfgOptBufferSize), cfgSourceParam, "    buffer-size is source config");
        TEST_RESULT_PTR(cfgOption(cfgOptSpoolPath), NULL, "    spool-path is not set");
        TEST_RESULT_INT(cfgOptionSource(cfgOptSpoolPath), cfgSourceDefault, "    spool-path is source default");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--recovery-option=a"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew(TEST_COMMAND_RESTORE));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            "key/value 'a' not valid for 'recovery-option' option");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--recovery-option=a"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew(TEST_COMMAND_RESTORE));
        TEST_ERROR(
            configParse(strLstSize(argList), strLstPtr(argList), false), OptionInvalidError,
            "key/value 'a' not valid for 'recovery-option' option");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--db-include=abc"));
        strLstAdd(argList, strNew("--db-include=def"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew(TEST_COMMAND_RESTORE));
        TEST_RESULT_VOID(configParse(strLstSize(argList), strLstPtr(argList), false), TEST_COMMAND_RESTORE " command");

        const VariantList *includeList = NULL;
        TEST_ASSIGN(includeList, cfgOptionLst(cfgOptDbInclude), "get db include options");
        TEST_RESULT_STR(strPtr(varStr(varLstGet(includeList, 0))), "abc", "check db include option");
        TEST_RESULT_STR(strPtr(varStr(varLstGet(includeList, 1))), "def", "check db include option");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNew("--pg1-path=/path/to/db"));
        strLstAdd(argList, strNew("--recovery-option=a=b"));
        strLstAdd(argList, strNew("--recovery-option=c=de=fg hi"));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew(TEST_COMMAND_RESTORE));
        TEST_RESULT_VOID(configParse(strLstSize(argList), strLstPtr(argList), false), TEST_COMMAND_RESTORE " command");

        const KeyValue *recoveryKv = NULL;
        TEST_ASSIGN(recoveryKv, cfgOptionKv(cfgOptRecoveryOption), "get recovery options");
        TEST_RESULT_STR(strPtr(varStr(kvGet(recoveryKv, varNewStr(strNew("a"))))), "b", "check recovery option");
        TEST_RESULT_STR(strPtr(varStr(kvGet(recoveryKv, varNewStr(strNew("c"))))), "de=fg hi", "check recovery option");

        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNewFmt("--config=%s", strPtr(configFile)));
        strLstAdd(argList, strNew("--stanza=db"));
        strLstAdd(argList, strNew(TEST_COMMAND_RESTORE));

        storagePutNP(storageNewWriteNP(storageLocalWrite(), configFile), bufNewStr(strNew(
            "[global:restore]\n"
            "recovery-option=f=g\n"
            "recovery-option=hijk=l\n"
            "\n"
            "[db]\n"
            "pg1-path=/path/to/db\n"
        )));

        TEST_RESULT_VOID(configParse(strLstSize(argList), strLstPtr(argList), false), TEST_COMMAND_RESTORE " command");

        TEST_ASSIGN(recoveryKv, cfgOptionKv(cfgOptRecoveryOption), "get recovery options");
        TEST_RESULT_STR(strPtr(varStr(kvGet(recoveryKv, varNewStr(strNew("f"))))), "g", "check recovery option");
        TEST_RESULT_STR(strPtr(varStr(kvGet(recoveryKv, varNewStr(strNew("hijk"))))), "l", "check recovery option");

        // Stanza options should not be loaded for commands that don't take a stanza
        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNew(TEST_BACKREST_EXE));
        strLstAdd(argList, strNewFmt("--config=%s", strPtr(configFile)));
        strLstAdd(argList, strNew("info"));

        storagePutNP(storageNewWriteNP(storageLocalWrite(), configFile), bufNewStr(strNew(
            "[global]\n"
            "repo1-path=/path/to/repo\n"
            "\n"
            "[db]\n"
            "repo1-path=/not/the/path\n"
        )));

        TEST_RESULT_VOID(configParse(strLstSize(argList), strLstPtr(argList), false), "info command");
        TEST_RESULT_STR(strPtr(cfgOptionStr(cfgOptRepoPath)), "/path/to/repo", "check repo1-path option");
    }

    // -----------------------------------------------------------------------------------------------------------------------------
    if (testBegin("deprecated option names"))
    {
        // Repository options
        // -------------------------------------------------------------------------------------------------------------------------
        testOptionFind("hardlink", PARSE_DEPRECATE_FLAG | cfgOptRepoHardlink);
        testOptionFind("no-hardlink", PARSE_DEPRECATE_FLAG | PARSE_NEGATE_FLAG | cfgOptRepoHardlink);

        testOptionFind("archive-queue-max", PARSE_DEPRECATE_FLAG | cfgOptArchivePushQueueMax);
        testOptionFind("reset-archive-queue-max", PARSE_DEPRECATE_FLAG | PARSE_RESET_FLAG | cfgOptArchivePushQueueMax);

        testOptionFind("backup-cmd", PARSE_DEPRECATE_FLAG | cfgOptRepoHostCmd);
        testOptionFind("backup-config", PARSE_DEPRECATE_FLAG | cfgOptRepoHostConfig);
        testOptionFind("backup-host", PARSE_DEPRECATE_FLAG | cfgOptRepoHost);
        testOptionFind("backup-ssh-port", PARSE_DEPRECATE_FLAG | cfgOptRepoHostPort);
        testOptionFind("backup-user", PARSE_DEPRECATE_FLAG | cfgOptRepoHostUser);

        testOptionFind("repo-cipher-pass", PARSE_DEPRECATE_FLAG | cfgOptRepoCipherPass);
        testOptionFind("repo-cipher-type", PARSE_DEPRECATE_FLAG | cfgOptRepoCipherType);
        testOptionFind("repo-path", PARSE_DEPRECATE_FLAG | cfgOptRepoPath);
        testOptionFind("repo-type", PARSE_DEPRECATE_FLAG | cfgOptRepoType);

        testOptionFind("repo-s3-bucket", PARSE_DEPRECATE_FLAG | cfgOptRepoS3Bucket);
        testOptionFind("repo-s3-ca-file", PARSE_DEPRECATE_FLAG | cfgOptRepoS3CaFile);
        testOptionFind("repo-s3-ca-path", PARSE_DEPRECATE_FLAG | cfgOptRepoS3CaPath);
        testOptionFind("repo-s3-endpoint", PARSE_DEPRECATE_FLAG | cfgOptRepoS3Endpoint);
        testOptionFind("repo-s3-host", PARSE_DEPRECATE_FLAG | cfgOptRepoS3Host);
        testOptionFind("repo-s3-key", PARSE_DEPRECATE_FLAG | cfgOptRepoS3Key);
        testOptionFind("repo-s3-key-secret", PARSE_DEPRECATE_FLAG | cfgOptRepoS3KeySecret);
        testOptionFind("repo-s3-region", PARSE_DEPRECATE_FLAG | cfgOptRepoS3Region);
        testOptionFind("repo-s3-verify-ssl", PARSE_DEPRECATE_FLAG | cfgOptRepoS3VerifySsl);
        testOptionFind("no-repo-s3-verify-ssl", PARSE_DEPRECATE_FLAG | PARSE_NEGATE_FLAG | cfgOptRepoS3VerifySsl);

        // PostreSQL options
        // -------------------------------------------------------------------------------------------------------------------------
        testOptionFind("db-cmd", PARSE_DEPRECATE_FLAG | cfgOptPgHostCmd);
        testOptionFind("db-config", PARSE_DEPRECATE_FLAG | cfgOptPgHostConfig);
        testOptionFind("db-host", PARSE_DEPRECATE_FLAG | cfgOptPgHost);
        testOptionFind("db-path", PARSE_DEPRECATE_FLAG | cfgOptPgPath);
        testOptionFind("db-port", PARSE_DEPRECATE_FLAG | cfgOptPgPort);
        testOptionFind("db-socket-path", PARSE_DEPRECATE_FLAG | cfgOptPgSocketPath);
        testOptionFind("db-ssh-port", PARSE_DEPRECATE_FLAG | cfgOptPgHostPort);
        testOptionFind("db-user", PARSE_DEPRECATE_FLAG | cfgOptPgHostUser);

        testOptionFind("no-db-user", 0);

        for (unsigned int optionIdx = 0; optionIdx < cfgDefOptionIndexTotal(cfgDefOptPgPath); optionIdx++)
        {
            testOptionFind(strPtr(strNewFmt("db%u-cmd", optionIdx + 1)), PARSE_DEPRECATE_FLAG | (cfgOptPgHostCmd + optionIdx));
            testOptionFind(
                strPtr(strNewFmt("db%u-config", optionIdx + 1)), PARSE_DEPRECATE_FLAG | (cfgOptPgHostConfig + optionIdx));
            testOptionFind(strPtr(strNewFmt("db%u-host", optionIdx + 1)), PARSE_DEPRECATE_FLAG | (cfgOptPgHost + optionIdx));
            testOptionFind(strPtr(strNewFmt("db%u-path", optionIdx + 1)), PARSE_DEPRECATE_FLAG | (cfgOptPgPath + optionIdx));
            testOptionFind(strPtr(strNewFmt("db%u-port", optionIdx + 1)), PARSE_DEPRECATE_FLAG | (cfgOptPgPort + optionIdx));
            testOptionFind(
                strPtr(strNewFmt("db%u-socket-path", optionIdx + 1)), PARSE_DEPRECATE_FLAG | (cfgOptPgSocketPath + optionIdx));
            testOptionFind(
                strPtr(strNewFmt("db%u-ssh-port", optionIdx + 1)), PARSE_DEPRECATE_FLAG | (cfgOptPgHostPort + optionIdx));
            testOptionFind(strPtr(strNewFmt("db%u-user", optionIdx + 1)), PARSE_DEPRECATE_FLAG | (cfgOptPgHostUser + optionIdx));
        }
    }

    FUNCTION_HARNESS_RESULT_VOID();
}
