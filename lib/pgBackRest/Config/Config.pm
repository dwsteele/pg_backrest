####################################################################################################################################
# CONFIG MODULE
####################################################################################################################################
package pgBackRest::Config::Config;

use strict;
use warnings FATAL => qw(all);
use Carp qw(confess);
use English '-no_match_vars';

use Exporter qw(import);
    our @EXPORT = qw();
use JSON::PP;

use pgBackRest::Common::Exception;
use pgBackRest::Common::Ini;
use pgBackRest::Common::Io::Base;
use pgBackRest::Common::Log;
use pgBackRest::Common::String;
use pgBackRest::Common::Wait;
use pgBackRest::LibC qw(:config :configDefine);
use pgBackRest::Version;

####################################################################################################################################
# Export config constants and functions
####################################################################################################################################
push(@EXPORT, @{$pgBackRest::LibC::EXPORT_TAGS{config}});
push(@EXPORT, @{$pgBackRest::LibC::EXPORT_TAGS{configDefine}});

####################################################################################################################################
# SOURCE Constants
####################################################################################################################################
use constant CFGDEF_SOURCE_CONFIG                                   => 'config';
    push @EXPORT, qw(CFGDEF_SOURCE_CONFIG);
use constant CFGDEF_SOURCE_PARAM                                    => 'param';
    push @EXPORT, qw(CFGDEF_SOURCE_PARAM);
use constant CFGDEF_SOURCE_DEFAULT                                  => 'default';
    push @EXPORT, qw(CFGDEF_SOURCE_DEFAULT);

####################################################################################################################################
# Configuration section constants
####################################################################################################################################
use constant CFGDEF_SECTION_GLOBAL                                  => 'global';
    push @EXPORT, qw(CFGDEF_SECTION_GLOBAL);
use constant CFGDEF_SECTION_STANZA                                  => 'stanza';
    push @EXPORT, qw(CFGDEF_SECTION_STANZA);

####################################################################################################################################
# Module variables
####################################################################################################################################
my %oOption;                # Option hash
my $strCommand;             # Command (backup, archive-get, ...)
my $bInitLog = false;       # Has logging been initialized yet?

####################################################################################################################################
# configLogging - configure logging based on options
####################################################################################################################################
sub configLogging
{
    my $bLogInitForce = shift;

    if ($bInitLog || (defined($bLogInitForce) && $bLogInitForce))
    {
        logLevelSet(
            cfgOptionValid(CFGOPT_LOG_LEVEL_FILE) ? cfgOption(CFGOPT_LOG_LEVEL_FILE) : OFF,
            cfgOptionValid(CFGOPT_LOG_LEVEL_CONSOLE) ? cfgOption(CFGOPT_LOG_LEVEL_CONSOLE) : OFF,
            cfgOptionValid(CFGOPT_LOG_LEVEL_STDERR) ? cfgOption(CFGOPT_LOG_LEVEL_STDERR) : OFF,
            cfgOptionValid(CFGOPT_LOG_TIMESTAMP) ? cfgOption(CFGOPT_LOG_TIMESTAMP) : undef,
            cfgOptionValid(CFGOPT_PROCESS_MAX) ? cfgOption(CFGOPT_PROCESS_MAX) : undef);

        $bInitLog = true;
    }
}

push @EXPORT, qw(configLogging);

####################################################################################################################################
# Load configuration
#
# Additional conditions that cannot be codified by the option definitions are also tested here.
####################################################################################################################################
sub configLoad
{
    my $bInitLogging = shift;
    my $strBackRestBin = shift;
    my $strCommandName = shift;
    my $rstrConfigJson = shift;

    # Set backrest bin
    projectBinSet($strBackRestBin);

    # Set command
    $strCommand = $strCommandName;

    eval
    {
        %oOption = %{(JSON::PP->new()->allow_nonref())->decode($$rstrConfigJson)};
        return true;
    }
    or do
    {
        confess &log(ASSERT, "unable to parse config JSON");
    };

    # Load options into final option hash
    for (my $iOptionId = 0; $iOptionId < cfgOptionTotal(); $iOptionId++)
    {
        my $strOptionName = cfgOptionName($iOptionId);

        # If option is defined it is valid
        if (defined($oOption{$strOptionName}))
        {
            # Convert JSON bool to standard bool that Perl understands
            if (cfgDefOptionType($iOptionId) eq CFGDEF_TYPE_BOOLEAN && defined($oOption{$strOptionName}{value}))
            {
                $oOption{$strOptionName}{value} = $oOption{$strOptionName}{value} eq INI_TRUE ? true : false;
            }
        }
        # Else it is not valid
        else
        {
            $oOption{$strOptionName}{valid} = false;
        }
    }

    # If this is not the remote and logging is allowed (to not overwrite log levels for tests) then set the log level so that
    # INFO/WARN messages can be displayed (the user may still disable them).  This should be run before any WARN logging is
    # generated.
    if (!defined($bInitLogging) || $bInitLogging)
    {
        configLogging(true);
    }

    return true;
}

push @EXPORT, qw(configLoad);

####################################################################################################################################
# cfgOptionIdFromIndex - return name for options that can be indexed (e.g. pg1-host, pg2-host).
####################################################################################################################################
sub cfgOptionIdFromIndex
{
    my $iOptionId = shift;
    my $iIndex = shift;

    # If the option doesn't have a prefix it can't be indexed
    $iIndex = defined($iIndex) ? $iIndex : 1;
    my $strPrefix = cfgDefOptionPrefix($iOptionId);

    if (!defined($strPrefix))
    {
        if ($iIndex > 1)
        {
            confess &log(ASSERT, "'" . cfgOptionName($iOptionId) . "' option does not allow indexing");
        }

        return $iOptionId;
    }

    return cfgOptionId("${strPrefix}${iIndex}" . substr(cfgOptionName($iOptionId), index(cfgOptionName($iOptionId), '-')));
}

push @EXPORT, qw(cfgOptionIdFromIndex);

####################################################################################################################################
# cfgOptionSource - how was the option set?
####################################################################################################################################
sub cfgOptionSource
{
    my $iOptionId = shift;

    cfgOptionValid($iOptionId, true);

    return $oOption{cfgOptionName($iOptionId)}{source};
}

push @EXPORT, qw(cfgOptionSource);

####################################################################################################################################
# cfgOptionValid - is the option valid for the current command?
####################################################################################################################################
sub cfgOptionValid
{
    my $iOptionId = shift;
    my $bError = shift;

    # If defined then this is the command help is being generated for so all valid checks should be against that command
    my $iCommandId;

    if (defined($strCommand))
    {
        $iCommandId = cfgCommandId($strCommand);
    }

    if (defined($iCommandId) && cfgDefOptionValid($iCommandId, $iOptionId))
    {
        return true;
    }

    if (defined($bError) && $bError)
    {
        my $strOption = cfgOptionName($iOptionId);

        if (!defined($oOption{$strOption}))
        {
            confess &log(ASSERT, "option '${strOption}' does not exist");
        }

        confess &log(ASSERT, "option '${strOption}' not valid for command '" . cfgCommandName(cfgCommandGet()) . "'");
    }

    return false;
}

push @EXPORT, qw(cfgOptionValid);

####################################################################################################################################
# cfgOption - get option value
####################################################################################################################################
sub cfgOption
{
    my $iOptionId = shift;
    my $bRequired = shift;

    cfgOptionValid($iOptionId, true);

    my $strOption = cfgOptionName($iOptionId);

    if (!defined($oOption{$strOption}{value}) && (!defined($bRequired) || $bRequired))
    {
        confess &log(ASSERT, "option ${strOption} is required");
    }

    return $oOption{$strOption}{value};
}

push @EXPORT, qw(cfgOption);

####################################################################################################################################
# cfgOptionDefault - get option default value
####################################################################################################################################
sub cfgOptionDefault
{
    my $iOptionId = shift;

    cfgOptionValid($iOptionId, true);

    return cfgDefOptionDefault(cfgCommandId($strCommand), $iOptionId);
}

push @EXPORT, qw(cfgOptionDefault);

####################################################################################################################################
# cfgOptionSet - set option value and source
####################################################################################################################################
sub cfgOptionSet
{
    my $iOptionId = shift;
    my $oValue = shift;
    my $bForce = shift;

    my $strOption = cfgOptionName($iOptionId);

    if (!cfgOptionValid($iOptionId, !defined($bForce) || !$bForce))
    {
        $oOption{$strOption}{valid} = true;
    }

    $oOption{$strOption}{source} = CFGDEF_SOURCE_PARAM;
    $oOption{$strOption}{value} = $oValue;
}

push @EXPORT, qw(cfgOptionSet);

####################################################################################################################################
# cfgOptionTest - test if an option exists or has a specific value
####################################################################################################################################
sub cfgOptionTest
{
    my $iOptionId = shift;
    my $strValue = shift;

    if (!cfgOptionValid($iOptionId))
    {
        return false;
    }

    if (defined($strValue))
    {
        return cfgOption($iOptionId) eq $strValue ? true : false;
    }

    return defined($oOption{cfgOptionName($iOptionId)}{value}) ? true : false;
}

push @EXPORT, qw(cfgOptionTest);

####################################################################################################################################
# cfgCommandGet - get the current command
####################################################################################################################################
sub cfgCommandGet
{
    return cfgCommandId($strCommand);
}

push @EXPORT, qw(cfgCommandGet);

####################################################################################################################################
# cfgCommandTest - test that the current command is equal to the provided value
####################################################################################################################################
sub cfgCommandTest
{
    my $iCommandIdTest = shift;

    return cfgCommandName($iCommandIdTest) eq $strCommand;
}

push @EXPORT, qw(cfgCommandTest);

####################################################################################################################################
# Set current command
####################################################################################################################################
sub cfgCommandSet
{
    my $iCommandId = shift;

    $strCommand = cfgCommandName($iCommandId);
}

push @EXPORT, qw(cfgCommandSet);

####################################################################################################################################
# cfgCommandWrite - using the options for the current command, write the command string for another command
#
# For example, this can be used to write the archive-get command for recovery.conf during a restore.
####################################################################################################################################
sub cfgCommandWrite
{
    my $iNewCommandId = shift;
    my $bIncludeConfig = shift;
    my $strExeString = shift;
    my $bIncludeCommand = shift;
    my $oOptionOverride = shift;
    my $bDisplayOnly = shift;

    # Set defaults
    $strExeString = defined($strExeString) ? $strExeString : projectBin();
    $bIncludeConfig = defined($bIncludeConfig) ? $bIncludeConfig : false;
    $bIncludeCommand = defined($bIncludeCommand) ? $bIncludeCommand : true;

    # Iterate the options to figure out which ones are not default and need to be written out to the new command string
    for (my $iOptionId = 0; $iOptionId < cfgOptionTotal(); $iOptionId++)
    {
        my $strOption = cfgOptionName($iOptionId);
        my $bSecure = cfgDefOptionSecure($iOptionId);

        # Skip option if it is secure and should not be output in logs or the command line
        next if (($bSecure || $iOptionId == CFGOPT_REPO_CIPHER_TYPE) && !$bDisplayOnly);

        # Process any option id overrides first
        if (defined($oOptionOverride->{$iOptionId}))
        {
            if (defined($oOptionOverride->{$iOptionId}{value}))
            {
                $strExeString .= cfgCommandWriteOptionFormat(
                    $strOption, false, $bSecure, {value => $oOptionOverride->{$iOptionId}{value}});
            }
        }
        # And process overrides passed by string - this is used by Perl compatibility functions
        elsif (defined($oOptionOverride->{$strOption}))
        {
            if (defined($oOptionOverride->{$strOption}{value}))
            {
                $strExeString .= cfgCommandWriteOptionFormat(
                    $strOption, false, $bSecure, {value => $oOptionOverride->{$strOption}{value}});
            }
        }
        # else look for non-default options in the current configuration
        elsif (cfgDefOptionValid($iNewCommandId, $iOptionId) &&
               defined($oOption{$strOption}{value}) &&
               ($bIncludeConfig ?
                    $oOption{$strOption}{source} ne CFGDEF_SOURCE_DEFAULT : $oOption{$strOption}{source} eq CFGDEF_SOURCE_PARAM))
        {
            my $oValue;
            my $bMulti = false;

            # If this is a hash then it will break up into multple command-line options
            if (ref($oOption{$strOption}{value}) eq 'HASH')
            {
                $oValue = $oOption{$strOption}{value};
                $bMulti = true;
            }
            # Else a single value but store it in a hash anyway to make processing below simpler
            else
            {
                $oValue = {value => $oOption{$strOption}{value}};
            }

            $strExeString .= cfgCommandWriteOptionFormat($strOption, $bMulti, $bSecure, $oValue);
        }
        # Else is reset
        elsif (cfgDefOptionValid($iNewCommandId, $iOptionId) && $oOption{$strOption}{reset})
        {
            $strExeString .= " --reset-${strOption}";
        }
    }

    if ($bIncludeCommand)
    {
        $strExeString .= ' ' . cfgCommandName($iNewCommandId);
    }

    return $strExeString;
}

push @EXPORT, qw(cfgCommandWrite);

# Helper function for cfgCommandWrite() to correctly format options for command-line usage
sub cfgCommandWriteOptionFormat
{
    my $strOption = shift;
    my $bMulti = shift;
    my $bSecure = shift;
    my $oValue = shift;

    # Loops though all keys in the hash
    my $strOptionFormat = '';
    my $strParam;

    foreach my $strKey (sort(keys(%$oValue)))
    {
        # Get the value - if the original value was a hash then the key must be prefixed
        my $strValue = $bSecure ? '<redacted>' : ($bMulti ?  "${strKey}=" : '') . $$oValue{$strKey};

        # Handle the no- prefix for boolean values
        if (cfgDefOptionType(cfgOptionId($strOption)) eq CFGDEF_TYPE_BOOLEAN)
        {
            $strParam = '--' . ($strValue ? '' : 'no-') . $strOption;
        }
        else
        {
            $strParam = "--${strOption}=${strValue}";
        }

        # Add quotes if the value has spaces in it
        $strOptionFormat .= ' ' . (index($strValue, " ") != -1 ? "\"${strParam}\"" : $strParam);
    }

    return $strOptionFormat;
}

1;
