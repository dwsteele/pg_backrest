/***********************************************************************************************************************************
GCS Storage
***********************************************************************************************************************************/
#ifndef STORAGE_GCS_STORAGE_H
#define STORAGE_GCS_STORAGE_H

#include "storage/storage.h"

/***********************************************************************************************************************************
Storage type
***********************************************************************************************************************************/
#define STORAGE_GCS_TYPE                                            STRID6("gcs", 0x130c71)

/***********************************************************************************************************************************
Key type
***********************************************************************************************************************************/
typedef enum
{
    storageGcsKeyTypeAuto = STRID5("auto", 0x7d2a10),
    storageGcsKeyTypeService = STRID5("service", 0x1469b48b30),
    storageGcsKeyTypeToken = STRID5("token", 0xe2adf40),
} StorageGcsKeyType;

/***********************************************************************************************************************************
Defaults
***********************************************************************************************************************************/
#define STORAGE_GCS_CHUNKSIZE_DEFAULT                               ((size_t)4 * 1024 * 1024)
#define STORAGE_GCS_METADATA_DEFAULT                                                                                               \
    "metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/token"

/***********************************************************************************************************************************
Constructors
***********************************************************************************************************************************/
Storage *storageGcsNew(
    const String *path, bool write, StoragePathExpressionCallback pathExpressionFunction, const String *bucket,
    StorageGcsKeyType keyType, const String *key, size_t blockSize, const String *endpoint, const String *metadata,
    TimeMSec timeout, bool verifyPeer, const String *caFile, const String *caPath);

#endif
