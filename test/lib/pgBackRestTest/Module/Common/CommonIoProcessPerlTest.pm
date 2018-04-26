####################################################################################################################################
# Tests for Common::Io::Process module
####################################################################################################################################
package pgBackRestTest::Module::Common::CommonIoProcessPerlTest;
use parent 'pgBackRestTest::Common::RunTest';

####################################################################################################################################
# Perl includes
####################################################################################################################################
use strict;
use warnings FATAL => qw(all);
use Carp qw(confess);
use English '-no_match_vars';

use pgBackRest::Common::Exception;
use pgBackRest::Common::Io::Buffered;
use pgBackRest::Common::Io::Process;
use pgBackRest::Common::Log;

use pgBackRestTest::Common::ExecuteTest;
use pgBackRestTest::Common::RunTest;

####################################################################################################################################
# run
####################################################################################################################################
sub run
{
    my $self = shift;

    # Test data
    my $strFile = $self->testPath() . qw{/} . 'file.txt';
    my $strFileContent = 'TESTDATA';

    ################################################################################################################################
    if ($self->begin('new() & processId()'))
    {
        #---------------------------------------------------------------------------------------------------------------------------
        my $oIoProcess = $self->testResult(sub {
            new pgBackRest::Common::Io::Process(
                new pgBackRest::Common::Io::Buffered(
                    new pgBackRest::Common::Io::Handle('test'), 1, 32), "echo '${strFileContent}'")}, '[object]', 'new - echo');
        $self->testResult(sub {defined($oIoProcess->processId())}, true, '   process id defined');
    }

    ################################################################################################################################
    if ($self->begin('close() and error when stderr has data'))
    {
        #---------------------------------------------------------------------------------------------------------------------------
        my $oIoProcess =
            new pgBackRest::Common::Io::Process(
                new pgBackRest::Common::Io::Buffered(
                    new pgBackRest::Common::Io::Handle('test'), 1, 32), "echo '${strFileContent}' 1>&2");

                $self->testException(
                    sub {$oIoProcess->close()}, ERROR_FILE_READ, 'test terminated unexpectedly [000]: TESTDATA');
    }

    ################################################################################################################################
    if ($self->begin('close() & error()'))
    {
        #---------------------------------------------------------------------------------------------------------------------------
        my $oIoProcess =
            new pgBackRest::Common::Io::Process(
                new pgBackRest::Common::Io::Buffered(
                    new pgBackRest::Common::Io::Handle('test'), 1, 32), "echo '${strFileContent}'");
        $oIoProcess->close();
        $self->testException(
            sub {$oIoProcess->error()}, ERROR_ASSERT, 'cannot call error() after process has been closed');
    }
}

1;
