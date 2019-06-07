####################################################################################################################################
# C Storage Read Interface
####################################################################################################################################
package pgBackRest::Storage::StorageRead;

use strict;
use warnings FATAL => qw(all);
use Carp qw(confess);
use English '-no_match_vars';

use File::Basename qw(dirname);
use Fcntl qw(:mode);
use File::stat qw{lstat};
use JSON::PP;

use pgBackRest::Common::Exception;
use pgBackRest::Common::Log;
use pgBackRest::Storage::Base;

####################################################################################################################################
# new
####################################################################################################################################
sub new
{
    my $class = shift;

    # Create the class hash
    my $self = {};
    bless $self, $class;

    # Assign function parameters, defaults, and log debug info
    (
        my $strOperation,
        $self->{oStorage},
        $self->{oStorageCRead},
    ) =
        logDebugParam
        (
            __PACKAGE__ . '->new', \@_,
            {name => 'oStorage'},
            {name => 'oStorageCRead'},
        );

    # Return from function and log return values if any
    return logDebugReturn
    (
        $strOperation,
        {name => 'self', value => $self}
    );
}

####################################################################################################################################
# Get a filter result
####################################################################################################################################
sub result
{
    my $self = shift;

    # Assign function parameters, defaults, and log debug info
    my
    (
        $strOperation,
        $strClass,
    ) =
        logDebugParam
        (
            __PACKAGE__ . '->result', \@_,
            {name => 'strClass'},
        );

    my $xResult = $self->{oStorage}->{oJSON}->decode($self->{oStorageCRead}->result($strClass));

    return logDebugReturn
    (
        $strOperation,
        {name => 'xResult', value => $xResult, trace => true},
    );
}

1;
