/***********************************************************************************************************************************
Ini Handler
***********************************************************************************************************************************/
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "common/debug.h"
#include "common/memContext.h"
#include "common/ini.h"
#include "common/type/keyValue.h"

/***********************************************************************************************************************************
Contains information about the ini
***********************************************************************************************************************************/
struct Ini
{
    MemContext *memContext;                                         // Context that contains the ini
    KeyValue *store;                                                // Key value store that contains the ini data
};

/***********************************************************************************************************************************
Create a new Ini object
***********************************************************************************************************************************/
Ini *
iniNew(void)
{
    Ini *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("ini")
    {
        // Create object
        this = memNew(sizeof(Ini));
        this->memContext = MEM_CONTEXT_NEW();

        // Allocate key value store
        this->store = kvNew();
    }
    MEM_CONTEXT_NEW_END();

    // Return buffer
    return this;
}

/***********************************************************************************************************************************
Internal function to get an ini value
***********************************************************************************************************************************/
static const Variant *
iniGetInternal(const Ini *this, const String *section, const String *key, bool required)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INI, this);
        FUNCTION_TEST_PARAM(STRING, section);
        FUNCTION_TEST_PARAM(STRING, key);
        FUNCTION_TEST_PARAM(BOOL, required);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(section != NULL);
    ASSERT(key != NULL);

    const Variant *result = NULL;

    // Get the section
    KeyValue *sectionKv = varKv(kvGet(this->store, VARSTR(section)));

    // Section must exist to get the value
    if (sectionKv != NULL)
        result = kvGet(sectionKv, VARSTR(key));

    // If value is null and required then error
    if (result == NULL && required)
        THROW_FMT(FormatError, "section '%s', key '%s' does not exist", strPtr(section), strPtr(key));

    FUNCTION_TEST_RETURN(result);
}

/***********************************************************************************************************************************
Get an ini value -- error if it does not exist
***********************************************************************************************************************************/
const String *
iniGet(const Ini *this, const String *section, const String *key)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INI, this);
        FUNCTION_TEST_PARAM(STRING, section);
        FUNCTION_TEST_PARAM(STRING, key);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(section != NULL);
    ASSERT(key != NULL);

    // Get the value
    const Variant *result = iniGetInternal(this, section, key, true);

    FUNCTION_TEST_RETURN(varStr(result));
}

/***********************************************************************************************************************************
Get an ini value -- if it does not exist then return specified default
***********************************************************************************************************************************/
const String *
iniGetDefault(const Ini *this, const String *section, const String *key, const String *defaultValue)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INI, this);
        FUNCTION_TEST_PARAM(STRING, section);
        FUNCTION_TEST_PARAM(STRING, key);
        FUNCTION_TEST_PARAM(STRING, defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(section != NULL);
    ASSERT(key != NULL);

    // Get the value
    const Variant *result = iniGetInternal(this, section, key, false);

    FUNCTION_TEST_RETURN(result == NULL ? defaultValue : varStr(result));
}

/***********************************************************************************************************************************
Internal function to get an ini value list
***********************************************************************************************************************************/
StringList *
iniGetList(const Ini *this, const String *section, const String *key)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INI, this);
        FUNCTION_TEST_PARAM(STRING, section);
        FUNCTION_TEST_PARAM(STRING, key);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(section != NULL);
    ASSERT(key != NULL);

    // Get the value
    const Variant *result = iniGetInternal(this, section, key, false);

    FUNCTION_TEST_RETURN(result == NULL ? false : strLstNewVarLst(varVarLst(result)));
}

/***********************************************************************************************************************************
Internal function to get an ini value list
***********************************************************************************************************************************/
bool
iniSectionKeyIsList(const Ini *this, const String *section, const String *key)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INI, this);
        FUNCTION_TEST_PARAM(STRING, section);
        FUNCTION_TEST_PARAM(STRING, key);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(section != NULL);
    ASSERT(key != NULL);

    // Get the value
    const Variant *result = iniGetInternal(this, section, key, true);

    FUNCTION_TEST_RETURN(varType(result) == varTypeVariantList);
}

/***********************************************************************************************************************************
Get a list of keys for a section
***********************************************************************************************************************************/
StringList *
iniSectionKeyList(const Ini *this, const String *section)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INI, this);
        FUNCTION_TEST_PARAM(STRING, section);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(section != NULL);

    StringList *result = NULL;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Get the section
        KeyValue *sectionKv = varKv(kvGet(this->store, VARSTR(section)));

        // Return key list if the section exists
        if (sectionKv != NULL)
            result = strLstNewVarLst(kvKeyList(sectionKv));
        // Otherwise return an empty list
        else
            result = strLstNew();

        strLstMove(result, MEM_CONTEXT_OLD());
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_TEST_RETURN(result);
}

/***********************************************************************************************************************************
Get a list of sections
***********************************************************************************************************************************/
StringList *
iniSectionList(const Ini *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INI, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    StringList *result = NULL;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Get the sections from the keyList
        result = strLstNewVarLst(kvKeyList(this->store));

        strLstMove(result, MEM_CONTEXT_OLD());
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_TEST_RETURN(result);
}

/***********************************************************************************************************************************
Parse ini from a string
***********************************************************************************************************************************/
void
iniParse(Ini *this, const String *content)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INI, this);
        FUNCTION_TEST_PARAM(STRING, content);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    MEM_CONTEXT_BEGIN(this->memContext)
    {
        kvFree(this->store);
        this->store = kvNew();

        if (content != NULL)
        {
            MEM_CONTEXT_TEMP_BEGIN()
            {
                // Track the current section
                String *section = NULL;

                // Split the content into lines and loop
                StringList *lines = strLstNewSplitZ(content, "\n");

                for (unsigned int lineIdx = 0; lineIdx < strLstSize(lines); lineIdx++)
                {
                    // Get next line
                    const String *line = strTrim(strLstGet(lines, lineIdx));
                    const char *linePtr = strPtr(line);

                    // Only interested in lines that are not blank or comments
                    if (strSize(line) > 0 && linePtr[0] != '#')
                    {
                        // Looks like this line is a section
                        if (linePtr[0] == '[')
                        {
                            // Make sure the section ends with ]
                            if (linePtr[strSize(line) - 1] != ']')
                                THROW_FMT(FormatError, "ini section should end with ] at line %u: %s", lineIdx + 1, linePtr);

                            // Assign section
                            section = strNewN(linePtr + 1, strSize(line) - 2);
                        }
                        // Else it should be a key/value
                        else
                        {
                            if (section == NULL)
                                THROW_FMT(FormatError, "key/value found outside of section at line %u: %s", lineIdx + 1, linePtr);

                            // Find the =
                            const char *lineEqual = strstr(linePtr, "=");

                            if (lineEqual == NULL)
                                THROW_FMT(FormatError, "missing '=' in key/value at line %u: %s", lineIdx + 1, linePtr);

                            // Extract the key
                            String *key = strTrim(strNewN(linePtr, (size_t)(lineEqual - linePtr)));

                            if (strSize(key) == 0)
                                THROW_FMT(FormatError, "key is zero-length at line %u: %s", lineIdx++, linePtr);

                            // Store the section/key/value
                            iniSet(this, section, key, strTrim(strNew(lineEqual + 1)));
                        }
                    }
                }
            }
            MEM_CONTEXT_TEMP_END();
        }
    }
    MEM_CONTEXT_END()

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Set an ini value
***********************************************************************************************************************************/
void
iniSet(Ini *this, const String *section, const String *key, const String *value)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INI, this);
        FUNCTION_TEST_PARAM(STRING, section);
        FUNCTION_TEST_PARAM(STRING, key);
        FUNCTION_TEST_PARAM(STRING, value);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(section != NULL);
    ASSERT(key != NULL);
    ASSERT(value != NULL);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        const Variant *sectionKey = VARSTR(section);
        KeyValue *sectionKv = varKv(kvGet(this->store, sectionKey));

        if (sectionKv == NULL)
            sectionKv = kvPutKv(this->store, sectionKey);

        kvAdd(sectionKv, VARSTR(key), VARSTR(value));
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Save the ini file
***********************************************************************************************************************************/
void
iniSave(Ini *this, IoWrite *write)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INI, this);
        FUNCTION_TEST_PARAM(IO_WRITE, write);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(write != NULL);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        ioWriteOpen(write);

        StringList *sectionList = strLstSort(iniSectionList(this), sortOrderAsc);

        for (unsigned int sectionIdx = 0; sectionIdx < strLstSize(sectionList); sectionIdx++)
        {
            const String *section = strLstGet(sectionList, sectionIdx);

            if (sectionIdx != 0)
                ioWrite(write, LF_BUF);

            ioWrite(write, BRACKETL_BUF);
            ioWriteStr(write, section);
            ioWriteLine(write, BRACKETR_BUF);

            StringList *keyList = strLstSort(iniSectionKeyList(this, section), sortOrderAsc);

            for (unsigned int keyIdx = 0; keyIdx < strLstSize(keyList); keyIdx++)
            {
                const String *key = strLstGet(keyList, keyIdx);

                ioWriteStr(write, key);
                ioWrite(write, EQ_BUF);
                ioWriteStrLine(write, iniGet(this, section, key));
            }
        }

        ioWriteClose(write);
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Move to a new mem context
***********************************************************************************************************************************/
Ini *
iniMove(Ini *this, MemContext *parentNew)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INI, this);
        FUNCTION_TEST_PARAM(MEM_CONTEXT, parentNew);
    FUNCTION_TEST_END();

    ASSERT(parentNew != NULL);

    if (this != NULL)
        memContextMove(this->memContext, parentNew);

    FUNCTION_TEST_RETURN(this);
}

/***********************************************************************************************************************************
Free the ini
***********************************************************************************************************************************/
void
iniFree(Ini *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INI, this);
    FUNCTION_TEST_END();

    if (this != NULL)
        memContextFree(this->memContext);

    FUNCTION_TEST_RETURN_VOID();
}
