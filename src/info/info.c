/***********************************************************************************************************************************
Info Handler
***********************************************************************************************************************************/
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "common/debug.h"
#include "common/ini.h"
#include "common/log.h"
#include "common/memContext.h"
#include "crypto/hash.h"
#include "info/info.h"
#include "storage/helper.h"
#include "version.h"

/***********************************************************************************************************************************
Internal constants
***********************************************************************************************************************************/
#define INI_COPY_EXT                                                ".copy"
#define INI_SECTION_BACKREST                                        "backrest"
#define INI_KEY_FORMAT                                              "backrest-format"
#define INI_KEY_VERSION                                             "backrest-version"
#define INI_KEY_CHECKSUM                                            "backrest-checksum"

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
struct Info
{
    MemContext *memContext;                                         // Context that contains the info
    String *fileName;                                               // Full path name of the file
    Ini *ini;                                                       // Parsed file contents
};

/***********************************************************************************************************************************
Return a hash of the contents of the info file
***********************************************************************************************************************************/
static CryptoHash *
infoHash(const Ini *ini)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INI, ini);

        FUNCTION_TEST_ASSERT(ini != NULL);
    FUNCTION_TEST_END();

    CryptoHash *result = cryptoHashNew(strNew(HASH_TYPE_SHA1));

    MEM_CONTEXT_TEMP_BEGIN()
    {
        StringList *sectionList = iniSectionList(ini);

        // Initial JSON opening bracket
        cryptoHashProcessC(result, (const unsigned char *)"{", 1);

        // Loop through sections and create hash for checking checksum
        for (unsigned int sectionIdx = 0; sectionIdx < strLstSize(sectionList); sectionIdx++)
        {
            String *section = strLstGet(sectionList, sectionIdx);

            // Add a comma before additional sections
            if (sectionIdx != 0)
                cryptoHashProcessC(result, (const unsigned char *)",", 1);

            // Create the section header
            cryptoHashProcessC(result, (const unsigned char *)"\"", 1);
            cryptoHashProcessStr(result, section);
            cryptoHashProcessC(result, (const unsigned char *)"\":{", 3);

            StringList *keyList = iniSectionKeyList(ini, section);
            unsigned int keyListSize = strLstSize(keyList);

            // Loop through values and build the section
            for (unsigned int keyIdx = 0; keyIdx < keyListSize; keyIdx++)
            {
                String *key = strLstGet(keyList, keyIdx);

                // Skip the backrest checksum in the file
                if ((strEq(section, strNew(INI_SECTION_BACKREST)) && !strEq(key, strNew(INI_KEY_CHECKSUM))) ||
                    !strEq(section, strNew(INI_SECTION_BACKREST)))
                {
                    cryptoHashProcessC(result, (const unsigned char *)"\"", 1);
                    cryptoHashProcessStr(result, key);
                    cryptoHashProcessC(result, (const unsigned char *)"\":", 2);
                    cryptoHashProcessStr(result, varStr(iniGet(ini, section, strLstGet(keyList, keyIdx))));
                    if ((keyListSize > 1) && (keyIdx < keyListSize - 1))
                        cryptoHashProcessC(result, (const unsigned char *)",", 1);
                }
            }

            // Close the key/value list
            cryptoHashProcessC(result, (const unsigned char *)"}", 1);
        }

        // JSON closing bracket
        cryptoHashProcessC(result, (const unsigned char *)"}", 1);
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_TEST_RESULT(CRYPTO_HASH, result);
}

/***********************************************************************************************************************************
Internal function to check if the information is valid or not
***********************************************************************************************************************************/
static bool
infoValidInternal(
        const Info *this,                                           // Info object to validate
        bool ignoreError)                                     // ignore errors found?
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INFO, this);
        FUNCTION_TEST_PARAM(BOOL, ignoreError);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    bool result = true;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Make sure the ini is valid by testing the checksum
        String *infoChecksum = varStr(iniGet(this->ini, strNew(INI_SECTION_BACKREST), strNew(INI_KEY_CHECKSUM)));

        CryptoHash *hash = infoHash(this->ini);

        // ??? Temporary hack until get json parser: add quotes around hash before comparing
        if (!strEq(infoChecksum, strQuoteZ(cryptoHashHex(hash), "\"")))
        {
            // ??? Temporary hack until get json parser: remove quotes around hash before displaying in messsage & check < 3
            String *chksumMsg = strNewFmt("invalid checksum in '%s', expected '%s' but found '%s'",
            strPtr(this->fileName), strPtr(cryptoHashHex(hash)), (strSize(infoChecksum) < 3) ?
                "[undef]" : strPtr(strSubN(infoChecksum, 1, strSize(infoChecksum) - 2)));

            if (!ignoreError)
            {
                THROW(ChecksumError, strPtr(chksumMsg));
            }
            else
            {
                LOG_WARN(strPtr(chksumMsg));
                result = false;
            }
        }

        // Make sure that the format is current, otherwise error
        if (varIntForce(iniGet(this->ini, strNew(INI_SECTION_BACKREST), strNew(INI_KEY_FORMAT))) != PGBACKREST_FORMAT)
        {
            String *fmtMsg = strNewFmt("invalid format in '%s', expected %d but found %d",
                strPtr(this->fileName), PGBACKREST_FORMAT, varIntForce(iniGet(this->ini, strNew(INI_SECTION_BACKREST),
                strNew(INI_KEY_FORMAT))));

            if (!ignoreError)
            {
                THROW(FormatError, strPtr(fmtMsg));
            }
            else
            {
                LOG_WARN(strPtr(fmtMsg));
                result = false;
            }
        }
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_TEST_RESULT(BOOL, result);
}

/***********************************************************************************************************************************
Internal function to load the copy and check validity
***********************************************************************************************************************************/
static bool
loadInternal(
    Info *this,                                                     // Info object to load parsed buffer into
    bool copyFile)                                                  // Is this the copy file?
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(INFO, this);
        FUNCTION_DEBUG_PARAM(BOOL, copyFile);

        FUNCTION_DEBUG_ASSERT(this != NULL);
    FUNCTION_DEBUG_END();

    bool result = false;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        String *fileName = copyFile ? strCat(strDup(this->fileName), INI_COPY_EXT) : this->fileName;

        // ??? Need to replace storageLocal when able
        Buffer *buffer = storageGetNP(storageNewReadP(storageLocal(), fileName, .ignoreMissing = true));

        // If the file exists, parse and validate it
        if (buffer != NULL)
        {
            iniParse(this->ini, strNewBuf(buffer));

            // Do not ignore errors if the copy file is invalid
            if (infoValidInternal(this, (copyFile ? false : true)))
                result = true;
        }
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_DEBUG_RESULT(BOOL, result);
}

/***********************************************************************************************************************************
Load an Info object
// ??? Need loadFile parameter to be able to load from a string.
// ??? Need to handle modified flag, encryption and checksum, initialization, etc.
// ??? The file MUST exist currently, so this is not actually creating the object - rather it is loading it
***********************************************************************************************************************************/
Info *
infoNew(
    const String *fileName)                                         // Full path/filename to load
{
    FUNCTION_DEBUG_BEGIN(logLevelDebug);
        FUNCTION_DEBUG_PARAM(STRING, fileName);

        FUNCTION_DEBUG_ASSERT(fileName != NULL);
    FUNCTION_DEBUG_END();

    Info *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("info")
    {
        // Create object
        this = memNew(sizeof(Info));
        this->memContext = MEM_CONTEXT_NEW();

        this->ini = iniNew();
        this->fileName = strDup(fileName);

        // Attempt to load the main file. If it does not exist or is invalid, try to load the copy.
        if (!loadInternal(this, false))
        {
            if (!loadInternal(this, true))
            {
                THROW_FMT(
                    FileMissingError, "unable to open %s or %s",
                    strPtr(this->fileName), strPtr(strCat(strDup(this->fileName), INI_COPY_EXT)));
            }
        }
    }
    MEM_CONTEXT_NEW_END();

    // Return buffer
    FUNCTION_DEBUG_RESULT(INFO, this);
}

/***********************************************************************************************************************************
Free the info
***********************************************************************************************************************************/
void
infoFree(Info *this)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(INFO, this);
    FUNCTION_DEBUG_END();

    if (this != NULL)
        memContextFree(this->memContext);

    FUNCTION_DEBUG_RESULT_VOID();
}

/***********************************************************************************************************************************
Accessor functions
***********************************************************************************************************************************/
Ini *
infoIni(const Info *this)
{
    FUNCTION_DEBUG_BEGIN(logLevelDebug);
        FUNCTION_DEBUG_PARAM(INFO, this);

        FUNCTION_DEBUG_ASSERT(this != NULL);
    FUNCTION_DEBUG_END();

    FUNCTION_DEBUG_RESULT(INI, this->ini);
}

String *
infoFileName(const Info *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INFO, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(STRING, this->fileName);
}
