####################################################################################################################################
# COMMON EXCEPTION AUTO MODULE
# 
# Automatically generated by Build.pm -- do not modify directly.
####################################################################################################################################
package pgBackRest::Common::ExceptionAuto;

use strict;
use warnings FATAL => qw(all);

use Exporter qw(import);
    our @EXPORT = qw();

####################################################################################################################################
# Error Definitions
####################################################################################################################################
use constant ERROR_MINIMUM                                          => 25;
push @EXPORT, qw(ERROR_MINIMUM);
use constant ERROR_MAXIMUM                                          => 125;
push @EXPORT, qw(ERROR_MAXIMUM);

use constant ERROR_ASSERT                                           => 25;
push @EXPORT, qw(ERROR_ASSERT);
use constant ERROR_CHECKSUM                                         => 26;
push @EXPORT, qw(ERROR_CHECKSUM);
use constant ERROR_CONFIG                                           => 27;
push @EXPORT, qw(ERROR_CONFIG);
use constant ERROR_FILE_INVALID                                     => 28;
push @EXPORT, qw(ERROR_FILE_INVALID);
use constant ERROR_FORMAT                                           => 29;
push @EXPORT, qw(ERROR_FORMAT);
use constant ERROR_COMMAND_REQUIRED                                 => 30;
push @EXPORT, qw(ERROR_COMMAND_REQUIRED);
use constant ERROR_OPTION_INVALID                                   => 31;
push @EXPORT, qw(ERROR_OPTION_INVALID);
use constant ERROR_OPTION_INVALID_VALUE                             => 32;
push @EXPORT, qw(ERROR_OPTION_INVALID_VALUE);
use constant ERROR_OPTION_INVALID_RANGE                             => 33;
push @EXPORT, qw(ERROR_OPTION_INVALID_RANGE);
use constant ERROR_OPTION_INVALID_PAIR                              => 34;
push @EXPORT, qw(ERROR_OPTION_INVALID_PAIR);
use constant ERROR_OPTION_DUPLICATE_KEY                             => 35;
push @EXPORT, qw(ERROR_OPTION_DUPLICATE_KEY);
use constant ERROR_OPTION_NEGATE                                    => 36;
push @EXPORT, qw(ERROR_OPTION_NEGATE);
use constant ERROR_OPTION_REQUIRED                                  => 37;
push @EXPORT, qw(ERROR_OPTION_REQUIRED);
use constant ERROR_POSTMASTER_RUNNING                               => 38;
push @EXPORT, qw(ERROR_POSTMASTER_RUNNING);
use constant ERROR_PROTOCOL                                         => 39;
push @EXPORT, qw(ERROR_PROTOCOL);
use constant ERROR_PATH_NOT_EMPTY                                   => 40;
push @EXPORT, qw(ERROR_PATH_NOT_EMPTY);
use constant ERROR_FILE_OPEN                                        => 41;
push @EXPORT, qw(ERROR_FILE_OPEN);
use constant ERROR_FILE_READ                                        => 42;
push @EXPORT, qw(ERROR_FILE_READ);
use constant ERROR_PARAM_REQUIRED                                   => 43;
push @EXPORT, qw(ERROR_PARAM_REQUIRED);
use constant ERROR_ARCHIVE_MISMATCH                                 => 44;
push @EXPORT, qw(ERROR_ARCHIVE_MISMATCH);
use constant ERROR_ARCHIVE_DUPLICATE                                => 45;
push @EXPORT, qw(ERROR_ARCHIVE_DUPLICATE);
use constant ERROR_VERSION_NOT_SUPPORTED                            => 46;
push @EXPORT, qw(ERROR_VERSION_NOT_SUPPORTED);
use constant ERROR_PATH_CREATE                                      => 47;
push @EXPORT, qw(ERROR_PATH_CREATE);
use constant ERROR_COMMAND_INVALID                                  => 48;
push @EXPORT, qw(ERROR_COMMAND_INVALID);
use constant ERROR_HOST_CONNECT                                     => 49;
push @EXPORT, qw(ERROR_HOST_CONNECT);
use constant ERROR_LOCK_ACQUIRE                                     => 50;
push @EXPORT, qw(ERROR_LOCK_ACQUIRE);
use constant ERROR_BACKUP_MISMATCH                                  => 51;
push @EXPORT, qw(ERROR_BACKUP_MISMATCH);
use constant ERROR_FILE_SYNC                                        => 52;
push @EXPORT, qw(ERROR_FILE_SYNC);
use constant ERROR_PATH_OPEN                                        => 53;
push @EXPORT, qw(ERROR_PATH_OPEN);
use constant ERROR_PATH_SYNC                                        => 54;
push @EXPORT, qw(ERROR_PATH_SYNC);
use constant ERROR_FILE_MISSING                                     => 55;
push @EXPORT, qw(ERROR_FILE_MISSING);
use constant ERROR_DB_CONNECT                                       => 56;
push @EXPORT, qw(ERROR_DB_CONNECT);
use constant ERROR_DB_QUERY                                         => 57;
push @EXPORT, qw(ERROR_DB_QUERY);
use constant ERROR_DB_MISMATCH                                      => 58;
push @EXPORT, qw(ERROR_DB_MISMATCH);
use constant ERROR_DB_TIMEOUT                                       => 59;
push @EXPORT, qw(ERROR_DB_TIMEOUT);
use constant ERROR_FILE_REMOVE                                      => 60;
push @EXPORT, qw(ERROR_FILE_REMOVE);
use constant ERROR_PATH_REMOVE                                      => 61;
push @EXPORT, qw(ERROR_PATH_REMOVE);
use constant ERROR_STOP                                             => 62;
push @EXPORT, qw(ERROR_STOP);
use constant ERROR_TERM                                             => 63;
push @EXPORT, qw(ERROR_TERM);
use constant ERROR_FILE_WRITE                                       => 64;
push @EXPORT, qw(ERROR_FILE_WRITE);
use constant ERROR_PROTOCOL_TIMEOUT                                 => 66;
push @EXPORT, qw(ERROR_PROTOCOL_TIMEOUT);
use constant ERROR_FEATURE_NOT_SUPPORTED                            => 67;
push @EXPORT, qw(ERROR_FEATURE_NOT_SUPPORTED);
use constant ERROR_ARCHIVE_COMMAND_INVALID                          => 68;
push @EXPORT, qw(ERROR_ARCHIVE_COMMAND_INVALID);
use constant ERROR_LINK_EXPECTED                                    => 69;
push @EXPORT, qw(ERROR_LINK_EXPECTED);
use constant ERROR_LINK_DESTINATION                                 => 70;
push @EXPORT, qw(ERROR_LINK_DESTINATION);
use constant ERROR_HOST_INVALID                                     => 72;
push @EXPORT, qw(ERROR_HOST_INVALID);
use constant ERROR_PATH_MISSING                                     => 73;
push @EXPORT, qw(ERROR_PATH_MISSING);
use constant ERROR_FILE_MOVE                                        => 74;
push @EXPORT, qw(ERROR_FILE_MOVE);
use constant ERROR_BACKUP_SET_INVALID                               => 75;
push @EXPORT, qw(ERROR_BACKUP_SET_INVALID);
use constant ERROR_TABLESPACE_MAP                                   => 76;
push @EXPORT, qw(ERROR_TABLESPACE_MAP);
use constant ERROR_PATH_TYPE                                        => 77;
push @EXPORT, qw(ERROR_PATH_TYPE);
use constant ERROR_LINK_MAP                                         => 78;
push @EXPORT, qw(ERROR_LINK_MAP);
use constant ERROR_FILE_CLOSE                                       => 79;
push @EXPORT, qw(ERROR_FILE_CLOSE);
use constant ERROR_DB_MISSING                                       => 80;
push @EXPORT, qw(ERROR_DB_MISSING);
use constant ERROR_DB_INVALID                                       => 81;
push @EXPORT, qw(ERROR_DB_INVALID);
use constant ERROR_ARCHIVE_TIMEOUT                                  => 82;
push @EXPORT, qw(ERROR_ARCHIVE_TIMEOUT);
use constant ERROR_FILE_MODE                                        => 83;
push @EXPORT, qw(ERROR_FILE_MODE);
use constant ERROR_OPTION_MULTIPLE_VALUE                            => 84;
push @EXPORT, qw(ERROR_OPTION_MULTIPLE_VALUE);
use constant ERROR_PROTOCOL_OUTPUT_REQUIRED                         => 85;
push @EXPORT, qw(ERROR_PROTOCOL_OUTPUT_REQUIRED);
use constant ERROR_LINK_OPEN                                        => 86;
push @EXPORT, qw(ERROR_LINK_OPEN);
use constant ERROR_ARCHIVE_DISABLED                                 => 87;
push @EXPORT, qw(ERROR_ARCHIVE_DISABLED);
use constant ERROR_FILE_OWNER                                       => 88;
push @EXPORT, qw(ERROR_FILE_OWNER);
use constant ERROR_USER_MISSING                                     => 89;
push @EXPORT, qw(ERROR_USER_MISSING);
use constant ERROR_OPTION_COMMAND                                   => 90;
push @EXPORT, qw(ERROR_OPTION_COMMAND);
use constant ERROR_GROUP_MISSING                                    => 91;
push @EXPORT, qw(ERROR_GROUP_MISSING);
use constant ERROR_PATH_EXISTS                                      => 92;
push @EXPORT, qw(ERROR_PATH_EXISTS);
use constant ERROR_FILE_EXISTS                                      => 93;
push @EXPORT, qw(ERROR_FILE_EXISTS);
use constant ERROR_MEMORY                                           => 94;
push @EXPORT, qw(ERROR_MEMORY);
use constant ERROR_CRYPTO                                           => 95;
push @EXPORT, qw(ERROR_CRYPTO);
use constant ERROR_PARAM_INVALID                                    => 96;
push @EXPORT, qw(ERROR_PARAM_INVALID);
use constant ERROR_PATH_CLOSE                                       => 97;
push @EXPORT, qw(ERROR_PATH_CLOSE);
use constant ERROR_FILE_INFO                                        => 98;
push @EXPORT, qw(ERROR_FILE_INFO);
use constant ERROR_JSON_FORMAT                                      => 99;
push @EXPORT, qw(ERROR_JSON_FORMAT);
use constant ERROR_KERNEL                                           => 100;
push @EXPORT, qw(ERROR_KERNEL);
use constant ERROR_SERVICE                                          => 101;
push @EXPORT, qw(ERROR_SERVICE);
use constant ERROR_EXECUTE                                          => 102;
push @EXPORT, qw(ERROR_EXECUTE);
use constant ERROR_RUNTIME                                          => 122;
push @EXPORT, qw(ERROR_RUNTIME);
use constant ERROR_INVALID                                          => 123;
push @EXPORT, qw(ERROR_INVALID);
use constant ERROR_UNHANDLED                                        => 124;
push @EXPORT, qw(ERROR_UNHANDLED);
use constant ERROR_UNKNOWN                                          => 125;
push @EXPORT, qw(ERROR_UNKNOWN);

1;
