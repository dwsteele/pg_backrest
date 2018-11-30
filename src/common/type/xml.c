/***********************************************************************************************************************************
Xml Handler
***********************************************************************************************************************************/
#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "common/debug.h"
#include "common/log.h"
#include "common/memContext.h"
#include "common/type/list.h"
#include "common/type/xml.h"

/***********************************************************************************************************************************
Node type
***********************************************************************************************************************************/
struct XmlNode
{
    xmlNodePtr node;
};

/***********************************************************************************************************************************
Document type
***********************************************************************************************************************************/
struct XmlDocument
{
    MemContext *memContext;
    xmlDocPtr xml;
    XmlNode *root;
};

/***********************************************************************************************************************************
Error handler

For now this is a noop until more detailed error messages are needed.  The function is called multiple times per error, so the
messages need to be accumulated and then returned together.

This empty function is required because without it libxml2 will dump errors to stdout.  Really.
***********************************************************************************************************************************/
void xmlErrorHandler(void *ctx, const char *format, ...)
{
    (void)ctx;
    (void)format;
}

/***********************************************************************************************************************************
Create node list
***********************************************************************************************************************************/
XmlNodeList *
xmlNodeLstNew(void)
{
    FUNCTION_TEST_VOID();
    FUNCTION_TEST_RESULT(XML_NODE_LIST, (XmlNodeList *)lstNew(sizeof(XmlNode *)));
}

/***********************************************************************************************************************************
Get a node from the list
***********************************************************************************************************************************/
XmlNode *
xmlNodeLstGet(const XmlNodeList *this, unsigned int listIdx)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(XML_NODE_LIST, this);
        FUNCTION_TEST_PARAM(UINT, listIdx);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(XML_NODE, *(XmlNode **)lstGet((List *)this, listIdx));
}

/***********************************************************************************************************************************
Get node list size
***********************************************************************************************************************************/
unsigned int
xmlNodeLstSize(const XmlNodeList *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(XML_NODE_LIST, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(UINT, lstSize((List *)this));
}

/***********************************************************************************************************************************
Free node list
***********************************************************************************************************************************/
void
xmlNodeLstFree(XmlNodeList *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(XML_NODE_LIST, this);
    FUNCTION_TEST_END();

    lstFree((List *)this);

    FUNCTION_TEST_RESULT_VOID();
}

/***********************************************************************************************************************************
Create node
***********************************************************************************************************************************/
static XmlNode *
xmlNodeNew(xmlNodePtr node)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(VOIDP, node);

        FUNCTION_TEST_ASSERT(node != NULL);
    FUNCTION_TEST_END();

    XmlNode *this = memNew(sizeof(XmlNode));
    this->node = node;

    FUNCTION_TEST_RESULT(XML_NODE, this);
}

/***********************************************************************************************************************************
Add a node to a node list
***********************************************************************************************************************************/
static XmlNodeList *
xmlNodeLstAdd(XmlNodeList *this, xmlNodePtr node)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(XML_NODE_LIST, this);
        FUNCTION_TEST_PARAM(VOIDP, node);

        FUNCTION_TEST_ASSERT(this != NULL);
        FUNCTION_TEST_ASSERT(node != NULL);
    FUNCTION_TEST_END();

    MEM_CONTEXT_BEGIN(lstMemContext((List *)this))
    {
        XmlNode *item = xmlNodeNew(node);
        lstAdd((List *)this, &item);
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RESULT(XML_NODE_LIST, this);
}

/***********************************************************************************************************************************
Get node attribute
***********************************************************************************************************************************/
String *
xmlNodeAttribute(XmlNode *this, const String *name)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(XML_NODE, this);
        FUNCTION_TEST_PARAM(STRING, name);

        FUNCTION_TEST_ASSERT(this != NULL);
        FUNCTION_TEST_ASSERT(name != NULL);
    FUNCTION_TEST_END();

    String *result = NULL;
    xmlChar *value = xmlGetProp(this->node, (unsigned char *)strPtr(name));

    if (value != NULL)
    {
        result = strNew((char *)value);
        xmlFree(value);
    }

    FUNCTION_TEST_RESULT(STRING, result);
}

/***********************************************************************************************************************************
Get node content
***********************************************************************************************************************************/
String *
xmlNodeContent(XmlNode *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(XML_NODE, this);
    FUNCTION_TEST_END();

    String *result = NULL;

    if (this != NULL)
    {
        xmlChar *content = xmlNodeGetContent(this->node);
        result = strNew((char *)content);
        xmlFree(content);
    }

    FUNCTION_TEST_RESULT(STRING, result);
}

/***********************************************************************************************************************************
Get a list of child nodes
***********************************************************************************************************************************/
XmlNodeList *
xmlNodeChildList(XmlNode *this, const String *name)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(XML_NODE, this);
        FUNCTION_TEST_PARAM(STRING, name);

        FUNCTION_TEST_ASSERT(this != NULL);
        FUNCTION_TEST_ASSERT(name != NULL);
    FUNCTION_TEST_END();

    XmlNodeList *list = xmlNodeLstNew();

    for (xmlNodePtr currentNode = this->node->children; currentNode != NULL; currentNode = currentNode->next)
    {
        if (currentNode->type == XML_ELEMENT_NODE && strEqZ(name, (char *)currentNode->name))
            xmlNodeLstAdd(list, currentNode);
    }

    FUNCTION_TEST_RESULT(XML_NODE_LIST, list);
}

/***********************************************************************************************************************************
Get a child node
***********************************************************************************************************************************/
XmlNode *
xmlNodeChildN(XmlNode *this, const String *name, unsigned int index, bool errorOnMissing)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(XML_NODE, this);
        FUNCTION_TEST_PARAM(STRING, name);
        FUNCTION_TEST_PARAM(UINT, index);

        FUNCTION_TEST_ASSERT(this != NULL);
        FUNCTION_TEST_ASSERT(name != NULL);
    FUNCTION_TEST_END();

    XmlNode *child = NULL;
    unsigned int childIdx = 0;

    for (xmlNodePtr currentNode = this->node->children; currentNode != NULL; currentNode = currentNode->next)
    {
        if (currentNode->type == XML_ELEMENT_NODE && strEqZ(name, (char *)currentNode->name))
        {
            if (childIdx == index)
            {
                child = xmlNodeNew(currentNode);
                break;
            }

            childIdx++;
        }
    }

    if (child == NULL && errorOnMissing)
        THROW_FMT(FormatError, "unable to find child '%s':%u in node '%s'", strPtr(name), index, this->node->name);

    FUNCTION_TEST_RESULT(XML_NODE, child);
}

XmlNode *
xmlNodeChild(XmlNode *this, const String *name, bool errorOnMissing)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(XML_NODE, this);
        FUNCTION_TEST_PARAM(STRING, name);

        FUNCTION_TEST_ASSERT(this != NULL);
        FUNCTION_TEST_ASSERT(name != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(XML_NODE, xmlNodeChildN(this, name, 0, errorOnMissing));
}

/***********************************************************************************************************************************
Get child total
***********************************************************************************************************************************/
unsigned int
xmlNodeChildTotal(XmlNode *this, const String *name)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(XML_NODE, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    unsigned int result = 0;

    for (xmlNodePtr currentNode = this->node->children; currentNode != NULL; currentNode = currentNode->next)
    {
        if (currentNode->type == XML_ELEMENT_NODE && strEqZ(name, (char *)currentNode->name))
            result++;
    }

    FUNCTION_TEST_RESULT(UINT, result);
}

/***********************************************************************************************************************************
Free node
***********************************************************************************************************************************/
void
xmlNodeFree(XmlNode *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(XML_NODE, this);
    FUNCTION_TEST_END();

    if (this != NULL)
        memFree(this);

    FUNCTION_TEST_RESULT_VOID();
}

/***********************************************************************************************************************************
Create document from C buffer
***********************************************************************************************************************************/
XmlDocument *
xmlDocumentNewC(const unsigned char *buffer, size_t bufferSize)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(UCHARP, buffer);
        FUNCTION_TEST_PARAM(SIZE, bufferSize);

        FUNCTION_TEST_ASSERT(buffer != NULL);
        FUNCTION_TEST_ASSERT(bufferSize > 0);
    FUNCTION_TEST_END();

    // Initialize xml if it is not already initialized
    static bool xmlInit = false;

    if (!xmlInit)
    {
        LIBXML_TEST_VERSION;

        // It's a pretty weird that we can't just pass a handler function but instead have to assign it to a var...
        static xmlGenericErrorFunc xmlErrorHandlerFunc = xmlErrorHandler;
        initGenericErrorDefaultFunc(&xmlErrorHandlerFunc);

        xmlInit = true;
    }

    // Create object
    XmlDocument *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("XmlDocument")
    {
        this = memNew(sizeof(XmlDocument));
        this->memContext = MEM_CONTEXT_NEW();

        if ((this->xml = xmlReadMemory((const char *)buffer, (int)bufferSize, "noname.xml", NULL, 0)) == NULL)
            THROW_FMT(FormatError, "invalid xml");

        // Set callback to ensure xml document is freed
        memContextCallback(this->memContext, (MemContextCallback)xmlDocumentFree, this);

        // Get the root node
        this->root = xmlNodeNew(xmlDocGetRootElement(this->xml));
    }
    MEM_CONTEXT_NEW_END();

    FUNCTION_TEST_RESULT(XML_DOCUMENT, this);
}

/***********************************************************************************************************************************
Create document from Buffer
***********************************************************************************************************************************/
XmlDocument *
xmlDocumentNewBuf(const Buffer *buffer)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(BUFFER, buffer);

        FUNCTION_TEST_ASSERT(buffer != NULL);
        FUNCTION_TEST_ASSERT(bufSize(buffer) > 0);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(XML_DOCUMENT, xmlDocumentNewC(bufPtr(buffer), bufSize(buffer)));
}

/***********************************************************************************************************************************
Create document from zero-terminated string
***********************************************************************************************************************************/
XmlDocument *
xmlDocumentNewZ(const char *string)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(CHARP, string);

        FUNCTION_TEST_ASSERT(string != NULL);
        FUNCTION_TEST_ASSERT(strlen(string) > 0);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(XML_DOCUMENT, xmlDocumentNewC((const unsigned char *)string, strlen(string)));
}

/***********************************************************************************************************************************
Get the root node
***********************************************************************************************************************************/
XmlNode *
xmlDocumentRoot(const XmlDocument *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(XML_DOCUMENT, this);

        FUNCTION_TEST_ASSERT(this != NULL);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RESULT(XML_NODE, this->root);
}

/***********************************************************************************************************************************
Free document
***********************************************************************************************************************************/
void
xmlDocumentFree(XmlDocument *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(XML_DOCUMENT, this);
    FUNCTION_TEST_END();

    if (this != NULL)
    {
        xmlFreeDoc(this->xml);

        memContextCallbackClear(this->memContext);
        memContextFree(this->memContext);
    }

    FUNCTION_TEST_RESULT_VOID();
}
