/***********************************************************************************************************************************
Local Command
***********************************************************************************************************************************/
#include "build.auto.h"

#include "command/archive/get/protocol.h"
#include "command/archive/push/protocol.h"
#include "command/backup/protocol.h"
#include "command/restore/protocol.h"
#include "common/debug.h"
#include "common/io/handleRead.h"
#include "common/io/handleWrite.h"
#include "common/log.h"
#include "config/config.h"
#include "config/protocol.h"
#include "protocol/helper.h"
#include "protocol/server.h"

/**********************************************************************************************************************************/
void
cmdLocal(int handleRead, int handleWrite)
{
    FUNCTION_LOG_VOID(logLevelDebug);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        String *name = strNewFmt(PROTOCOL_SERVICE_LOCAL "-%u", cfgOptionUInt(cfgOptProcess));
        IoRead *read = ioHandleReadNew(name, handleRead, (TimeMSec)(cfgOptionDbl(cfgOptProtocolTimeout) * 1000));
        ioReadOpen(read);
        IoWrite *write = ioHandleWriteNew(name, handleWrite);
        ioWriteOpen(write);

        ProtocolServer *server = protocolServerNew(name, PROTOCOL_SERVICE_LOCAL_STR, read, write);
        protocolServerHandlerAdd(server, archiveGetProtocol);
        protocolServerHandlerAdd(server, archivePushProtocol);
        protocolServerHandlerAdd(server, backupProtocol);
        protocolServerHandlerAdd(server, restoreProtocol);
        protocolServerProcess(server);
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}
