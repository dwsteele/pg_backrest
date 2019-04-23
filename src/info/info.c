/***********************************************************************************************************************************
Info Handler
***********************************************************************************************************************************/
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "common/crypto/cipherBlock.h"
#include "common/crypto/hash.h"
#include "common/debug.h"
#include "common/ini.h"
#include "common/log.h"
#include "common/memContext.h"
#include "common/type/json.h"
#include "info/info.h"
#include "storage/helper.h"
#include "version.h"

/***********************************************************************************************************************************
Internal constants
***********************************************************************************************************************************/
#define INI_COPY_EXT                                                ".copy"

STRING_STATIC(INFO_SECTION_BACKREST_STR,                            "backrest");
STRING_STATIC(INFO_SECTION_CIPHER_STR,                              "cipher");

STRING_STATIC(INFO_KEY_CIPHER_PASS_STR,                             "cipher-pass");
STRING_STATIC(INFO_KEY_CHECKSUM_STR,                                "backrest-checksum");
STRING_EXTERN(INFO_KEY_FORMAT_STR,                                  INFO_KEY_FORMAT);
STRING_EXTERN(INFO_KEY_VERSION_STR,                                 INFO_KEY_VERSION);

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
struct Info
{
    MemContext *memContext;                                         // Mem context
    String *fileName;                                               // Full path name of the file
    Ini *ini;                                                       // Parsed file contents
    const String *cipherPass;                                       // Cipher passphrase if set
};

/***********************************************************************************************************************************
Return a hash of the contents of the info file
***********************************************************************************************************************************/
static CryptoHash *
infoHash(const Ini *ini)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INI, ini);
    FUNCTION_TEST_END();

    ASSERT(ini != NULL);

    CryptoHash *result = cryptoHashNew(HASH_TYPE_SHA1_STR);

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
                if ((strEq(section, INFO_SECTION_BACKREST_STR) && !strEq(key, INFO_KEY_CHECKSUM_STR)) ||
                    !strEq(section, INFO_SECTION_BACKREST_STR))
                {
                    cryptoHashProcessC(result, (const unsigned char *)"\"", 1);
                    cryptoHashProcessStr(result, key);
                    cryptoHashProcessC(result, (const unsigned char *)"\":", 2);
                    cryptoHashProcessStr(result, iniGet(ini, section, strLstGet(keyList, keyIdx)));
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

    FUNCTION_TEST_RETURN(result);
}

/***********************************************************************************************************************************
Load and validate the info file (or copy)
***********************************************************************************************************************************/
static bool
infoLoad(Info *this, const Storage *storage, bool copyFile, CipherType cipherType, const String *cipherPass)
{
    FUNCTION_LOG_BEGIN(logLevelTrace)
        FUNCTION_LOG_PARAM(INFO, this);
        FUNCTION_LOG_PARAM(STORAGE, storage);
        FUNCTION_LOG_PARAM(BOOL, copyFile);                       // Is this the copy file?
        FUNCTION_LOG_PARAM(ENUM, cipherType);
        FUNCTION_TEST_PARAM(STRING, cipherPass);
    FUNCTION_LOG_END();

    ASSERT(this != NULL);

    bool result = false;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        String *fileName = copyFile ? strCat(strDup(this->fileName), INI_COPY_EXT) : this->fileName;

        // Attempt to load the file
        StorageFileRead *infoRead = storageNewReadNP(storage, fileName);

        if (cipherType != cipherTypeNone)
        {
            ioReadFilterGroupSet(
                storageFileReadIo(infoRead),
                ioFilterGroupAdd(
                    ioFilterGroupNew(), cipherBlockFilter(cipherBlockNew(cipherModeDecrypt, cipherType, BUFSTR(cipherPass),
                    NULL))));
        }

        // Load and parse the info file
        Buffer *buffer = NULL;

        TRY_BEGIN()
        {
            buffer = storageGetNP(infoRead);
        }
        CATCH(CryptoError)
        {
            THROW_FMT(
                CryptoError, "'%s' %s\nHINT: Is or was the repo encrypted?", strPtr(storagePathNP(storage, fileName)),
                errorMessage());
        }
        TRY_END();

        iniParse(this->ini, strNewBuf(buffer));

        // Make sure the ini is valid by testing the checksum
        const String *infoChecksumJson = iniGet(this->ini, INFO_SECTION_BACKREST_STR, INFO_KEY_CHECKSUM_STR);
        const String *checksum = bufHex(cryptoHash(infoHash(this->ini)));

        if (strSize(infoChecksumJson) == 0)
        {
            THROW_FMT(
                ChecksumError, "invalid checksum in '%s', expected '%s' but no checksum found",
                strPtr(storagePathNP(storage, fileName)), strPtr(checksum));
        }
        else
        {
            const String *infoChecksum = jsonToStr(infoChecksumJson);

            if (!strEq(infoChecksum, checksum))
            {
                THROW_FMT(
                    ChecksumError, "invalid checksum in '%s', expected '%s' but found '%s'",
                    strPtr(storagePathNP(storage, fileName)), strPtr(checksum), strPtr(infoChecksum));
            }
        }

        // Make sure that the format is current, otherwise error
        if (jsonToUInt(iniGet(this->ini, INFO_SECTION_BACKREST_STR, INFO_KEY_FORMAT_STR)) != REPOSITORY_FORMAT)
        {
            THROW_FMT(
                FormatError, "invalid format in '%s', expected %d but found %d", strPtr(fileName), REPOSITORY_FORMAT,
                varIntForce(VARSTR(iniGet(this->ini, INFO_SECTION_BACKREST_STR, INFO_KEY_FORMAT_STR))));
        }
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(BOOL, result);
}

/***********************************************************************************************************************************
Load an Info object
// ??? Need loadFile parameter to be able to load from a string.
// ??? Need to handle modified flag, encryption and checksum, initialization, etc.
// ??? The file MUST exist currently, so this is not actually creating the object - rather it is loading it
***********************************************************************************************************************************/
Info *
infoNew(const Storage *storage, const String *fileName, CipherType cipherType, const String *cipherPass)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(STORAGE, storage);
        FUNCTION_LOG_PARAM(STRING, fileName);                     // Full path/filename to load
        FUNCTION_LOG_PARAM(ENUM, cipherType);
        FUNCTION_TEST_PARAM(STRING, cipherPass);
    FUNCTION_LOG_END();

    ASSERT(storage != NULL);
    ASSERT(fileName != NULL);
    ASSERT(cipherType == cipherTypeNone || cipherPass != NULL);

    Info *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("Info")
    {
        // Create object
        this = memNew(sizeof(Info));
        this->memContext = MEM_CONTEXT_NEW();

        this->ini = iniNew();
        this->fileName = strDup(fileName);

        // Attempt to load the primary file
        TRY_BEGIN()
        {
            infoLoad(this, storage, false, cipherType, cipherPass);
        }
        CATCH_ANY()
        {
            // On error store the error and try to load the copy
            String *primaryError = strNewFmt("%s: %s", errorTypeName(errorType()), errorMessage());
            bool primaryMissing = errorType() == &FileMissingError;
            const ErrorType *primaryErrorType = errorType();

            TRY_BEGIN()
            {
                infoLoad(this, storage, true, cipherType, cipherPass);
            }
            CATCH_ANY()
            {
                // If both copies of the file have the same error then throw that error,
                // else if one file is missing but the other is in error and it is not missing, throw that error
                // else throw an open error
                THROWP_FMT(
                    errorType() == primaryErrorType ? errorType() :
                        (errorType() == &FileMissingError ? primaryErrorType :
                        (primaryMissing ? errorType() : &FileOpenError)),
                    "unable to load info file '%s' or '%s" INI_COPY_EXT "':\n%s\n%s: %s",
                    strPtr(storagePathNP(storage, this->fileName)), strPtr(storagePathNP(storage, this->fileName)),
                    strPtr(primaryError), errorTypeName(errorType()), errorMessage());
            }
            TRY_END();
        }
        TRY_END();

        // Load the cipher passphrase if it exists
        const String *cipherPass = iniGetDefault(this->ini, INFO_SECTION_CIPHER_STR, INFO_KEY_CIPHER_PASS_STR, NULL);

        if (cipherPass != NULL)
            this->cipherPass = jsonToStr(cipherPass);
    }
    MEM_CONTEXT_NEW_END();

    FUNCTION_LOG_RETURN(INFO, this);
}

/***********************************************************************************************************************************
Accessor functions
***********************************************************************************************************************************/
const String *
infoCipherPass(const Info *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INFO, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->cipherPass);
}

Ini *
infoIni(const Info *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INFO, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->ini);
}

String *
infoFileName(const Info *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INFO, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->fileName);
}

/***********************************************************************************************************************************
Free the object
***********************************************************************************************************************************/
void
infoFree(Info *this)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(INFO, this);
    FUNCTION_LOG_END();

    if (this != NULL)
        memContextFree(this->memContext);

    FUNCTION_LOG_RETURN_VOID();
}
