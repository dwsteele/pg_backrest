/***********************************************************************************************************************************
Crypto Common
***********************************************************************************************************************************/
#ifndef CRYPTO_CRYPTO_H
#define CRYPTO_CRYPTO_H

/***********************************************************************************************************************************
Cipher modes
***********************************************************************************************************************************/
typedef enum
{
    cipherModeEncrypt,
    cipherModeDecrypt,
} CipherMode;

/***********************************************************************************************************************************
Cipher types
***********************************************************************************************************************************/
typedef enum
{
    cipherTypeNone,
    cipherTypeAes256Cbc,
} CipherType;

#include <common/type/string.h>

#define CIPHER_TYPE_NONE                                            "none"
    STRING_DECLARE(CIPHER_TYPE_NONE_STR);
#define CIPHER_TYPE_AES_256_CBC                                     "aes-256-cbc"
    STRING_DECLARE(CIPHER_TYPE_AES_256_CBC_STR);

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
void cryptoInit(void);
bool cryptoIsInit(void);

void cryptoError(bool error, const char *description);
void cryptoErrorCode(unsigned long code, const char *description);

CipherType cipherType(const String *name);
const String *cipherTypeName(CipherType type);

void cryptoRandomBytes(unsigned char *buffer, size_t size);

#endif
