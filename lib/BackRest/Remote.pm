####################################################################################################################################
# REMOTE MODULE
####################################################################################################################################
package BackRest::Remote;

use threads;
use strict;
use warnings;
use Carp;

use Moose;
use Thread::Queue;
use Net::OpenSSH;
use File::Basename;
use IO::Handle;
use POSIX ':sys_wait_h';
use IO::Compress::Gzip qw(gzip $GzipError);
use IO::Uncompress::Gunzip qw(gunzip $GunzipError);

use lib dirname($0) . '/../lib';
use BackRest::Exception;
use BackRest::Utility;

####################################################################################################################################
# Remote xfer default block size constant
####################################################################################################################################
use constant
{
    DEFAULT_BLOCK_SIZE  => 1048576
};

####################################################################################################################################
# Module variables
####################################################################################################################################
# Protocol strings
has strGreeting => (is => 'ro', default => 'PG_BACKREST_REMOTE');

# Command strings
has strCommand => (is => 'bare');

# Module variables
has strHost => (is => 'bare');            # Host host
has strUser => (is => 'bare');            # User user
has oSSH => (is => 'bare');               # SSH object

# Process variables
has pId => (is => 'bare');                # Process Id
has hIn => (is => 'bare');                # Input stream
has hOut => (is => 'bare');               # Output stream
has hErr => (is => 'bare');               # Error stream

# Thread variables
has iThreadIdx => (is => 'bare');         # Thread index
has oThread => (is => 'bare');            # Thread object
has oThreadQueue => (is => 'bare');       # Thread queue object
has oThreadResult => (is => 'bare');      # Thread result object

# Block size
has iBlockSize => (is => 'bare', default => DEFAULT_BLOCK_SIZE);  # Set block size to default

####################################################################################################################################
# CONSTRUCTOR
####################################################################################################################################
sub BUILD
{
    my $self = shift;

    $self->{strGreeting} .= ' ' . version_get();

    if (defined($self->{strHost}))
    {
        # User must be defined
        if (!defined($self->{strUser}))
        {
            confess &log(ASSERT, 'strUser must be defined');
        }

        # User must be defined
        if (!defined($self->{strCommand}))
        {
            confess &log(ASSERT, 'strCommand must be defined');
        }

        # Set SSH Options
        my $strOptionSSHRequestTTY = 'RequestTTY=yes';
        my $strOptionSSHCompression = 'Compression=no';

        &log(TRACE, 'connecting to remote ssh host ' . $self->{strHost});

        # Make SSH connection
        $self->{oSSH} = Net::OpenSSH->new($self->{strHost}, timeout => 300, user => $self->{strUser},
                                  master_opts => [-o => $strOptionSSHCompression, -o => $strOptionSSHRequestTTY]);

        $self->{oSSH}->error and confess &log(ERROR, "unable to connect to $self->{strHost}: " . $self->{oSSH}->error);

        # Execute remote command
        ($self->{hIn}, $self->{hOut}, $self->{hErr}, $self->{pId}) = $self->{oSSH}->open3($self->{strCommand});

        $self->greeting_read();
    }

    $self->{oThreadQueue} = Thread::Queue->new();
    $self->{oThreadResult} = Thread::Queue->new();
    $self->{oThread} = threads->create(\&binary_xfer_thread, $self);
}

####################################################################################################################################
# thread_kill
####################################################################################################################################
sub thread_kill
{
    my $self = shift;

    if (defined($self->{oThread}))
    {
        $self->{oThreadQueue}->enqueue(undef);
        $self->{oThread}->join();
        $self->{oThread} = undef;
    }
}

####################################################################################################################################
# DESTRUCTOR
####################################################################################################################################
sub DEMOLISH
{
    my $self = shift;

    $self->thread_kill();

    if (defined($self->{oCompressAsync}))
    {
        $self->{oCompressAsync} = undef;
    }
}

####################################################################################################################################
# CLONE
####################################################################################################################################
sub clone
{
    my $self = shift;
    my $iThreadIdx = shift;

    return BackRest::Remote->new
    (
        strCommand => $self->{strCommand},
        strHost => $self->{strHost},
        strUser => $self->{strUser},
        iBlockSize => $self->{iBlockSize},
        iThreadIdx => $iThreadIdx
    );
}

####################################################################################################################################
# COMPRESS_ASYNC_GET
####################################################################################################################################
sub compress_async_get
{
    my $self = shift;

    if (!defined($self->{oCompressAsync}))
    {
        $self->{oCompressAsync} = new BackRest::ProcessAsync;
    }

    return $self->{oCompressAsync};
}

####################################################################################################################################
# GREETING_READ
#
# Read the greeting and make sure it is as expected.
####################################################################################################################################
sub greeting_read
{
    my $self = shift;

    # Make sure that the remote is running the right version
    if ($self->read_line($self->{hOut}) ne $self->{strGreeting})
    {
        confess &log(ERROR, 'remote version mismatch');
    }
}

####################################################################################################################################
# GREETING_WRITE
#
# Send a greeting to the master process.
####################################################################################################################################
sub greeting_write
{
    my $self = shift;

    if (!syswrite(*STDOUT, "$self->{strGreeting}\n"))
    {
        confess 'unable to write greeting';
    }
}

####################################################################################################################################
# STRING_WRITE
#
# Write a string.
####################################################################################################################################
sub string_write
{
    my $self = shift;
    my $hOut = shift;
    my $strBuffer = shift;

    $strBuffer =~ s/\n/\n\./g;

    if (!syswrite($hOut, '.' . $strBuffer))
    {
        confess 'unable to write string';
    }
}

####################################################################################################################################
# PIPE_TO_STRING Function
#
# Copies data from a file handle into a string.
####################################################################################################################################
sub pipe_to_string
{
    my $self = shift;
    my $hOut = shift;

    my $strBuffer;
    my $hString = IO::String->new($strBuffer);
    $self->binary_xfer($hOut, $hString);

    return $strBuffer;
}

####################################################################################################################################
# ERROR_WRITE
#
# Write errors with error codes in protocol format, otherwise write to stderr and exit with error.
####################################################################################################################################
sub error_write
{
    my $self = shift;
    my $oMessage = shift;

    my $iCode;
    my $strMessage;

    if (blessed($oMessage))
    {
        if ($oMessage->isa('BackRest::Exception'))
        {
            $iCode = $oMessage->code();
            $strMessage = $oMessage->message();
        }
        else
        {
            syswrite(*STDERR, 'unknown error object: ' . $oMessage);
            exit 1;
        }
    }
    else
    {
        syswrite(*STDERR, $oMessage);
        exit 1;
    }

    if (defined($strMessage))
    {
        $self->string_write(*STDOUT, trim($strMessage));
    }

    if (!syswrite(*STDOUT, "\nERROR" . (defined($iCode) ? " $iCode" : '') . "\n"))
    {
        confess 'unable to write error';
    }
}

####################################################################################################################################
# READ_LINE
#
# Read a line.
####################################################################################################################################
sub read_line
{
    my $self = shift;
    my $hIn = shift;
    my $bError = shift;

    my $strLine;
    my $strChar;
    my $iByteIn;

    while (1)
    {
        $iByteIn = sysread($hIn, $strChar, 1);

        if (!defined($iByteIn) || $iByteIn != 1)
        {
            $self->wait_pid();

            if (defined($bError) and !$bError)
            {
                return undef;
            }

            confess &log(ERROR, 'unable to read 1 byte' . (defined($!) ? ': ' . $! : ''));
        }

        if ($strChar eq "\n")
        {
            last;
        }

        $strLine .= $strChar;
    }

    return $strLine;
}

####################################################################################################################################
# WRITE_LINE
#
# Write a line data
####################################################################################################################################
sub write_line
{
    my $self = shift;
    my $hOut = shift;
    my $strBuffer = shift;

    $strBuffer = $strBuffer . "\n";

    my $iLineOut = syswrite($hOut, $strBuffer, length($strBuffer));

    if (!defined($iLineOut) || $iLineOut != length($strBuffer))
    {
        confess 'unable to write ' . length($strBuffer) . ' byte(s)';
    }
}

####################################################################################################################################
# WAIT_PID
#
# See if the remote process has terminated unexpectedly.
####################################################################################################################################
sub wait_pid
{
    my $self = shift;

    if (defined($self->{pId}) && waitpid($self->{pId}, WNOHANG) != 0)
    {
        my $strError = 'no error on stderr';

        if (!defined($self->{hErr}))
        {
            $strError = 'no error captured because stderr is already closed';
        }
        else
        {
            $strError = $self->pipe_to_string($self->{hErr});
        }

        $self->{pId} = undef;
        $self->{hIn} = undef;
        $self->{hOut} = undef;
        $self->{hErr} = undef;

        confess &log(ERROR, "remote process terminated: ${strError}");
    }
}

####################################################################################################################################
# BINARY_XFER_THREAD
#
# De/Compresses data on a thread.
####################################################################################################################################
# sub binary_xfer_thread
# {
#     my $self = shift;
#
#     while (my $strMessage = $self->{oThreadQueue}->dequeue())
#     {
#         my @stryMessage = split(':', $strMessage);
#         my @strHandle = split(',', $stryMessage[1]);
#
#         my $hIn = IO::Handle->new_from_fd($strHandle[0], '<');
#         my $hOut = IO::Handle->new_from_fd($strHandle[1], '>');
#
#         $self->{oThreadResult}->enqueue('running');
#
#         if ($stryMessage[0] eq 'compress')
#         {
#             gzip($hIn => $hOut)
#                 or confess &log(ERROR, 'unable to compress: ' . $GzipError);
#         }
#         else
#         {
#             gunzip($hIn => $hOut)
#                 or die confess &log(ERROR, 'unable to uncompress: ' . $GunzipError);
#         }
#
#         close($hOut);
#
#         $self->{oThreadResult}->enqueue('complete');
#     }
# }

####################################################################################################################################
# BINARY_XFER
#
# Copies data from one file handle to another, optionally compressing or decompressing the data in stream.
####################################################################################################################################
sub binary_xfer
{
    my $self = shift;
    my $hIn = shift;
    my $hOut = shift;
    my $strRemote = shift;
    my $bSourceCompressed = shift;
    my $bDestinationCompress = shift;

    # If no remote is defined then set to none
    if (!defined($strRemote))
    {
        $strRemote = 'none';
    }
    # Only set compression defaults when remote is defined
    else
    {
        $bSourceCompressed = defined($bSourceCompressed) ? $bSourceCompressed : false;
        $bDestinationCompress = defined($bDestinationCompress) ? $bDestinationCompress : false;
    }

    # Working variables
    my $iBlockSize = $self->{iBlockSize};
    my $iBlockIn;
    my $iBlockInTotal = $iBlockSize;
    my $iBlockOut;
    my $iBlockTotal = 0;
    my $strBlockHeader;
    my $strBlock;
    my $oGzip;
    my $hPipeIn;
    my $hPipeOut;
    my $pId;
    my $bThreadRunning = false;

    # Both the in and out streams must be defined
    if (!defined($hIn) || !defined($hOut))
    {
        confess &log(ASSERT, 'hIn or hOut is not defined');
    }

    # If this is output and the source is not already compressed
    if ($strRemote eq 'out' && !$bSourceCompressed)
    {
        # Increase the blocksize since we are compressing
        $iBlockSize *= 4;

        # Open the in/out pipes
        pipe $hPipeOut, $hPipeIn;

        # Queue the compression job with the thread
        $self->{oThreadQueue}->enqueue('compress:' . fileno($hIn) . ',' . fileno($hPipeIn));

        # Wait for the thread to acknowledge that it has duplicated the file handles
        my $strMessage = $self->{oThreadResult}->dequeue();

        # Close input pipe so that thread has the only copy, reset hIn to hPipeOut
        if ($strMessage eq 'running')
        {
            close($hPipeIn);
            $hIn = $hPipeOut;
        }
        # If any other message is returned then error
        else
        {
            confess "unknown thread message while waiting for running: ${strMessage}";
        }

        $bThreadRunning = true;
    }
    # Spawn a child process to do decompression
    elsif ($strRemote eq 'in' && !$bDestinationCompress)
    {
        # Open the in/out pipes
        pipe $hPipeOut, $hPipeIn;

        # Queue the decompression job with the thread
        $self->{oThreadQueue}->enqueue('decompress:' . fileno($hPipeOut) . ',' . fileno($hOut));

        # Wait for the thread to acknowledge that it has duplicated the file handles
        my $strMessage = $self->{oThreadResult}->dequeue();

        # Close output pipe so that thread has the only copy, reset hOut to hPipeIn
        if ($strMessage eq 'running')
        {
            close($hPipeOut);
            $hOut = $hPipeIn;
        }
        # If any other message is returned then error
        else
        {
            confess "unknown thread message while waiting for running: ${strMessage}";
        }

        $bThreadRunning = true;
    }

    while (1)
    {
        if ($strRemote eq 'in')
        {
            if ($iBlockInTotal == $iBlockSize)
            {
                $strBlockHeader = $self->read_line($hIn);

                if ($strBlockHeader !~ /^block [0-9]+$/)
                {
                    $self->wait_pid();
                    confess "unable to read block header ${strBlockHeader}";
                }

                $iBlockInTotal = 0;
                $iBlockTotal += 1;
            }

            $iBlockSize = trim(substr($strBlockHeader, index($strBlockHeader, ' ') + 1));

            if ($iBlockSize != 0)
            {
                $iBlockIn = sysread($hIn, $strBlock, $iBlockSize - $iBlockInTotal);

                if (!defined($iBlockIn))
                {
                    my $strError = $!;

                    $self->wait_pid();
                    confess "unable to read block #${iBlockTotal}/${iBlockSize} bytes from remote" .
                            (defined($strError) ? ": ${strError}" : '');
                }

                $iBlockInTotal += $iBlockIn;
            }
            else
            {
                $iBlockIn = 0;
            }
        }
        else
        {
            $iBlockIn = sysread($hIn, $strBlock, $iBlockSize);

            if (!defined($iBlockIn))
            {
                $self->wait_pid();
                confess &log(ERROR, 'unable to read');
            }
        }

        if ($strRemote eq 'out')
        {
            $strBlockHeader = "block ${iBlockIn}\n";

            $iBlockOut = syswrite($hOut, $strBlockHeader);

            if (!defined($iBlockOut) || $iBlockOut != length($strBlockHeader))
            {
                $self->wait_pid();
                confess 'unable to write block header';
            }
        }

        if ($iBlockIn > 0)
        {
            $iBlockOut = syswrite($hOut, $strBlock, $iBlockIn);

            if (!defined($iBlockOut) || $iBlockOut != $iBlockIn)
            {
                $self->wait_pid();
                confess "unable to write ${iBlockIn} bytes" . (defined($!) ? ': ' . $! : '');
            }
        }
        else
        {
            last;
        }
    }

    if ($bThreadRunning)
    {
        # Make sure the de/compress pipes are closed
        if ($strRemote eq 'out' && !$bSourceCompressed)
        {
            close($hPipeOut);
        }
        elsif ($strRemote eq 'in' && !$bDestinationCompress)
        {
            close($hPipeIn);
        }

        # Wait for the thread to acknowledge that it has completed
        my $strMessage = $self->{oThreadResult}->dequeue();

        if ($strMessage eq 'complete')
        {
        }
        # If any other message is returned then error
        else
        {
            confess "unknown thread message while waiting for complete: ${strMessage}";
        }
    }
}

####################################################################################################################################
# OUTPUT_READ
#
# Read output from the remote process.
####################################################################################################################################
sub output_read
{
    my $self = shift;
    my $bOutputRequired = shift;
    my $strErrorPrefix = shift;
    my $bSuppressLog = shift;

    my $strLine;
    my $strOutput;
    my $bError = false;
    my $iErrorCode;
    my $strError;

    # Read output lines
    while ($strLine = $self->read_line($self->{hOut}, false))
    {
        if ($strLine =~ /^ERROR.*/)
        {
            $bError = true;

            $iErrorCode = (split(' ', $strLine))[1];

            last;
        }

        if ($strLine =~ /^OK$/)
        {
            last;
        }

        $strOutput .= (defined($strOutput) ? "\n" : '') . substr($strLine, 1);
    }

    # Check if the process has exited abnormally
    $self->wait_pid();

    # Raise any errors
    if ($bError)
    {
        confess &log(ERROR, (defined($strErrorPrefix) ? "${strErrorPrefix}" : '') .
                            (defined($strOutput) ? ": ${strOutput}" : ''), $iErrorCode, $bSuppressLog);
    }

    # If output is required and there is no output, raise exception
    if ($bOutputRequired && !defined($strOutput))
    {
        confess &log(ERROR, (defined($strErrorPrefix) ? "${strErrorPrefix}: " : '') . 'output is not defined');
    }

    # Return output
    return $strOutput;
}

####################################################################################################################################
# OUTPUT_WRITE
#
# Write output for the master process.
####################################################################################################################################
sub output_write
{
    my $self = shift;
    my $strOutput = shift;

    if (defined($strOutput))
    {
        $self->string_write(*STDOUT, "${strOutput}");

        if (!syswrite(*STDOUT, "\n"))
        {
            confess 'unable to write output';
        }
    }

    if (!syswrite(*STDOUT, "OK\n"))
    {
        confess 'unable to write output';
    }
}

####################################################################################################################################
# COMMAND_PARAM_STRING
#
# Output command parameters in the hash as a string (used for debugging).
####################################################################################################################################
sub command_param_string
{
    my $self = shift;
    my $oParamHashRef = shift;

    my $strParamList;

    foreach my $strParam (sort(keys $oParamHashRef))
    {
        $strParamList .= (defined($strParamList) ? ',' : '') . "${strParam}=" .
                         (defined(${$oParamHashRef}{"${strParam}"}) ? ${$oParamHashRef}{"${strParam}"} : '[undef]');
    }

    return $strParamList;
}

####################################################################################################################################
# COMMAND_READ
#
# Read command sent by the master process.
####################################################################################################################################
sub command_read
{
    my $self = shift;
    my $oParamHashRef = shift;

    my $strLine;
    my $strCommand;

    while ($strLine = $self->read_line(*STDIN))
    {
        if (!defined($strCommand))
        {
            if ($strLine =~ /:$/)
            {
                $strCommand = substr($strLine, 0, length($strLine) - 1);
            }
            else
            {
                $strCommand = $strLine;
                last;
            }
        }
        else
        {
            if ($strLine eq 'end')
            {
                last;
            }

            my $iPos = index($strLine, '=');

            if ($iPos == -1)
            {
                confess "param \"${strLine}\" is missing = character";
            }

            my $strParam = substr($strLine, 0, $iPos);
            my $strValue = substr($strLine, $iPos + 1);

            ${$oParamHashRef}{"${strParam}"} = ${strValue};
        }
    }

    return $strCommand;
}

####################################################################################################################################
# COMMAND_WRITE
#
# Send command to remote process.
####################################################################################################################################
sub command_write
{
    my $self = shift;
    my $strCommand = shift;
    my $oParamRef = shift;

    my $strOutput = $strCommand;

    if (defined($oParamRef))
    {
        $strOutput = "${strCommand}:\n";

        foreach my $strParam (sort(keys $oParamRef))
        {
            if ($strParam =~ /=/)
            {
                confess &log(ASSERT, "param \"${strParam}\" cannot contain = character");
            }

            my $strValue = ${$oParamRef}{"${strParam}"};

            if ($strParam =~ /\n\$/)
            {
                confess &log(ASSERT, "param \"${strParam}\" value cannot end with LF");
            }

            if (defined(${strValue}))
            {
                $strOutput .= "${strParam}=${strValue}\n";
            }
        }

        $strOutput .= 'end';
    }

    &log(TRACE, "Remote->command_write:\n" . $strOutput);

    if (!syswrite($self->{hIn}, "${strOutput}\n"))
    {
        confess 'unable to write command';
    }
}

####################################################################################################################################
# COMMAND_EXECUTE
#
# Send command to remote process and wait for output.
####################################################################################################################################
sub command_execute
{
    my $self = shift;
    my $strCommand = shift;
    my $oParamRef = shift;
    my $bOutputRequired = shift;
    my $strErrorPrefix = shift;

    $self->command_write($strCommand, $oParamRef);

    return $self->output_read($bOutputRequired, $strErrorPrefix);
}

no Moose;
__PACKAGE__->meta->make_immutable;
