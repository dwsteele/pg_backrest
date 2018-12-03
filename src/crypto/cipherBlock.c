/***********************************************************************************************************************************
Block Cipher
***********************************************************************************************************************************/
#include <string.h>

#include <openssl/evp.h>
#include <openssl/err.h>

#include "common/assert.h"
#include "common/debug.h"
#include "common/io/filter/filter.intern.h"
#include "common/log.h"
#include "common/memContext.h"
#include "crypto/cipherBlock.h"
#include "crypto/crypto.h"

/***********************************************************************************************************************************
Filter type constant
***********************************************************************************************************************************/
#define CIPHER_BLOCK_FILTER_TYPE                                   "cipherBlock"
    STRING_STATIC(CIPHER_BLOCK_FILTER_TYPE_STR,                    CIPHER_BLOCK_FILTER_TYPE);

/***********************************************************************************************************************************
Header constants and sizes
***********************************************************************************************************************************/
// Magic constant for salted encrypt.  Only salted encrypt is done here, but this constant is required for compatibility with the
// openssl command-line tool.
#define CIPHER_BLOCK_MAGIC                                          "Salted__"
#define CIPHER_BLOCK_MAGIC_SIZE                                     (sizeof(CIPHER_BLOCK_MAGIC) - 1)

// Total length of cipher header
#define CIPHER_BLOCK_HEADER_SIZE                                    (CIPHER_BLOCK_MAGIC_SIZE + PKCS5_SALT_LEN)

/***********************************************************************************************************************************
Track state during block encrypt/decrypt
***********************************************************************************************************************************/
struct CipherBlock
{
    MemContext *memContext;                                         // Context to store data
    CipherMode mode;                                                // Mode encrypt/decrypt
    bool saltDone;                                                  // Has the salt been read/generated?
    bool processDone;                                               // Has any data been processed?
    size_t passSize;                                                // Size of passphrase in bytes
    unsigned char *pass;                                            // Passphrase used to generate encryption key
    size_t headerSize;                                              // Size of header read during decrypt
    unsigned char header[CIPHER_BLOCK_HEADER_SIZE];                 // Buffer to hold partial header during decrypt
    const EVP_CIPHER *cipher;                                       // Cipher object
    const EVP_MD *digest;                                           // Message digest object
    EVP_CIPHER_CTX *cipherContext;                                  // Encrypt/decrypt context

    IoFilter *filter;                                               // Filter interface
    Buffer *buffer;                                                 // Internal buffer in case destination buffer isn't large enough
    bool inputSame;                                                 // Is the same input required on next process call?
    bool done;                                                      // Is processing done?
};

/***********************************************************************************************************************************
New block encrypt/decrypt object
***********************************************************************************************************************************/
CipherBlock *
cipherBlockNew(CipherMode mode, CipherType cipherType, const Buffer *pass, const String *digestName)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(ENUM, mode);
        FUNCTION_DEBUG_PARAM(ENUM, cipherType);
        FUNCTION_DEBUG_PARAM(BUFFER, pass);
        FUNCTION_DEBUG_PARAM(STRING, digestName);

        FUNCTION_DEBUG_ASSERT(cipherType == cipherTypeAes256Cbc);
        FUNCTION_DEBUG_ASSERT(pass != NULL);
        FUNCTION_DEBUG_ASSERT(bufSize(pass) > 0);
    FUNCTION_DEBUG_END();

    FUNCTION_DEBUG_RESULT(
        CIPHER_BLOCK, cipherBlockNewC(mode, CIPHER_TYPE_AES_256_CBC, bufPtr(pass), bufSize(pass), strPtr(digestName)));
}

CipherBlock *
cipherBlockNewC(CipherMode mode, const char *cipherName, const unsigned char *pass, size_t passSize, const char *digestName)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(ENUM, mode);
        FUNCTION_DEBUG_PARAM(STRINGZ, cipherName);
        FUNCTION_DEBUG_PARAM(UCHARP, pass);
        FUNCTION_DEBUG_PARAM(SIZE, passSize);
        FUNCTION_DEBUG_PARAM(STRINGZ, digestName);

        FUNCTION_DEBUG_ASSERT(cipherName != NULL);
        FUNCTION_DEBUG_ASSERT(pass != NULL);
        FUNCTION_DEBUG_ASSERT(passSize > 0);
    FUNCTION_DEBUG_END();

    // Only need to init once.
    if (!cryptoIsInit())
        cryptoInit();

    // Lookup cipher by name.  This means the ciphers passed in must exactly match a name expected by OpenSSL.  This is a good
    // thing since the name required by the openssl command-line tool will match what is used by pgBackRest.
    const EVP_CIPHER *cipher = EVP_get_cipherbyname(cipherName);

    if (!cipher)
        THROW_FMT(AssertError, "unable to load cipher '%s'", cipherName);

    // Lookup digest.  If not defined it will be set to sha1.
    const EVP_MD *digest = NULL;

    if (digestName)
        digest = EVP_get_digestbyname(digestName);
    else
        digest = EVP_sha1();

    if (!digest)
        THROW_FMT(AssertError, "unable to load digest '%s'", digestName);

    // Allocate memory to hold process state
    CipherBlock *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("cipherBlock")
    {
        // Allocate state and set context
        this = memNew(sizeof(CipherBlock));
        this->memContext = MEM_CONTEXT_NEW();

        // Set mode, encrypt or decrypt
        this->mode = mode;

        // Set cipher and digest
        this->cipher = cipher;
        this->digest = digest;

        // Store the passphrase
        this->passSize = passSize;
        this->pass = memNewRaw(this->passSize);
        memcpy(this->pass, pass, this->passSize);

        // Create filter interface
        this->filter = ioFilterNewP(
            CIPHER_BLOCK_FILTER_TYPE_STR, this, .done = (IoFilterInterfaceDone)cipherBlockDone,
            .inOut = (IoFilterInterfaceProcessInOut)cipherBlockProcess,
            .inputSame = (IoFilterInterfaceInputSame)cipherBlockInputSame);
    }
    MEM_CONTEXT_NEW_END();

    FUNCTION_DEBUG_RESULT(CIPHER_BLOCK, this);
}

/***********************************************************************************************************************************
Determine how large the destination buffer should be
***********************************************************************************************************************************/
size_t
cipherBlockProcessSizeC(CipherBlock *this, size_t sourceSize)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(CIPHER_BLOCK, this);
        FUNCTION_DEBUG_PARAM(SIZE, sourceSize);

        FUNCTION_DEBUG_ASSERT(this != NULL);
    FUNCTION_DEBUG_END();

    // Destination size is source size plus one extra block
    size_t destinationSize = sourceSize + EVP_MAX_BLOCK_LENGTH;

    // On encrypt the header size must be included before the first block
    if (this->mode == cipherModeEncrypt && !this->saltDone)
        destinationSize += CIPHER_BLOCK_MAGIC_SIZE + PKCS5_SALT_LEN;

    FUNCTION_DEBUG_RESULT(SIZE, destinationSize);
}

/***********************************************************************************************************************************
Encrypt/decrypt data
***********************************************************************************************************************************/
size_t
cipherBlockProcessC(CipherBlock *this, const unsigned char *source, size_t sourceSize, unsigned char *destination)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(CIPHER_BLOCK, this);
        FUNCTION_DEBUG_PARAM(UCHARP, source);
        FUNCTION_DEBUG_PARAM(SIZE, sourceSize);
        FUNCTION_DEBUG_PARAM(UCHARP, destination);

        FUNCTION_DEBUG_ASSERT(this != NULL);
        FUNCTION_DEBUG_ASSERT(source != NULL || sourceSize == 0);
        FUNCTION_DEBUG_ASSERT(destination != NULL);
    FUNCTION_DEBUG_END();

    // Actual destination size
    size_t destinationSize = 0;

    // If the salt has not been generated/read yet
    if (!this->saltDone)
    {
        const unsigned char *salt = NULL;

        // On encrypt the salt is generated
        if (this->mode == cipherModeEncrypt)
        {
            // Add magic to the destination buffer so openssl knows the file is salted
            memcpy(destination, CIPHER_BLOCK_MAGIC, CIPHER_BLOCK_MAGIC_SIZE);
            destination += CIPHER_BLOCK_MAGIC_SIZE;
            destinationSize += CIPHER_BLOCK_MAGIC_SIZE;

            // Add salt to the destination buffer
            cryptoRandomBytes(destination, PKCS5_SALT_LEN);
            salt = destination;
            destination += PKCS5_SALT_LEN;
            destinationSize += PKCS5_SALT_LEN;
        }
        // On decrypt the salt is read from the header
        else if (sourceSize > 0)
        {
            // Check if the entire header has been read
            if (this->headerSize + sourceSize >= CIPHER_BLOCK_HEADER_SIZE)
            {
                // Copy header (or remains of header) from source into the header buffer
                memcpy(this->header + this->headerSize, source, CIPHER_BLOCK_HEADER_SIZE - this->headerSize);
                salt = this->header + CIPHER_BLOCK_MAGIC_SIZE;

                // Advance source and source size by the number of bytes read
                source += CIPHER_BLOCK_HEADER_SIZE - this->headerSize;
                sourceSize -= CIPHER_BLOCK_HEADER_SIZE - this->headerSize;

                // The first bytes of the file to decrypt should be equal to the magic.  If not then this is not an
                // encrypted file, or at least not in a format we recognize.
                if (memcmp(this->header, CIPHER_BLOCK_MAGIC, CIPHER_BLOCK_MAGIC_SIZE) != 0)
                    THROW(CryptoError, "cipher header invalid");
            }
            // Else copy what was provided into the header buffer and return 0
            else
            {
                memcpy(this->header + this->headerSize, source, sourceSize);
                this->headerSize += sourceSize;

                // Indicate that there is nothing left to process
                sourceSize = 0;
            }
        }

        // If salt generation/read is done
        if (salt)
        {
            // Generate key and initialization vector
            unsigned char key[EVP_MAX_KEY_LENGTH];
            unsigned char initVector[EVP_MAX_IV_LENGTH];

            EVP_BytesToKey(
                this->cipher, this->digest, salt, (unsigned char *)this->pass, (int)this->passSize, 1, key, initVector);

            // Create context to track cipher
            cryptoError(!(this->cipherContext = EVP_CIPHER_CTX_new()), "unable to create context");

            // Set free callback to ensure cipher context is freed
            memContextCallback(this->memContext, (MemContextCallback)cipherBlockFree, this);

            // Initialize cipher
            cryptoError(
                !EVP_CipherInit_ex(
                    this->cipherContext, this->cipher, NULL, key, initVector, this->mode == cipherModeEncrypt),
                    "unable to initialize cipher");

            this->saltDone = true;
        }
    }

    // Recheck that source size > 0 as the bytes may have been consumed reading the header
    if (sourceSize > 0)
    {
        // Process the data
        size_t destinationUpdateSize = 0;

        cryptoError(
            !EVP_CipherUpdate(this->cipherContext, destination, (int *)&destinationUpdateSize, source, (int)sourceSize),
            "unable to process cipher");

        destinationSize += destinationUpdateSize;

        // Note that data has been processed so flush is valid
        this->processDone = true;
    }

    // Return actual destination size
    FUNCTION_DEBUG_RESULT(SIZE, destinationSize);
}

/***********************************************************************************************************************************
Flush the remaining data
***********************************************************************************************************************************/
size_t
cipherBlockFlushC(CipherBlock *this, unsigned char *destination)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(CIPHER_BLOCK, this);
        FUNCTION_DEBUG_PARAM(UCHARP, destination);

        FUNCTION_DEBUG_ASSERT(this != NULL);
        FUNCTION_DEBUG_ASSERT(destination != NULL);
    FUNCTION_DEBUG_END();

    // Actual destination size
    size_t destinationSize = 0;

    // If no header was processed then error
    if (!this->saltDone)
        THROW(CryptoError, "cipher header missing");

    // Only flush remaining data if some data was processed
    if (!EVP_CipherFinal(this->cipherContext, destination, (int *)&destinationSize))
        THROW(CryptoError, "unable to flush");

    // Return actual destination size
    FUNCTION_DEBUG_RESULT(SIZE, destinationSize);
}

/***********************************************************************************************************************************
Process function used by C filter
***********************************************************************************************************************************/
void
cipherBlockProcess(CipherBlock *this, const Buffer *source, Buffer *destination)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(CIPHER_BLOCK, this);
        FUNCTION_DEBUG_PARAM(BUFFER, source);
        FUNCTION_DEBUG_PARAM(BUFFER, destination);

        FUNCTION_DEBUG_ASSERT(this != NULL);
        FUNCTION_DEBUG_ASSERT(destination != NULL);
        FUNCTION_DEBUG_ASSERT(bufRemains(destination) > 0);
    FUNCTION_DEBUG_END();

    // Copy already buffered bytes
    if (this->buffer != NULL && bufUsed(this->buffer) > 0)
    {
        if (bufRemains(destination) >= bufUsed(this->buffer))
        {
            bufCat(destination, this->buffer);
            bufUsedZero(this->buffer);

            this->inputSame = false;
        }
        else
        {
            size_t catSize = bufRemains(destination);
            bufCatSub(destination, this->buffer, 0, catSize);

            memmove(bufPtr(this->buffer), bufPtr(this->buffer) + catSize, bufUsed(this->buffer) - catSize);
            bufUsedSet(this->buffer, bufUsed(this->buffer) - catSize);

            this->inputSame = true;
        }
    }
    else
    {
        ASSERT_DEBUG(this->buffer == NULL || bufUsed(this->buffer) == 0);

        // Determine how much space is required in the output buffer
        Buffer *outputActual = destination;

        size_t destinationSize = cipherBlockProcessSizeC(this, source == NULL ? 0 : bufUsed(source));

        if (destinationSize > bufRemains(destination))
        {
            // Allocate the buffer if needed
            MEM_CONTEXT_BEGIN(this->memContext)
            {
                if (this->buffer == NULL)
                {
                    this->buffer = bufNew(destinationSize);
                }
                // Resize buffer if needed
                else
                    bufResize(this->buffer, destinationSize);
            }
            MEM_CONTEXT_END();

            outputActual = this->buffer;
        }

        // Encrypt/decrypt bytes
        size_t destinationSizeActual;

        if (source == NULL)
        {
            // If salt was not generated it means that process() was never called with any data.  It's OK to encrypt a zero byte
            // file but we need to call process to generate the header.
            if (!this->saltDone)
            {
                destinationSizeActual = cipherBlockProcessC(this, NULL, 0, bufRemainsPtr(outputActual));
                bufUsedInc(outputActual, destinationSizeActual);
            }

            destinationSizeActual = cipherBlockFlushC(this, bufRemainsPtr(outputActual));
            this->done = true;
        }
        else
            destinationSizeActual = cipherBlockProcessC(this, bufPtr(source), bufUsed(source), bufRemainsPtr(outputActual));

        bufUsedInc(outputActual, destinationSizeActual);

        // Copy from buffer to destination if needed
        if (this->buffer != NULL && bufUsed(this->buffer) > 0)
            cipherBlockProcess(this, source, destination);
    }

    FUNCTION_DEBUG_RESULT_VOID();
}

/***********************************************************************************************************************************
Is cipher done?
***********************************************************************************************************************************/
bool
cipherBlockDone(const CipherBlock *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(CIPHER_BLOCK, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(BOOL, this->done && !this->inputSame);
}

/***********************************************************************************************************************************
Get filter interface
***********************************************************************************************************************************/
IoFilter *
cipherBlockFilter(const CipherBlock *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(CIPHER_BLOCK, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(IO_FILTER, this->filter);
}

/***********************************************************************************************************************************
Should the same input be provided again?
***********************************************************************************************************************************/
bool
cipherBlockInputSame(const CipherBlock *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(CIPHER_BLOCK, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(BOOL, this->inputSame);
}

/***********************************************************************************************************************************
Render as string for logging
***********************************************************************************************************************************/
String *
cipherBlockToLog(const CipherBlock *this)
{
    return strNewFmt(
        "{inputSame: %s, done: %s}", cvtBoolToConstZ(this->inputSame), cvtBoolToConstZ(this->done));
}

/***********************************************************************************************************************************
Free memory
***********************************************************************************************************************************/
void
cipherBlockFree(CipherBlock *this)
{
    FUNCTION_DEBUG_BEGIN(logLevelTrace);
        FUNCTION_DEBUG_PARAM(CIPHER_BLOCK, this);
    FUNCTION_DEBUG_END();

    if (this != NULL)
    {
        // Free cipher context
        if (this->cipherContext)
            EVP_CIPHER_CTX_cleanup(this->cipherContext);

        // Free mem context
        memContextCallbackClear(this->memContext);
        memContextFree(this->memContext);
    }

    FUNCTION_DEBUG_RESULT_VOID();
}
