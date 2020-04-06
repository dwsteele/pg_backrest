/***********************************************************************************************************************************
Remote Storage
***********************************************************************************************************************************/
#ifndef STORAGE_REMOTE_STORAGE_H
#define STORAGE_REMOTE_STORAGE_H

#include "protocol/client.h"
#include "storage/storage.intern.h"

/***********************************************************************************************************************************
Storage type
***********************************************************************************************************************************/
#define STORAGE_REMOTE_TYPE                                         "remote"
    STRING_DECLARE(STORAGE_REMOTE_TYPE_STR);

/***********************************************************************************************************************************
Constructors
***********************************************************************************************************************************/
Storage *storageRemoteNew(
    mode_t modeFile, mode_t modePath, bool write, StoragePathExpressionCallback pathExpressionFunction, ProtocolClient *client,
    unsigned int compressLevel);

#endif
