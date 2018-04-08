/***********************************************************************************************************************************
Variant Data Type
***********************************************************************************************************************************/
#ifndef COMMON_TYPE_VARIANT_H
#define COMMON_TYPE_VARIANT_H

#include <stdint.h>

/***********************************************************************************************************************************
Variant object
***********************************************************************************************************************************/
typedef struct Variant Variant;

/***********************************************************************************************************************************
Variant type
***********************************************************************************************************************************/
typedef enum
{
    varTypeBool,
    varTypeDouble,
    varTypeInt,
    varTypeInt64,
    varTypeKeyValue,
    varTypeString,
    varTypeVariantList,
} VariantType;

#include "common/type/keyValue.h"
#include "common/type/string.h"
#include "common/type/variantList.h"

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
Variant *varNewBool(bool data);
bool varBool(const Variant *this);
bool varBoolForce(const Variant *this);

Variant *varNewDbl(double data);
double varDbl(const Variant *this);
double varDblForce(const Variant *this);

Variant *varNewInt(int data);
int varInt(const Variant *this);
int varIntForce(const Variant *this);

Variant *varNewInt64(int64_t data);
int64_t varInt64(const Variant *this);
int64_t varInt64Force(const Variant *this);

Variant *varNewKv();
KeyValue *varKv(const Variant *this);

Variant *varNewStr(const String *data);
Variant *varNewStrZ(const char *data);
String *varStr(const Variant *this);
String *varStrForce(const Variant *this);

Variant *varNewVarLst(const VariantList *data);
Variant *varNewVarLstEmpty();
VariantList *varVarLst(const Variant *this);

Variant *varDup(const Variant *this);
bool varEq(const Variant *this1, const Variant *this2);
VariantType varType(const Variant *this);

void varFree(Variant *this);

#endif
