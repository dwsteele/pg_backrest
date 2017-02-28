####################################################################################################################################
# StanzaUpgradeTest.pm - Tests for stanza-upgrade command
####################################################################################################################################
package pgBackRestTest::Stanza::StanzaUpgradeTest;
use parent 'pgBackRestTest::Common::Env::EnvHostTest';

####################################################################################################################################
# Perl includes
####################################################################################################################################
use strict;
use warnings FATAL => qw(all);
use Carp qw(confess);

use File::Basename qw(dirname);

use pgBackRest::Archive::ArchiveInfo;
use pgBackRest::BackupInfo;
use pgBackRest::DbVersion;
use pgBackRest::Common::Exception;
use pgBackRest::Common::Ini;
use pgBackRest::Common::Log;
use pgBackRest::Common::Wait;
use pgBackRest::Config::Config;
use pgBackRest::File;
use pgBackRest::FileCommon;
use pgBackRest::InfoCommon;
use pgBackRest::Manifest;

use pgBackRestTest::Common::Env::EnvHostTest;
use pgBackRestTest::Common::ExecuteTest;
use pgBackRestTest::Common::RunTest;

####################################################################################################################################
# run
####################################################################################################################################
sub run
{
    my $self = shift;

    for (my $bRemote = false; $bRemote <= true; $bRemote++)
    {
        # Increment the run, log, and decide whether this unit test should be run
        if (!$self->begin($bRemote ? "remote" : "local")) {next}

        # Create hosts, file object, and config
        my ($oHostDbMaster, $oHostDbStandby, $oHostBackup, $oFile) = $self->setup(
            true, $self->expect(), {bHostBackup => $bRemote});

        # Create the test path for pg_control
        filePathCreate(($oHostDbMaster->dbBasePath() . '/' . DB_PATH_GLOBAL), undef, false, true);

        # Copy pg_control for stanza-create
        executeTest(
            'cp ' . $self->dataPath() . '/backup.pg_control_' . WAL_VERSION_94 . '.bin ' . $oHostDbMaster->dbBasePath() . '/' .
            DB_FILE_PGCONTROL);

        # Attempt an upgrade before stanza-create has been performed
        $oHostBackup->stanzaUpgrade('fail on stanza not initialized since archive.info is missing',
            {iExpectedExitStatus => ERROR_FILE_MISSING, strOptionalParam => '--no-' . OPTION_ONLINE});

        # Create the stanza
        $oHostBackup->stanzaCreate('successfully create the stanza', {strOptionalParam => '--no-' . OPTION_ONLINE});

        # Perform a stanza upgrade which will indicate already up to date
        $oHostBackup->stanzaUpgrade('already up to date', {strOptionalParam => '--no-' . OPTION_ONLINE});

        # Remove backup.info
        $oHostBackup->executeSimple('rm ' . $oFile->pathGet(PATH_BACKUP_CLUSTER, FILE_BACKUP_INFO));

        $oHostBackup->stanzaUpgrade('fail on stanza not initialized since backup.info is missing',
            {iExpectedExitStatus => ERROR_FILE_MISSING, strOptionalParam => '--no-' . OPTION_ONLINE});

        $oHostBackup->stanzaCreate('use force to recreate the stanza',
            {strOptionalParam => '--no-' . OPTION_ONLINE . ' --' . OPTION_FORCE});

        # Modify archive.info to an older database version causing a mismatch between the archive and backup info histories
        $oHostBackup->infoMunge($oFile->pathGet(PATH_BACKUP_ARCHIVE, ARCHIVE_INFO_FILE),
            {&INFO_ARCHIVE_SECTION_DB =>
                {&INFO_ARCHIVE_KEY_DB_VERSION => '9.3', &INFO_ARCHIVE_KEY_DB_SYSTEM_ID => 6999999999999999999},
             &INFO_ARCHIVE_SECTION_DB_HISTORY =>
                {'1' =>
                    {&INFO_ARCHIVE_KEY_DB_VERSION => '9.3', &INFO_ARCHIVE_KEY_DB_ID => 6999999999999999999}}});

        # Perform a successful stanza upgrade
        $oHostBackup->stanzaUpgrade('successfully upgrade mismatched files', {strOptionalParam => '--no-' . OPTION_ONLINE});
    }
}

1;
