####################################################################################################################################
# ARCHIVE INFO MODULE
#
# The archive.info file is created when archiving begins. It is located under the stanza directory. The file contains information
# regarding the stanza database version, database WAL segment system id and other information to ensure that archiving is being
# performed on the proper database.
####################################################################################################################################
package pgBackRest::ArchiveInfo;
use parent 'pgBackRest::Common::Ini';

use strict;
use warnings FATAL => qw(all);
use Carp qw(confess);

use Exporter qw(import);
use File::Basename qw(dirname basename);
use File::stat;

use lib dirname($0);
use pgBackRest::Common::Exception;
use pgBackRest::Common::Ini;
use pgBackRest::Common::Log;
use pgBackRest::BackupInfo;
use pgBackRest::Config::Config;
use pgBackRest::File;
use pgBackRest::FileCommon;
use pgBackRest::Manifest;

####################################################################################################################################
# File/path constants
####################################################################################################################################
use constant ARCHIVE_INFO_FILE                                      => 'archive.info';
    our @EXPORT = qw(ARCHIVE_INFO_FILE);

####################################################################################################################################
# Backup info Constants
####################################################################################################################################
use constant INFO_ARCHIVE_SECTION_DB                                => INFO_BACKUP_SECTION_DB;
    push @EXPORT, qw(INFO_ARCHIVE_SECTION_DB);
use constant INFO_ARCHIVE_SECTION_DB_HISTORY                        => INFO_BACKUP_SECTION_DB_HISTORY;
    push @EXPORT, qw(INFO_ARCHIVE_SECTION_DB);

use constant INFO_ARCHIVE_KEY_DB_VERSION                            => MANIFEST_KEY_DB_VERSION;
    push @EXPORT, qw(INFO_ARCHIVE_KEY_DB_VERSION);
use constant INFO_ARCHIVE_KEY_DB_ID                                 => INFO_BACKUP_KEY_HISTORY_ID;
    push @EXPORT, qw(INFO_ARCHIVE_KEY_DB_ID);
use constant INFO_ARCHIVE_KEY_DB_SYSTEM_ID                          => MANIFEST_KEY_SYSTEM_ID;
    push @EXPORT, qw(INFO_ARCHIVE_KEY_DB_SYSTEM_ID);

####################################################################################################################################
# CONSTRUCTOR
####################################################################################################################################
sub new
{
    my $class = shift;                              # Class name

    # Assign function parameters, defaults, and log debug info
    my
    (
        $strOperation,
        $strArchiveClusterPath,                     # Backup cluster path
        $bRequired                                  # Is archive info required?
    ) =
        logDebugParam
        (
            __PACKAGE__ . '->new', \@_,
            {name => 'strArchiveClusterPath'},
            {name => 'bRequired', default => false}
        );

    # Build the archive info path/file name
    my $strArchiveInfoFile = "${strArchiveClusterPath}/" . ARCHIVE_INFO_FILE;
    my $bExists = fileExists($strArchiveInfoFile);

    if (!$bExists && $bRequired)
    {
        confess &log(ERROR, ARCHIVE_INFO_FILE . " does not exist but is required to get WAL segments\n" .
                     "HINT: is archive_command configured in postgresql.conf?\n" .
                     "HINT: use --no-archive-check to disable archive checks during backup if you have an alternate archiving" .
                     " scheme.", ERROR_FILE_MISSING);
    }

    # Init object and store variables
    my $self = $class->SUPER::new($strArchiveInfoFile, $bExists);

    $self->{bExists} = $bExists;
    $self->{strArchiveClusterPath} = $strArchiveClusterPath;

    # Return from function and log return values if any
    return logDebugReturn
    (
        $strOperation,
        {name => 'self', value => $self}
    );
}

####################################################################################################################################
# check
#
# Check archive info file and make sure it is compatible with the current version of the database for the stanza. If the file does
# not exist it will be created with the values passed.
####################################################################################################################################
sub check
{
    my $self = shift;

    # Assign function parameters, defaults, and log debug info
    my
    (
        $strOperation,
        $strDbVersion,
        $ullDbSysId
    ) =
        logDebugParam
        (
            __PACKAGE__ . '->check', \@_,
            {name => 'strDbVersion'},
            {name => 'ullDbSysId'}
        );

    my $bSave = false;

    if ($self->test(INFO_ARCHIVE_SECTION_DB))
    {
        my $strError = undef;

        if (!$self->test(INFO_ARCHIVE_SECTION_DB, INFO_ARCHIVE_KEY_DB_VERSION, undef, $strDbVersion))
        {
            $strError = "WAL segment version ${strDbVersion} does not match archive version " .
                        $self->get(INFO_ARCHIVE_SECTION_DB, INFO_ARCHIVE_KEY_DB_VERSION);
        }

        if (!$self->test(INFO_ARCHIVE_SECTION_DB, INFO_ARCHIVE_KEY_DB_SYSTEM_ID, undef, $ullDbSysId))
        {
            $strError = (defined($strError) ? ($strError . "\n") : "") .
                        "WAL segment system-id ${ullDbSysId} does not match archive system-id " .
                        $self->get(INFO_ARCHIVE_SECTION_DB, INFO_ARCHIVE_KEY_DB_SYSTEM_ID);
        }

        if (defined($strError))
        {
            confess &log(ERROR, "${strError}\nHINT: are you archiving to the correct stanza?", ERROR_ARCHIVE_MISMATCH);
        }
    }
    # Else create the info file from the parameters passed which are usually derived from the current WAL segment
    else
    {
        my $iDbId = 1;

        # Fill db section
        $self->numericSet(INFO_ARCHIVE_SECTION_DB, INFO_ARCHIVE_KEY_DB_SYSTEM_ID, undef, $ullDbSysId);
        $self->set(INFO_ARCHIVE_SECTION_DB, INFO_ARCHIVE_KEY_DB_VERSION, undef, $strDbVersion);
        $self->numericSet(INFO_ARCHIVE_SECTION_DB, INFO_ARCHIVE_KEY_DB_ID, undef, $iDbId);

        # Fill db history
        $self->numericSet(INFO_ARCHIVE_SECTION_DB_HISTORY, $iDbId, INFO_ARCHIVE_KEY_DB_ID, $ullDbSysId);
        $self->set(INFO_ARCHIVE_SECTION_DB_HISTORY, $iDbId, INFO_ARCHIVE_KEY_DB_VERSION, $strDbVersion);

        $bSave = true;
    }

    # Save if changes have been made
    if ($bSave)
    {
        $self->save();
    }

    # Return from function and log return values if any
    return logDebugReturn
    (
        $strOperation,
        {name => 'strArchiveId', value => $self->archiveId()}
    );
}


####################################################################################################################################
# archiveId
#
# Get the archive id which is a combination of the DB version and the db-id setting (e.g. 9.4-1)
####################################################################################################################################
sub archiveId
{
    my $self = shift;

    # Assign function parameters, defaults, and log debug info
    my ($strOperation) = logDebugParam(__PACKAGE__ . '->archiveId');

    # Return from function and log return values if any
    return logDebugReturn
    (
        $strOperation,
        {name => 'strArchiveId', value => $self->get(INFO_ARCHIVE_SECTION_DB, INFO_ARCHIVE_KEY_DB_VERSION) . "-" .
                                          $self->get(INFO_ARCHIVE_SECTION_DB, INFO_ARCHIVE_KEY_DB_ID)}
    );
}

1;
