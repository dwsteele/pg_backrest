/***********************************************************************************************************************************
Convert JSON to/from KeyValue
***********************************************************************************************************************************/
#ifndef COMMON_TYPE_JSON_H
#define COMMON_TYPE_JSON_H

#include "common/type/keyValue.h"

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
KeyValue *jsonToKv(const String *json);
String *varToJson(const Variant *var, unsigned int indent);
String *kvToJson(const KeyValue *kv, unsigned int indent);

String *kvToJson(const KeyValue *kv, unsigned int indent);
String *varToJson(const Variant *var, unsigned int indent);

#endif
