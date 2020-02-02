/***********************************************************************************************************************************
Storage Test Harness
***********************************************************************************************************************************/
#include <inttypes.h>

#include "common/debug.h"
#include "common/compress/gzip/decompress.h"
#include "common/user.h"
#include "storage/storage.h"

#include "common/harnessStorage.h"

/**********************************************************************************************************************************/
void
hrnStorageInfoListCallback(void *callbackData, const StorageInfo *info)
{
    HarnessStorageInfoListCallbackData *data = callbackData;

    if (data->rootPathOmit && info->type == storageTypePath && strEq(info->name, DOT_STR))
        return;

    strCatFmt(data->content, "%s {", strPtr(info->name));

    switch (info->type)
    {
        case storageTypeFile:
        {
            strCat(data->content, "file");

            if (!data->sizeOmit)
            {
                uint64_t size = info->size;

                // If the file is compressed then decompress to get the real size
                if (data->fileCompressed)
                {
                    ASSERT(data->storage != NULL);

                    StorageRead *read = storageNewReadP(
                        data->storage,
                        data->path != NULL ? strNewFmt("%s/%s", strPtr(data->path), strPtr(info->name)) : info->name);
                    ioFilterGroupAdd(ioReadFilterGroup(storageReadIo(read)), gzipDecompressNew(false));
                    size = bufUsed(storageGetP(read));
                }

                strCatFmt(data->content, ", s=%" PRIu64, size);
            }

            break;
        }

        case storageTypeLink:
        {
            strCatFmt(data->content, "link, d=%s", strPtr(info->linkDestination));
            break;
        }

        case storageTypePath:
        {
            strCat(data->content, "path");
            break;
        }

        case storageTypeSpecial:
        {
            strCat(data->content, "special");
            break;
        }
    }

    if (info->type != storageTypeSpecial)
    {
        if (info->type != storageTypeLink)
        {
            if (!data->modeOmit || (info->type == storageTypePath && data->modePath != info->mode) ||
                (info->type == storageTypeFile && data->modeFile != info->mode))
            {
                strCatFmt(data->content, ", m=%04o", info->mode);
            }
        }

        if (info->type == storageTypeFile)
        {
            if (!data->timestampOmit)
                strCatFmt(data->content, ", t=%" PRIu64, (uint64_t)info->timeModified);
        }

        if (!data->userOmit || userId() != info->userId)
        {
            if (info->user != NULL)
            {
                strCatFmt(data->content, ", u=%s", strPtr(info->user));
            }
            else
            {
                strCatFmt(data->content, ", u=%d", (int)info->userId);
            }
        }

        if (!data->groupOmit || groupId() != info->groupId)
        {
            if (info->group != NULL)
            {
                strCatFmt(data->content, ", g=%s", strPtr(info->group));
            }
            else
            {
                strCatFmt(data->content, ", g=%d", (int)info->groupId);
            }
        }
    }

    strCat(data->content, "}\n");
}
