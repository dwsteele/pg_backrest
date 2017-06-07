####################################################################################################################################
# StorageS3DriverTest.pm - S3 Storage Tests
####################################################################################################################################
package pgBackRestTest::Module::Storage::StorageS3DriverTest;
use parent 'pgBackRestTest::Env::S3EnvTest';

####################################################################################################################################
# Perl includes
####################################################################################################################################
use strict;
use warnings FATAL => qw(all);
use Carp qw(confess);
use English '-no_match_vars';

use pgBackRest::Common::Log;
use pgBackRest::Common::String;
use pgBackRest::Storage::S3::Driver;

use pgBackRestTest::Common::ExecuteTest;
use pgBackRestTest::Common::RunTest;

# !!! CONSOLIDATE FILE TESTS INTO THIS MODULE TO REDUCE DUPLICATION

####################################################################################################################################
# initTest
####################################################################################################################################
sub initTest
{
    my $self = shift;

    executeTest("$self->{strS3Command} rm --recursive s3://pgbackrest-dev");
}

####################################################################################################################################
# run
####################################################################################################################################
sub run
{
    my $self = shift;

    # Initialize the driver
    my $oS3 = $self->initS3();
    my $oStorage = new pgBackRest::Storage::Local('', $oS3);

    # Test variables
    my $strFile = 'file.txt';
    my $strFileContent = 'TESTDATA';

    ################################################################################################################################
    if ($self->begin('exists()'))
    {
        #---------------------------------------------------------------------------------------------------------------------------
        $self->testResult(sub {$oStorage->exists($strFile)}, false, 'root file does not exist');

        #---------------------------------------------------------------------------------------------------------------------------
        storageTest()->put($strFile, $strFileContent);
        executeTest("$self->{strS3Command} cp " . $self->testPath() . "/${strFile} s3://pgbackrest-dev");

        $self->testResult(sub {$oStorage->exists($strFile)}, true, 'root file exists');

        #---------------------------------------------------------------------------------------------------------------------------
        $self->testResult(sub {$oStorage->pathExists('/path/to')}, false, 'sub path does not exist');
        $self->testResult(sub {$oStorage->exists("/path/to/${strFile}")}, false, 'sub file does not exist');

        #---------------------------------------------------------------------------------------------------------------------------
        executeTest("$self->{strS3Command} cp " . $self->testPath() . "/${strFile} s3://pgbackrest-dev/path/to/${strFile}");

        $self->testResult(sub {$oStorage->pathExists('/path/to')}, true, 'sub path exists');
        # $oStorage->pathExists('/path/to');
        $self->testResult(sub {$oStorage->exists("/path/to/${strFile}")}, true, 'sub file exists');
    }

    ################################################################################################################################
    if ($self->begin('manifest()'))
    {
        #---------------------------------------------------------------------------------------------------------------------------
        $self->testResult(sub {$oStorage->manifest('')}, '{. => {type => d}}', 'no files');

        #---------------------------------------------------------------------------------------------------------------------------
        storageTest()->put($strFile, $strFileContent);
        storageTest()->put("${strFile}2", $strFileContent . '2');

        executeTest("$self->{strS3Command} cp " . $self->testPath() . "/${strFile} s3://pgbackrest-dev");
        executeTest("$self->{strS3Command} cp " . $self->testPath() . "/${strFile}2 s3://pgbackrest-dev/path/to/${strFile}2");

        $self->testResult(
            sub {$oStorage->manifest('')},
            '{. => {type => d}, file.txt => {size => 8, type => f}, path => {type => d}, path/to => {type => d},' .
                ' path/to/file.txt2 => {size => 9, type => f}}',
            'root path');
        $self->testResult(
            sub {$oStorage->manifest('/path/to')}, '{. => {type => d}, file.txt2 => {size => 9, type => f}}', 'sub path');
    }

    ################################################################################################################################
    if ($self->begin('list()'))
    {
        #---------------------------------------------------------------------------------------------------------------------------
        $self->testResult(
            sub {$oStorage->list('')}, '[undef]', 'no files');

        #---------------------------------------------------------------------------------------------------------------------------
        storageTest()->put($strFile, $strFileContent);
        storageTest()->put("${strFile}2", $strFileContent . '2');

        executeTest("$self->{strS3Command} cp " . $self->testPath() . "/${strFile} s3://pgbackrest-dev");
        executeTest("$self->{strS3Command} cp " . $self->testPath() . "/${strFile}2 s3://pgbackrest-dev/path/to/${strFile}2");

        $self->testResult(sub {$oStorage->list('')}, '(file.txt, path)', 'root path');
        $self->testResult(sub {$oStorage->list('/path/to')}, 'file.txt2', 'sub path');
    }

    ################################################################################################################################
    if ($self->begin('remove()'))
    {
        #---------------------------------------------------------------------------------------------------------------------------
        $oStorage->put($strFile, $strFileContent);
        $oStorage->put("/path/to/${strFile}2", $strFileContent);
        $oStorage->put("/path/to/${strFile}3", $strFileContent);
        $oStorage->put("/path/to/${strFile}4", $strFileContent);

        $self->testResult(
            sub {$oStorage->manifest('/')},
            '{. => {type => d}, file.txt => {size => 8, type => f}, path => {type => d}, path/to => {type => d},' .
                ' path/to/file.txt2 => {size => 8, type => f}, path/to/file.txt3 => {size => 8, type => f},' .
                ' path/to/file.txt4 => {size => 8, type => f}}',
            'check manifest');

        #---------------------------------------------------------------------------------------------------------------------------
        $self->testResult(sub {$oStorage->remove('/path/to', {bRecurse => true})}, true, 'remove subpath');

        $self->testResult(
            sub {$oStorage->manifest('/')},
            '{. => {type => d}, file.txt => {size => 8, type => f}}', 'check manifest');

        #---------------------------------------------------------------------------------------------------------------------------
        $self->testResult(sub {$oStorage->remove($strFile)}, true, 'remove file');

        $self->testResult(sub {$oStorage->manifest('/')}, '{. => {type => d}}', 'check manifest');
    }

    ################################################################################################################################
    if ($self->begin('info()'))
    {
        #---------------------------------------------------------------------------------------------------------------------------
        storageTest()->put($strFile, $strFileContent);
        storageTest()->put("${strFile}2", $strFileContent . '2');

        executeTest("$self->{strS3Command} cp " . $self->testPath() . "/${strFile} s3://pgbackrest-dev");
        executeTest("$self->{strS3Command} cp " . $self->testPath() . "/${strFile}2 s3://pgbackrest-dev");
        executeTest("$self->{strS3Command} cp " . $self->testPath() . "/${strFile}2 s3://pgbackrest-dev/path/to/${strFile}2");

        $self->testResult(sub {$oStorage->info($strFile)->size()}, 8, 'file size');
        $self->testResult(sub {$oStorage->info("/path/to/${strFile}2")->size()}, 9, 'file 2 size');
    }
}

1;
