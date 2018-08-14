/***********************************************************************************************************************************
Cryptographic Hashes
***********************************************************************************************************************************/
#ifndef CRYPTO_HASH_H
#define CRYPTO_HASH_H

/***********************************************************************************************************************************
Hash object
***********************************************************************************************************************************/
typedef struct CryptoHash CryptoHash;

#include "common/io/filter/filter.h"
#include "common/type/string.h"

/***********************************************************************************************************************************
Hash types
***********************************************************************************************************************************/
#define HASH_TYPE_MD5                                               "md5"
#define HASH_TYPE_SHA1                                              "sha1"
#define HASH_TYPE_SHA256                                            "sha256"

/***********************************************************************************************************************************
Constructor
***********************************************************************************************************************************/
CryptoHash *cryptoHashNew(const String *type);

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
void cryptoHashProcess(CryptoHash *this, const Buffer *message);
void cryptoHashProcessC(CryptoHash *this, const unsigned char *message, size_t messageSize);
void cryptoHashProcessStr(CryptoHash *this, const String *message);

/***********************************************************************************************************************************
Getters
***********************************************************************************************************************************/
const Buffer *cryptoHash(CryptoHash *this);
String *cryptoHashHex(CryptoHash *this);
IoFilter *cryptoHashFilter(CryptoHash *this);
const Variant *cryptoHashResult(CryptoHash *this);

/***********************************************************************************************************************************
Destructor
***********************************************************************************************************************************/
void cryptoHashFree(CryptoHash *this);

/***********************************************************************************************************************************
Helper functions
***********************************************************************************************************************************/
String *cryptoHashOne(const String *type, Buffer *message);
String *cryptoHashOneC(const String *type, const unsigned char *message, size_t messageSize);
String *cryptoHashOneStr(const String *type, String *message);

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
#define FUNCTION_DEBUG_CRYPTO_HASH_TYPE                                                                                           \
    CryptoHash *
#define FUNCTION_DEBUG_CRYPTO_HASH_FORMAT(value, buffer, bufferSize)                                                              \
    objToLog(value, "CryptoHash", buffer, bufferSize)

#endif
