/***********************************************************************************************************************************
Version Numbers and Names
***********************************************************************************************************************************/
#ifndef VERSION_H
#define VERSION_H

/***********************************************************************************************************************************
Official name of the project
***********************************************************************************************************************************/
#define PROJECT_NAME                                                "pgBackRest"

/***********************************************************************************************************************************
Standard binary name
***********************************************************************************************************************************/
#define PROJECT_BIN                                                 "pgbackrest"

/***********************************************************************************************************************************
Format Number -- defines format for info and manifest files as well as on-disk structure.  If this number changes then the
repository will be invalid unless migration functions are written.
***********************************************************************************************************************************/
#define REPOSITORY_FORMAT                                           5

/***********************************************************************************************************************************
Software version.  Currently this value is maintained in Version.pm and updated by test.pl.
***********************************************************************************************************************************/
#define PROJECT_VERSION                                             "2.23dev"

#endif
