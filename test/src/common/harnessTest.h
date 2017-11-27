/***********************************************************************************************************************************
C Test Harness
***********************************************************************************************************************************/
#include "common/error.h"
#include "common/type.h"

// Bogus values
#define BOGUS_STR                                                   "BOGUS"

// Functions
void testAdd(int run, bool selected);
bool testBegin(const char *name);
void testComplete();

/***********************************************************************************************************************************
Maximum size of a formatted result in the TEST_RESULT macro.  Strings don't count as they are output directly, so this only applies
to the formatting of bools, ints, floats, etc.  This should be plenty of room for any of those types.
***********************************************************************************************************************************/
#define TEST_RESULT_FORMAT_SIZE                                     128

/***********************************************************************************************************************************
Test that an expected error is actually thrown and error when it isn't
***********************************************************************************************************************************/
#define TEST_ERROR(statement, errorTypeExpected, errorMessageExpected)                                                             \
{                                                                                                                                  \
    bool TEST_ERROR_catch = false;                                                                                                 \
                                                                                                                                   \
    printf("    l%04d - expect error: %s\n", __LINE__, errorMessageExpected);                                                      \
    fflush(stdout);                                                                                                                \
                                                                                                                                   \
    TRY_BEGIN()                                                                                                                    \
    {                                                                                                                              \
        statement;                                                                                                                 \
    }                                                                                                                              \
    CATCH_ANY()                                                                                                                    \
    {                                                                                                                              \
        TEST_ERROR_catch = true;                                                                                                   \
                                                                                                                                   \
        if (strcmp(errorMessage(), errorMessageExpected) != 0 || errorType() != &errorTypeExpected)                                \
            THROW(                                                                                                                 \
                AssertError, "expected error %s, '%s' but got %s, '%s'", errorTypeName(&errorTypeExpected), errorMessageExpected,  \
                errorName(), errorMessage());                                                                                      \
    }                                                                                                                              \
    TRY_END();                                                                                                                     \
                                                                                                                                   \
    if (!TEST_ERROR_catch)                                                                                                         \
        THROW(                                                                                                                     \
            AssertError, "statement '%s' returned but error %s, '%s' was expected", #statement, errorTypeName(&errorTypeExpected), \
            errorMessageExpected);                                                                                                 \
}

/***********************************************************************************************************************************
Format the test type into the given buffer -- or return verbatim if char *
***********************************************************************************************************************************/
#define TEST_TYPE_FORMAT_VAR(value)                                                                                                \
    char value##StrBuffer[TEST_RESULT_FORMAT_SIZE + 1];                                                                            \
    char *value##Str = value##StrBuffer;

#define TEST_TYPE_FORMAT_SPRINTF(format, value)                                                                                    \
    if (snprintf((char *)value##Str, TEST_RESULT_FORMAT_SIZE + 1, format, value) > TEST_RESULT_FORMAT_SIZE)                        \
    {                                                                                                                              \
        THROW(                                                                                                                     \
            AssertError, "formatted type '" format "' needs more than the %d characters available", TEST_RESULT_FORMAT_SIZE);      \
    }

#define TEST_TYPE_FORMAT(type, format, value)                                                                                      \
    TEST_TYPE_FORMAT_VAR(value);                                                                                                   \
    TEST_TYPE_FORMAT_SPRINTF(format, value);

#define TEST_TYPE_FORMAT_PTR(type, format, value)                                                                                  \
    TEST_TYPE_FORMAT_VAR(value);                                                                                                   \
                                                                                                                                   \
    if (value == NULL)                                                                                                             \
        value##Str = (char *)"NULL";                                                                                               \
    else if (strcmp(#type, "char *") == 0)                                                                                         \
        value##Str = (char *)value;                                                                                                \
    else                                                                                                                           \
        TEST_TYPE_FORMAT_SPRINTF(format, value);

/***********************************************************************************************************************************
Compare types
***********************************************************************************************************************************/
#define TEST_TYPE_COMPARE_STR(result, value, typeOp, valueExpected)                                                                \
    if (value != NULL && valueExpected != NULL)                                                                                    \
        result = strcmp((char *)value, (char *)valueExpected) typeOp 0;                                                            \
    else                                                                                                                           \
        result = value typeOp valueExpected;

#define TEST_TYPE_COMPARE(result, value, typeOp, valueExpected)                                                                    \
    result = value typeOp valueExpected;

/***********************************************************************************************************************************
Test the result of a statement and make sure it matches the expected value.  This macro can test any C type given the correct
parameters.
***********************************************************************************************************************************/
#define TEST_RESULT(statement, resultExpectedValue, type, format, formatMacro, typeOp, compareMacro, ...)                          \
{                                                                                                                                  \
    /* Assign expected result to a local variable */                                                                               \
    const type TEST_RESULT_resultExpected = (type)(resultExpectedValue);                                                           \
                                                                                                                                   \
    /* Output test info */                                                                                                         \
    printf("    l%04d - ", __LINE__);                                                                                              \
    printf(__VA_ARGS__);                                                                                                           \
    printf("\n");                                                                                                                  \
    fflush(stdout);                                                                                                                \
                                                                                                                                   \
    /* Format the expected result */                                                                                               \
    formatMacro(type, format, TEST_RESULT_resultExpected);                                                                         \
                                                                                                                                   \
    /* Try to run the statement.  Assign expected to result to silence compiler warning about unitialized var. */                  \
    type TEST_RESULT_result = (type)TEST_RESULT_resultExpected;                                                                    \
                                                                                                                                   \
    TRY_BEGIN()                                                                                                                    \
    {                                                                                                                              \
        TEST_RESULT_result = (type)(statement);                                                                                    \
    }                                                                                                                              \
    /* Catch any errors */                                                                                                         \
    CATCH_ANY()                                                                                                                    \
    {                                                                                                                              \
        /* No errors were expected so error */                                                                                     \
        THROW(                                                                                                                     \
            AssertError, "statement '%s' threw error %s, '%s' but result <%s> expected",                                           \
            #statement, errorName(), errorMessage(), TEST_RESULT_resultExpectedStr);                                               \
    }                                                                                                                              \
    TRY_END();                                                                                                                     \
                                                                                                                                   \
   /* Test the type operator */                                                                                                    \
    bool TEST_RESULT_resultOp = false;                                                                                             \
    compareMacro(TEST_RESULT_resultOp, TEST_RESULT_result, typeOp, TEST_RESULT_resultExpected);                                    \
                                                                                                                                   \
    /* If type operator test was not successful */                                                                                 \
    if (!TEST_RESULT_resultOp)                                                                                                     \
    {                                                                                                                              \
        /* Format the actual result */                                                                                             \
        formatMacro(type, format, TEST_RESULT_result);                                                                             \
                                                                                                                                   \
        /* Throw error */                                                                                                          \
        THROW(                                                                                                                     \
            AssertError, "statement '%s' result is '%s' but '%s' expected",                                                        \
            #statement, TEST_RESULT_resultStr, TEST_RESULT_resultExpectedStr);                                                     \
    }                                                                                                                              \
}

/***********************************************************************************************************************************
Macros to ease the use of common data types
***********************************************************************************************************************************/
#define TEST_RESULT_BOOL_PARAM(statement, resultExpected, typeOp, ...)                                                             \
    TEST_RESULT(statement, resultExpected, bool, "%d", TEST_TYPE_FORMAT, typeOp, TEST_TYPE_COMPARE, __VA_ARGS__);
#define TEST_RESULT_BOOL(statement, resultExpected, ...)                                                                           \
    TEST_RESULT_BOOL_PARAM(statement, resultExpected, ==, __VA_ARGS__);

#define TEST_RESULT_CHAR_PARAM(statement, resultExpected, typeOp, ...)                                                             \
    TEST_RESULT(statement, resultExpected, char, "%c", TEST_TYPE_FORMAT, typeOp, TEST_TYPE_COMPARE, __VA_ARGS__);
#define TEST_RESULT_CHAR(statement, resultExpected, ...)                                                                           \
    TEST_RESULT_CHAR_PARAM(statement, resultExpected, ==, __VA_ARGS__);
#define TEST_RESULT_CHAR_NE(statement, resultExpected, ...)                                                                        \
    TEST_RESULT_CHAR_PARAM(statement, resultExpected, !=, __VA_ARGS__);

#define TEST_RESULT_DOUBLE_PARAM(statement, resultExpected, typeOp, ...)                                                           \
    TEST_RESULT(statement, resultExpected, double, "%f", TEST_TYPE_FORMAT, typeOp, TEST_TYPE_COMPARE, __VA_ARGS__);
#define TEST_RESULT_DOUBLE(statement, resultExpected, ...)                                                                         \
    TEST_RESULT_DOUBLE_PARAM(statement, resultExpected, ==, __VA_ARGS__);

#define TEST_RESULT_INT_PARAM(statement, resultExpected, typeOp, ...)                                                              \
    TEST_RESULT(statement, resultExpected, int, "%d", TEST_TYPE_FORMAT, typeOp, TEST_TYPE_COMPARE, __VA_ARGS__);
#define TEST_RESULT_INT(statement, resultExpected, ...)                                                                            \
    TEST_RESULT_INT_PARAM(statement, resultExpected, ==, __VA_ARGS__);
#define TEST_RESULT_INT_NE(statement, resultExpected, ...)                                                                         \
    TEST_RESULT_INT_PARAM(statement, resultExpected, !=, __VA_ARGS__);

#define TEST_RESULT_PTR_PARAM(statement, resultExpected, typeOp, ...)                                                              \
    TEST_RESULT(statement, resultExpected, void *, "%p", TEST_TYPE_FORMAT_PTR, typeOp, TEST_TYPE_COMPARE, __VA_ARGS__);
#define TEST_RESULT_PTR(statement, resultExpected, ...)                                                                            \
    TEST_RESULT_PTR_PARAM(statement, resultExpected, ==, __VA_ARGS__);
#define TEST_RESULT_PTR_NE(statement, resultExpected, ...)                                                                         \
    TEST_RESULT_PTR_PARAM(statement, resultExpected, !=, __VA_ARGS__);

#define TEST_RESULT_STR_PARAM(statement, resultExpected, typeOp, ...)                                                              \
    TEST_RESULT(statement, resultExpected, char *, "%s", TEST_TYPE_FORMAT_PTR, typeOp, TEST_TYPE_COMPARE_STR, __VA_ARGS__);
#define TEST_RESULT_STR(statement, resultExpected, ...)                                                                            \
    TEST_RESULT_STR_PARAM(statement, resultExpected, ==, __VA_ARGS__);
#define TEST_RESULT_STR_NE(statement, resultExpected, ...)                                                                         \
    TEST_RESULT_STR_PARAM(statement, resultExpected, !=, __VA_ARGS__);

#define TEST_RESULT_U16_HEX(statement, resultExpected, ...)                                                                        \
    TEST_RESULT(statement, resultExpected, uint16, "0x%04X", TEST_TYPE_FORMAT, ==, TEST_TYPE_COMPARE, __VA_ARGS__);
