####################################################################################################################################
# CIFS Storage Driver
#
# Implements storage functions for Posix-compliant file systems.
####################################################################################################################################
package pgBackRest::Storage::Cifs::Driver;
use parent 'pgBackRest::Storage::Posix::Driver';

use strict;
use warnings FATAL => qw(all);
use Carp qw(confess);
use English '-no_match_vars';

use Exporter qw(import);
    our @EXPORT = qw();

use pgBackRest::Common::Log;
use pgBackRest::Storage::Base;

####################################################################################################################################
# Package name constant
####################################################################################################################################
use constant STORAGE_CIFS_DRIVER                                    => __PACKAGE__;
    push @EXPORT, qw(STORAGE_CIFS_DRIVER);

####################################################################################################################################
# pathSync - CIFS does not support path sync so this is a noop
####################################################################################################################################
sub pathSync
{
    my $self = shift;

    # Assign function parameters, defaults, and log debug info
    my
    (
        $strOperation,
        $strPath,
    ) =
        logDebugParam
        (
            __PACKAGE__ . '->pathSync', \@_,
            {name => 'strPath', trace => true},
        );

    # Return from function and log return values if any
    return logDebugReturn($strOperation);
}

####################################################################################################################################
# Getters/Setters
####################################################################################################################################
sub capability {shift eq STORAGE_CAPABILITY_SIZE_DIFF ? true : false}
sub className {STORAGE_CIFS_DRIVER}

1;
