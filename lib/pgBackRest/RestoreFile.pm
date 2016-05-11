####################################################################################################################################
# RESTORE FILE MODULE
####################################################################################################################################
package pgBackRest::RestoreFile;

use threads;
use threads::shared;
use Thread::Queue;
use strict;
use warnings FATAL => qw(all);
use Carp qw(confess);

use Exporter qw(import);
    our @EXPORT = qw();
use Fcntl qw(O_WRONLY O_CREAT O_TRUNC);
use File::Basename qw(dirname);
use File::stat qw(lstat);

use lib dirname($0);
use pgBackRest::Common::Exception;
use pgBackRest::Common::Log;
use pgBackRest::Common::String;
use pgBackRest::Config::Config;
use pgBackRest::File;
use pgBackRest::FileCommon;
use pgBackRest::Manifest;

####################################################################################################################################
# Operation constants
####################################################################################################################################
use constant OP_RESTORE_FILE                                        => 'RestoreFile';

use constant OP_RESTORE_FILE_RESTORE_FILE                           => OP_RESTORE_FILE . '::restoreFile';

####################################################################################################################################
# restoreFile
#
# Restores a single file.
####################################################################################################################################
sub restoreFile
{
    my $oFileHash = shift;          # File to restore

    # Assign function parameters, defaults, and log debug info
    my
    (
        $strOperation,
        $lCopyTimeBegin,                            # Time that the backup begain - used for size/timestamp deltas
        $bDelta,                                    # Is restore a delta?
        $bForce,                                    # Force flag
        $strBackupPath,                             # Backup path
        $bSourceCompression,                        # Is the source compressed?
        $strCurrentUser,                            # Current OS user
        $strCurrentGroup,                           # Current OS group
        $oFile,                                     # File object
        $lSizeTotal,                                # Total size of files to be restored
        $lSizeCurrent                               # Current size of files restored
    ) =
        logDebugParam
        (
            OP_RESTORE_FILE_RESTORE_FILE, \@_,
            {name => 'lCopyTimeBegin', trace => true},
            {name => 'bDelta', trace => true},
            {name => 'bForce', trace => true},
            {name => 'strBackupPath', trace => true},
            {name => 'bSourceCompression', trace => true},
            {name => 'strCurrentUser', trace => true},
            {name => 'strCurrentGroup', trace => true},
            {name => 'oFile', trace => true},
            {name => 'lSizeTotal', default => 0, trace => true},
            {name => 'lSizeCurrent', required => false, trace => true}
        );

    # Add the size of the current file to keep track of percent complete
    if ($lSizeTotal > 0)
    {
        $lSizeCurrent += $$oFileHash{size};
    }

    # Copy flag and log message
    my $bCopy = true;
    my $bZero = false;
    my $strLog;

    if (defined($$oFileHash{zero}) && $$oFileHash{zero})
    {
        $bCopy = false;
        $bZero = true;

        # Open the file truncating to zero bytes in case it already exists
        my $hFile = fileOpen($$oFileHash{db_file}, O_WRONLY | O_CREAT | O_TRUNC);

        # Now truncate to the original size.  This will create a sparse file which is very efficient for this use case.
        truncate($hFile, $$oFileHash{size});

        # Sync the file
        $hFile->sync()
            or confess &log(ERROR, "unable to sync $$oFileHash{db_file}", ERROR_FILE_SYNC);

        # Close the file
        close($hFile)
            or confess &log(ERROR, "unable to close $$oFileHash{db_file}", ERROR_FILE_CLOSE);

        # Fix the timestamp - not really needed in this case but good for testing
        utime($$oFileHash{modification_time}, $$oFileHash{modification_time}, $$oFileHash{db_file})
            or confess &log(ERROR, "unable to set time for $$oFileHash{db_file}");

        # Set file mode
        chmod(oct($$oFileHash{mode}), $$oFileHash{db_file})
            or confess &log(ERROR, "unable to set mode for $$oFileHash{db_file}");

        # Set file ownership
        $oFile->owner(PATH_DB_ABSOLUTE, $$oFileHash{db_file}, $$oFileHash{user}, $$oFileHash{group});
    }
    elsif ($oFile->exists(PATH_DB_ABSOLUTE, $$oFileHash{db_file}))
    {
        # Perform delta if requested
        if ($bDelta)
        {
            # If force then use size/timestamp delta
            if ($bForce)
            {
                my $oStat = lstat($$oFileHash{db_file});

                # Make sure that timestamp/size are equal and that timestamp is before the copy start time of the backup
                if (defined($oStat) && $oStat->size == $$oFileHash{size} &&
                    $oStat->mtime == $$oFileHash{modification_time} && $oStat->mtime < $lCopyTimeBegin)
                {
                    $strLog =  'exists and matches size ' . $oStat->size . ' and modification time ' . $oStat->mtime;
                    $bCopy = false;
                }
            }
            else
            {
                my ($strChecksum, $lSize) = $oFile->hashSize(PATH_DB_ABSOLUTE, $$oFileHash{db_file});

                if ($lSize == $$oFileHash{size} && ($lSize == 0 || $strChecksum eq $$oFileHash{checksum}))
                {
                    $strLog =  'exists and ' . ($lSize == 0 ? 'is zero size' : "matches backup");

                    # Even if hash is the same set the time back to backup time.  This helps with unit testing, but also
                    # presents a pristine version of the database after restore.
                    utime($$oFileHash{modification_time}, $$oFileHash{modification_time}, $$oFileHash{db_file})
                        or confess &log(ERROR, "unable to set time for $$oFileHash{db_file}");

                    $bCopy = false;
                }
            }
        }
    }

    # Copy the file from the backup to the database
    if ($bCopy)
    {
        my ($bCopyResult, $strCopyChecksum, $lCopySize) =
            $oFile->copy(PATH_BACKUP_CLUSTER, (defined($$oFileHash{reference}) ? $$oFileHash{reference} : $strBackupPath) .
                         "/$$oFileHash{repo_file}" .
                         ($bSourceCompression ? '.' . $oFile->{strCompressExtension} : ''),
                         PATH_DB_ABSOLUTE, $$oFileHash{db_file},
                         $bSourceCompression,   # Source is compressed based on backup settings
                         undef, undef,
                         $$oFileHash{modification_time},
                         $$oFileHash{mode},
                         undef,
                         $$oFileHash{user},
                         $$oFileHash{group});

        if ($lCopySize != 0 && $strCopyChecksum ne $$oFileHash{checksum})
        {
            confess &log(ERROR, "error restoring $$oFileHash{db_file}: actual checksum ${strCopyChecksum} " .
                                "does not match expected checksum $$oFileHash{checksum}", ERROR_CHECKSUM);
        }
    }

    # Log the restore
    &log($bCopy ? INFO : DETAIL,
         'restore' . ($bZero ? ' zeroed' : '') .
         " file $$oFileHash{db_file}" . (defined($strLog) ? " - ${strLog}" : '') .
         ' (' . fileSizeFormat($$oFileHash{size}) .
         ($lSizeTotal > 0 ? ', ' . int($lSizeCurrent * 100 / $lSizeTotal) . '%' : '') . ')' .
         ($$oFileHash{size} != 0 && !$bZero ? " checksum $$oFileHash{checksum}" : ''));

    # Return from function and log return values if any
    return logDebugReturn
    (
        $strOperation,
        {name => 'lSizeCurrent', value => $lSizeCurrent, trace => true}
    );
}

push @EXPORT, qw(restoreFile);

1;
