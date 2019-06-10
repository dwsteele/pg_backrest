/***********************************************************************************************************************************
Test S3 Storage
***********************************************************************************************************************************/
#include <unistd.h>

#include "common/harnessConfig.h"
#include "common/harnessTls.h"

/***********************************************************************************************************************************
Test server
***********************************************************************************************************************************/
#define S3_TEST_HOST                                                "bucket.s3.amazonaws.com"
#define DATE_REPLACE                                                "????????"
#define DATETIME_REPLACE                                            "????????T??????Z"
#define SHA256_REPLACE                                                                                                             \
    "????????????????????????????????????????????????????????????????"

static const char *
testS3ServerRequest(const char *verb, const char *uri, const char *content)
{
    String *request = strNewFmt(
        "%s %s HTTP/1.1\r\n"
            "authorization:AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/" DATE_REPLACE "/us-east-1/s3/aws4_request,"
                "SignedHeaders=content-length;",
        verb, uri);

    if (content != NULL)
        strCat(request, "content-md5;");

    strCatFmt(
        request,
        "host;x-amz-content-sha256;x-amz-date,Signature=" SHA256_REPLACE "\r\n"
        "content-length:%zu\r\n",
        content == NULL ? 0 : strlen(content));

    if (content != NULL)
    {
        char md5Hash[HASH_TYPE_MD5_SIZE_HEX];
        encodeToStr(encodeBase64, bufPtr(cryptoHashOne(HASH_TYPE_MD5_STR, BUFSTRZ(content))), HASH_TYPE_M5_SIZE, md5Hash);
        strCatFmt(request, "content-md5:%s\r\n", md5Hash);
    }

    strCatFmt(
        request,
        "host:" S3_TEST_HOST "\r\n"
        "x-amz-content-sha256:%s\r\n"
        "x-amz-date:" DATETIME_REPLACE "\r\n"
        "\r\n",
        content == NULL ?
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" :
            strPtr(bufHex(cryptoHashOne(HASH_TYPE_SHA256_STR, BUFSTRZ(content)))));

    if (content != NULL)
        strCat(request, content);

    return strPtr(request);
}

static const char *
testS3ServerResponse(unsigned int code, const char *message, const char *header, const char *content)
{
    String *response = strNewFmt("HTTP/1.1 %u %s\r\n", code, message);

    if (header != NULL)
        strCatFmt(response, "%s\r\n", header);

    if (content != NULL)
    {
        strCatFmt(
            response,
            "content-length:%zu\r\n"
                "\r\n"
                "%s",
            strlen(content), content);
    }
    else
        strCat(response, "\r\n");

    return strPtr(response);
}

static void
testS3Server(void)
{
    if (fork() == 0)
    {
        harnessTlsServerInit(TLS_TEST_PORT, TLS_CERT_TEST_CERT, TLS_CERT_TEST_KEY);
        harnessTlsServerAccept();

        // storageS3NewRead() and StorageS3FileRead
        // -------------------------------------------------------------------------------------------------------------------------
        // Ignore missing file
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_GET, "/fi%26le.txt", NULL));
        harnessTlsServerReply(testS3ServerResponse(404, "Not Found", NULL, NULL));

        // Error on missing file
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_GET, "/file.txt", NULL));
        harnessTlsServerReply(testS3ServerResponse(404, "Not Found", NULL, NULL));

        // Get file
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_GET, "/file.txt", NULL));
        harnessTlsServerReply(testS3ServerResponse(200, "OK", NULL, "this is a sample file"));

        // Get zero-length file
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_GET, "/file0.txt", NULL));
        harnessTlsServerReply(testS3ServerResponse(200, "OK", NULL, NULL));

        // Throw non-404 error
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_GET, "/file.txt", NULL));
        harnessTlsServerReply(testS3ServerResponse(303, "Some bad status", NULL, "CONTENT"));

        // storageS3NewWrite() and StorageWriteS3
        // -------------------------------------------------------------------------------------------------------------------------
        // File is written all at once
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_PUT, "/file.txt", "ABCD"));
        harnessTlsServerReply(testS3ServerResponse(200, "OK", NULL, NULL));

        // Zero-length file
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_PUT, "/file.txt", ""));
        harnessTlsServerReply(testS3ServerResponse(200, "OK", NULL, NULL));

        // File is written in chunks with nothing left over on close
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_POST, "/file.txt?uploads=", NULL));
        harnessTlsServerReply(testS3ServerResponse(
            200, "OK", NULL,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<InitiateMultipartUploadResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
                "<Bucket>bucket</Bucket>"
                "<Key>file.txt</Key>"
                "<UploadId>WxRt</UploadId>"
                "</InitiateMultipartUploadResult>"));

        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_PUT, "/file.txt?partNumber=1&uploadId=WxRt", "1234567890123456"));
        harnessTlsServerReply(testS3ServerResponse(200, "OK", "etag:WxRt1", NULL));
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_PUT, "/file.txt?partNumber=2&uploadId=WxRt", "7890123456789012"));
        harnessTlsServerReply(testS3ServerResponse(200, "OK", "eTag:WxRt2", NULL));

        harnessTlsServerExpect(testS3ServerRequest(
            HTTP_VERB_POST, "/file.txt?uploadId=WxRt",
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<CompleteMultipartUpload>"
                "<Part><PartNumber>1</PartNumber><ETag>WxRt1</ETag></Part>"
                "<Part><PartNumber>2</PartNumber><ETag>WxRt2</ETag></Part>"
                "</CompleteMultipartUpload>\n"));
        harnessTlsServerReply(testS3ServerResponse(200, "OK", NULL, NULL));

        // File is written in chunks with something left over on close
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_POST, "/file.txt?uploads=", NULL));
        harnessTlsServerReply(testS3ServerResponse(
            200, "OK", NULL,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<InitiateMultipartUploadResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
                "<Bucket>bucket</Bucket>"
                "<Key>file.txt</Key>"
                "<UploadId>RR55</UploadId>"
                "</InitiateMultipartUploadResult>"));

        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_PUT, "/file.txt?partNumber=1&uploadId=RR55", "1234567890123456"));
        harnessTlsServerReply(testS3ServerResponse(200, "OK", "etag:RR551", NULL));
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_PUT, "/file.txt?partNumber=2&uploadId=RR55", "7890"));
        harnessTlsServerReply(testS3ServerResponse(200, "OK", "eTag:RR552", NULL));

        harnessTlsServerExpect(testS3ServerRequest(
            HTTP_VERB_POST, "/file.txt?uploadId=RR55",
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<CompleteMultipartUpload>"
                "<Part><PartNumber>1</PartNumber><ETag>RR551</ETag></Part>"
                "<Part><PartNumber>2</PartNumber><ETag>RR552</ETag></Part>"
                "</CompleteMultipartUpload>\n"));
        harnessTlsServerReply(testS3ServerResponse(200, "OK", NULL, NULL));

        // storageDriverExists()
        // -------------------------------------------------------------------------------------------------------------------------
        // File missing
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_HEAD, "/BOGUS", NULL));
        harnessTlsServerReply(testS3ServerResponse(404, "Not Found", NULL, NULL));

        // File exists
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_HEAD, "/subdir/file1.txt", NULL));
        harnessTlsServerReply(testS3ServerResponse(200, "OK", "content-length:999", NULL));

        // Info()
        // -------------------------------------------------------------------------------------------------------------------------
        // File missing
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_HEAD, "/BOGUS", NULL));
        harnessTlsServerReply(testS3ServerResponse(404, "Not Found", NULL, NULL));

        // File exists
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_HEAD, "/subdir/file1.txt", NULL));
        harnessTlsServerReply(testS3ServerResponse(200, "OK", "content-length:9999", NULL));

        // InfoList()
        // -------------------------------------------------------------------------------------------------------------------------
        harnessTlsServerExpect(
            testS3ServerRequest(HTTP_VERB_GET, "/?delimiter=%2F&list-type=2&prefix=path%2Fto%2F", NULL));
        harnessTlsServerReply(
            testS3ServerResponse(
                200, "OK", NULL,
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
                "    <Contents>"
                "        <Key>path/to/test_file</Key>"
                "        <Size>787</Size>"
                "    </Contents>"
                "   <CommonPrefixes>"
                "       <Prefix>path/to/test_path/</Prefix>"
                "   </CommonPrefixes>"
                "</ListBucketResult>"));

        // storageDriverList()
        // -------------------------------------------------------------------------------------------------------------------------
        // Throw error
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_GET, "/?delimiter=%2F&list-type=2", NULL));
        harnessTlsServerReply(testS3ServerResponse(344, "Another bad status", NULL, NULL));

        // list a file/path in root
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_GET, "/?delimiter=%2F&list-type=2", NULL));
        harnessTlsServerReply(
            testS3ServerResponse(
                200, "OK", NULL,
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
                "    <Contents>"
                "        <Key>test1.txt</Key>"
                "    </Contents>"
                "   <CommonPrefixes>"
                "       <Prefix>path1/</Prefix>"
                "   </CommonPrefixes>"
                "</ListBucketResult>"));

        // list a file in root with expression
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_GET, "/?delimiter=%2F&list-type=2&prefix=test", NULL));
        harnessTlsServerReply(
            testS3ServerResponse(
                200, "OK", NULL,
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
                "    <Contents>"
                "        <Key>test1.txt</Key>"
                "    </Contents>"
                "</ListBucketResult>"));

        // list files with continuation
        harnessTlsServerExpect(
            testS3ServerRequest(HTTP_VERB_GET, "/?delimiter=%2F&list-type=2&prefix=path%2Fto%2F", NULL));
        harnessTlsServerReply(
            testS3ServerResponse(
                200, "OK", NULL,
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
                "    <NextContinuationToken>1ueGcxLPRx1Tr/XYExHnhbYLgveDs2J/wm36Hy4vbOwM=</NextContinuationToken>"
                "    <Contents>"
                "        <Key>path/to/test1.txt</Key>"
                "    </Contents>"
                "    <Contents>"
                "        <Key>path/to/test2.txt</Key>"
                "    </Contents>"
                "   <CommonPrefixes>"
                "       <Prefix>path/to/path1/</Prefix>"
                "   </CommonPrefixes>"
                "</ListBucketResult>"));

        harnessTlsServerExpect(
            testS3ServerRequest(
                HTTP_VERB_GET,
                "/?continuation-token=1ueGcxLPRx1Tr%2FXYExHnhbYLgveDs2J%2Fwm36Hy4vbOwM%3D&delimiter=%2F&list-type=2"
                    "&prefix=path%2Fto%2F",
                NULL));
        harnessTlsServerReply(
            testS3ServerResponse(
                200, "OK", NULL,
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
                "    <Contents>"
                "        <Key>path/to/test3.txt</Key>"
                "    </Contents>"
                "   <CommonPrefixes>"
                "       <Prefix>path/to/path2/</Prefix>"
                "   </CommonPrefixes>"
                "</ListBucketResult>"));

        // list files with expression
        harnessTlsServerExpect(
            testS3ServerRequest(HTTP_VERB_GET, "/?delimiter=%2F&list-type=2&prefix=path%2Fto%2Ftest", NULL));
        harnessTlsServerReply(
            testS3ServerResponse(
                200, "OK", NULL,
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
                "    <Contents>"
                "        <Key>path/to/test1.txt</Key>"
                "    </Contents>"
                "    <Contents>"
                "        <Key>path/to/test2.txt</Key>"
                "    </Contents>"
                "    <Contents>"
                "        <Key>path/to/test3.txt</Key>"
                "    </Contents>"
                "   <CommonPrefixes>"
                "       <Prefix>path/to/test1.path/</Prefix>"
                "   </CommonPrefixes>"
                "   <CommonPrefixes>"
                "       <Prefix>path/to/test2.path/</Prefix>"
                "   </CommonPrefixes>"
                "</ListBucketResult>"));

        // storageDriverPathRemove()
        // -------------------------------------------------------------------------------------------------------------------------
        // delete files from root
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_GET, "/?list-type=2", NULL));
        harnessTlsServerReply(
            testS3ServerResponse(
                200, "OK", NULL,
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
                "    <Contents>"
                "        <Key>test1.txt</Key>"
                "    </Contents>"
                "    <Contents>"
                "        <Key>path1/xxx.zzz</Key>"
                "    </Contents>"
                "</ListBucketResult>"));

        harnessTlsServerExpect(
            testS3ServerRequest(HTTP_VERB_POST, "/?delete=",
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<Delete><Quiet>true</Quiet>"
                "<Object><Key>test1.txt</Key></Object>"
                "<Object><Key>path1/xxx.zzz</Key></Object>"
                "</Delete>\n"));
        harnessTlsServerReply(
            testS3ServerResponse(
                200, "OK", NULL, "<DeleteResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\"></DeleteResult>"));

        // nothing to do in empty subpath
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_GET, "/?list-type=2&prefix=path%2F", NULL));
        harnessTlsServerReply(
            testS3ServerResponse(
                200, "OK", NULL,
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
                "</ListBucketResult>"));

        // delete with continuation
        harnessTlsServerExpect(
            testS3ServerRequest(HTTP_VERB_GET, "/?list-type=2&prefix=path%2Fto%2F", NULL));
        harnessTlsServerReply(
            testS3ServerResponse(
                200, "OK", NULL,
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
                "    <NextContinuationToken>continue</NextContinuationToken>"
                "   <CommonPrefixes>"
                "       <Prefix>path/to/test3/</Prefix>"
                "   </CommonPrefixes>"
                "    <Contents>"
                "        <Key>path/to/test1.txt</Key>"
                "    </Contents>"
                "</ListBucketResult>"));

        harnessTlsServerExpect(
            testS3ServerRequest(HTTP_VERB_GET, "/?continuation-token=continue&list-type=2&prefix=path%2Fto%2F", NULL));
        harnessTlsServerReply(
            testS3ServerResponse(
                200, "OK", NULL,
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
                "    <Contents>"
                "        <Key>path/to/test3.txt</Key>"
                "    </Contents>"
                "    <Contents>"
                "        <Key>path/to/test2.txt</Key>"
                "    </Contents>"
                "</ListBucketResult>"));

        harnessTlsServerExpect(
            testS3ServerRequest(HTTP_VERB_POST, "/?delete=",
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<Delete><Quiet>true</Quiet>"
                "<Object><Key>path/to/test1.txt</Key></Object>"
                "<Object><Key>path/to/test3.txt</Key></Object>"
                "</Delete>\n"));
        harnessTlsServerReply(testS3ServerResponse(200, "OK", NULL, NULL));

        harnessTlsServerExpect(
            testS3ServerRequest(HTTP_VERB_POST, "/?delete=",
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<Delete><Quiet>true</Quiet>"
                "<Object><Key>path/to/test2.txt</Key></Object>"
                "</Delete>\n"));
        harnessTlsServerReply(testS3ServerResponse(200, "OK", NULL, NULL));

        // delete error
        harnessTlsServerExpect(
            testS3ServerRequest(HTTP_VERB_GET, "/?list-type=2&prefix=path%2F", NULL));
        harnessTlsServerReply(
            testS3ServerResponse(
                200, "OK", NULL,
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
                "    <Contents>"
                "        <Key>path/sample.txt</Key>"
                "    </Contents>"
                "    <Contents>"
                "        <Key>path/sample2.txt</Key>"
                "    </Contents>"
                "</ListBucketResult>"));

        harnessTlsServerExpect(
            testS3ServerRequest(HTTP_VERB_POST, "/?delete=",
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<Delete><Quiet>true</Quiet>"
                "<Object><Key>path/sample.txt</Key></Object>"
                "<Object><Key>path/sample2.txt</Key></Object>"
                "</Delete>\n"));
        harnessTlsServerReply(
            testS3ServerResponse(
                200, "OK", NULL,
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<DeleteResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
                    "<Error><Key>sample2.txt</Key><Code>AccessDenied</Code><Message>Access Denied</Message></Error>"
                    "</DeleteResult>"));

        // storageDriverRemove()
        // -------------------------------------------------------------------------------------------------------------------------
        // remove file
        harnessTlsServerExpect(testS3ServerRequest(HTTP_VERB_DELETE, "/path/to/test.txt", NULL));
        harnessTlsServerReply(testS3ServerResponse(204, "No Content", NULL, NULL));

        harnessTlsServerClose();
        exit(0);
    }
}

/***********************************************************************************************************************************
Callback and data for storageInfoList() tests
***********************************************************************************************************************************/
unsigned int testStorageInfoListSize = 0;
StorageInfo testStorageInfoList[256];

void
testStorageInfoListCallback(void *callbackData, const StorageInfo *info)
{
    MEM_CONTEXT_BEGIN((MemContext *)callbackData)
    {
        testStorageInfoList[testStorageInfoListSize] = *info;
        testStorageInfoList[testStorageInfoListSize].name = strDup(info->name);
        testStorageInfoListSize++;
    }
    MEM_CONTEXT_END();
}

/***********************************************************************************************************************************
Test Run
***********************************************************************************************************************************/
void
testRun(void)
{
    FUNCTION_HARNESS_VOID();

    // Test strings
    const String *path = strNew("/");
    const String *bucket = strNew("bucket");
    const String *region = strNew("us-east-1");
    const String *endPoint = strNew("s3.amazonaws.com");
    const String *host = strNew(TLS_TEST_HOST);
    const unsigned int port = TLS_TEST_PORT;
    const String *accessKey = strNew("AKIAIOSFODNN7EXAMPLE");
    const String *secretAccessKey = strNew("wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY");
    const String *securityToken = strNew(
        "AQoDYXdzEPT//////////wEXAMPLEtc764bNrC9SAPBSM22wDOk4x4HIZ8j4FZTwdQWLWsKWHGBuFqwAeMicRXmxfpSPfIeoIYRqTflfKD8YUuwthAx7mSEI/q"
        "kPpKPi/kMcGdQrmGdeehM4IC1NtBmUpp2wUE8phUZampKsburEDy0KPkyQDYwT7WZ0wq5VSXDvp75YU9HFvlRd8Tx6q6fE8YQcHNVXAkiY9q6d+xo0rKwT38xV"
        "qr7ZD0u0iPPkUL64lIZbqBAz+scqKmlzm8FDrypNC9Yjc8fPOLn9FX9KSYvKTr4rvx3iSIlTJabIQwj2ICCR/oLxBA==");

    // *****************************************************************************************************************************
    if (testBegin("storageS3New() and storageRepoGet()"))
    {
        // Only required options
        // -------------------------------------------------------------------------------------------------------------------------
        StringList *argList = strLstNew();
        strLstAddZ(argList, "pgbackrest");
        strLstAddZ(argList, "--stanza=db");
        strLstAddZ(argList, "--repo1-type=s3");
        strLstAdd(argList, strNewFmt("--repo1-path=%s", strPtr(path)));
        strLstAdd(argList, strNewFmt("--repo1-s3-bucket=%s", strPtr(bucket)));
        strLstAdd(argList, strNewFmt("--repo1-s3-region=%s", strPtr(region)));
        strLstAdd(argList, strNewFmt("--repo1-s3-endpoint=%s", strPtr(endPoint)));
        setenv("PGBACKREST_REPO1_S3_KEY", strPtr(accessKey), true);
        setenv("PGBACKREST_REPO1_S3_KEY_SECRET", strPtr(secretAccessKey), true);
        strLstAddZ(argList, "archive-get");
        harnessCfgLoad(strLstSize(argList), strLstPtr(argList));

        Storage *storage = NULL;
        TEST_ASSIGN(storage, storageRepoGet(strNew(STORAGE_TYPE_S3), false), "get S3 repo storage");
        TEST_RESULT_STR(strPtr(storage->path), strPtr(path), "    check path");
        TEST_RESULT_STR(strPtr(((StorageS3 *)storage->driver)->bucket), strPtr(bucket), "    check bucket");
        TEST_RESULT_STR(strPtr(((StorageS3 *)storage->driver)->region), strPtr(region), "    check region");
        TEST_RESULT_STR(
            strPtr(((StorageS3 *)storage->driver)->host), strPtr(strNewFmt("%s.%s", strPtr(bucket), strPtr(endPoint))),
            "    check host");
        TEST_RESULT_STR(strPtr(((StorageS3 *)storage->driver)->accessKey), strPtr(accessKey), "    check access key");
        TEST_RESULT_STR(
            strPtr(((StorageS3 *)storage->driver)->secretAccessKey), strPtr(secretAccessKey), "    check secret access key");
        TEST_RESULT_PTR(((StorageS3 *)storage->driver)->securityToken, NULL, "    check security token");

        // Add default options
        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAddZ(argList, "pgbackrest");
        strLstAddZ(argList, "--stanza=db");
        strLstAddZ(argList, "--repo1-type=s3");
        strLstAdd(argList, strNewFmt("--repo1-path=%s", strPtr(path)));
        strLstAdd(argList, strNewFmt("--repo1-s3-bucket=%s", strPtr(bucket)));
        strLstAdd(argList, strNewFmt("--repo1-s3-region=%s", strPtr(region)));
        strLstAdd(argList, strNewFmt("--repo1-s3-endpoint=%s", strPtr(endPoint)));
        strLstAdd(argList, strNewFmt("--repo1-s3-host=%s", strPtr(host)));
        strLstAddZ(argList, "--repo1-s3-ca-path=" TLS_CERT_FAKE_PATH);
        strLstAddZ(argList, "--repo1-s3-ca-file=" TLS_CERT_FAKE_PATH "/pgbackrest-test.crt");
        setenv("PGBACKREST_REPO1_S3_KEY", strPtr(accessKey), true);
        setenv("PGBACKREST_REPO1_S3_KEY_SECRET", strPtr(secretAccessKey), true);
        setenv("PGBACKREST_REPO1_S3_TOKEN", strPtr(securityToken), true);
        strLstAddZ(argList, "archive-get");
        harnessCfgLoad(strLstSize(argList), strLstPtr(argList));

        TEST_ASSIGN(storage, storageRepoGet(strNew(STORAGE_TYPE_S3), false), "get S3 repo storage with options");
        TEST_RESULT_STR(strPtr(((StorageS3 *)storage->driver)->bucket), strPtr(bucket), "    check bucket");
        TEST_RESULT_STR(strPtr(((StorageS3 *)storage->driver)->region), strPtr(region), "    check region");
        TEST_RESULT_STR(
            strPtr(((StorageS3 *)storage->driver)->host), strPtr(strNewFmt("%s.%s", strPtr(bucket), strPtr(endPoint))),
            "    check host");
        TEST_RESULT_UINT(((StorageS3 *)storage->driver)->port, 443, "    check port");
        TEST_RESULT_STR(strPtr(((StorageS3 *)storage->driver)->accessKey), strPtr(accessKey), "    check access key");
        TEST_RESULT_STR(
            strPtr(((StorageS3 *)storage->driver)->secretAccessKey), strPtr(secretAccessKey), "    check secret access key");
        TEST_RESULT_STR(
            strPtr(((StorageS3 *)storage->driver)->securityToken), strPtr(securityToken), "    check security token");

        // Add a port to the endpoint
        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAddZ(argList, "pgbackrest");
        strLstAddZ(argList, "--stanza=db");
        strLstAddZ(argList, "--repo1-type=s3");
        strLstAdd(argList, strNewFmt("--repo1-path=%s", strPtr(path)));
        strLstAdd(argList, strNewFmt("--repo1-s3-bucket=%s", strPtr(bucket)));
        strLstAdd(argList, strNewFmt("--repo1-s3-region=%s", strPtr(region)));
        strLstAdd(argList, strNewFmt("--repo1-s3-endpoint=%s:999", strPtr(endPoint)));
        strLstAddZ(argList, "--repo1-s3-ca-path=" TLS_CERT_FAKE_PATH);
        strLstAddZ(argList, "--repo1-s3-ca-file=" TLS_CERT_FAKE_PATH "/pgbackrest-test.crt");
        setenv("PGBACKREST_REPO1_S3_KEY", strPtr(accessKey), true);
        setenv("PGBACKREST_REPO1_S3_KEY_SECRET", strPtr(secretAccessKey), true);
        setenv("PGBACKREST_REPO1_S3_TOKEN", strPtr(securityToken), true);
        strLstAddZ(argList, "archive-get");
        harnessCfgLoad(strLstSize(argList), strLstPtr(argList));

        TEST_ASSIGN(storage, storageRepoGet(strNew(STORAGE_TYPE_S3), false), "get S3 repo storage with options");
        TEST_RESULT_STR(strPtr(((StorageS3 *)storage->driver)->bucket), strPtr(bucket), "    check bucket");
        TEST_RESULT_STR(strPtr(((StorageS3 *)storage->driver)->region), strPtr(region), "    check region");
        TEST_RESULT_STR(strPtr(((StorageS3 *)storage->driver)->host), strPtr(strNewFmt("%s.%s", strPtr(bucket), strPtr(endPoint))), "    check host");
        TEST_RESULT_UINT(((StorageS3 *)storage->driver)->port, 999, "    check port");
        TEST_RESULT_STR(strPtr(((StorageS3 *)storage->driver)->accessKey), strPtr(accessKey), "    check access key");
        TEST_RESULT_STR(
            strPtr(((StorageS3 *)storage->driver)->secretAccessKey), strPtr(secretAccessKey), "    check secret access key");
        TEST_RESULT_STR(
            strPtr(((StorageS3 *)storage->driver)->securityToken), strPtr(securityToken), "    check security token");

        // Also add port to the host
        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAddZ(argList, "pgbackrest");
        strLstAddZ(argList, "--stanza=db");
        strLstAddZ(argList, "--repo1-type=s3");
        strLstAdd(argList, strNewFmt("--repo1-path=%s", strPtr(path)));
        strLstAdd(argList, strNewFmt("--repo1-s3-bucket=%s", strPtr(bucket)));
        strLstAdd(argList, strNewFmt("--repo1-s3-region=%s", strPtr(region)));
        strLstAdd(argList, strNewFmt("--repo1-s3-endpoint=%s:999", strPtr(endPoint)));
        strLstAdd(argList, strNewFmt("--repo1-s3-host=%s:7777", strPtr(host)));
        strLstAddZ(argList, "--repo1-s3-ca-path=" TLS_CERT_FAKE_PATH);
        strLstAddZ(argList, "--repo1-s3-ca-file=" TLS_CERT_FAKE_PATH "/pgbackrest-test.crt");
        setenv("PGBACKREST_REPO1_S3_KEY", strPtr(accessKey), true);
        setenv("PGBACKREST_REPO1_S3_KEY_SECRET", strPtr(secretAccessKey), true);
        setenv("PGBACKREST_REPO1_S3_TOKEN", strPtr(securityToken), true);
        strLstAddZ(argList, "archive-get");
        harnessCfgLoad(strLstSize(argList), strLstPtr(argList));

        TEST_ASSIGN(storage, storageRepoGet(strNew(STORAGE_TYPE_S3), false), "get S3 repo storage with options");
        TEST_RESULT_STR(strPtr(((StorageS3 *)storage->driver)->bucket), strPtr(bucket), "    check bucket");
        TEST_RESULT_STR(strPtr(((StorageS3 *)storage->driver)->region), strPtr(region), "    check region");
        TEST_RESULT_STR(
            strPtr(((StorageS3 *)storage->driver)->host), strPtr(strNewFmt("%s.%s", strPtr(bucket), strPtr(endPoint))),
            "    check host");
        TEST_RESULT_UINT(((StorageS3 *)storage->driver)->port, 7777, "    check port");
        TEST_RESULT_STR(strPtr(((StorageS3 *)storage->driver)->accessKey), strPtr(accessKey), "    check access key");
        TEST_RESULT_STR(
            strPtr(((StorageS3 *)storage->driver)->secretAccessKey), strPtr(secretAccessKey), "    check secret access key");
        TEST_RESULT_STR(
            strPtr(((StorageS3 *)storage->driver)->securityToken), strPtr(securityToken), "    check security token");
    }

    // *****************************************************************************************************************************
    if (testBegin("storageS3DateTime() and storageS3Auth()"))
    {
        TEST_RESULT_STR(strPtr(storageS3DateTime(1491267845)), "20170404T010405Z", "static date");

        // -------------------------------------------------------------------------------------------------------------------------
        StorageS3 *driver = (StorageS3 *)storageDriver(
            storageS3New(
                path, true, NULL, bucket, endPoint, region, accessKey, secretAccessKey, NULL, 16, 2, NULL, 0, 0, true, NULL, NULL));

        HttpHeader *header = httpHeaderNew(NULL);

        HttpQuery *query = httpQueryNew();
        httpQueryAdd(query, strNew("list-type"), strNew("2"));

        TEST_RESULT_VOID(
            storageS3Auth(
                driver, strNew("GET"), strNew("/"), query, strNew("20170606T121212Z"), header,
                strNew("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")),
            "generate authorization");
        TEST_RESULT_STR(
            strPtr(httpHeaderGet(header, strNew("authorization"))),
            "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20170606/us-east-1/s3/aws4_request,"
                "SignedHeaders=host;x-amz-content-sha256;x-amz-date,"
                "Signature=cb03bf1d575c1f8904dabf0e573990375340ab293ef7ad18d049fc1338fd89b3",
            "    check authorization header");

        // Test again to be sure cache signing key is used
        const Buffer *lastSigningKey = driver->signingKey;

        TEST_RESULT_VOID(
            storageS3Auth(
                driver, strNew("GET"), strNew("/"), query, strNew("20170606T121212Z"), header,
                strNew("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")),
            "generate authorization");
        TEST_RESULT_STR(
            strPtr(httpHeaderGet(header, strNew("authorization"))),
            "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20170606/us-east-1/s3/aws4_request,"
                "SignedHeaders=host;x-amz-content-sha256;x-amz-date,"
                "Signature=cb03bf1d575c1f8904dabf0e573990375340ab293ef7ad18d049fc1338fd89b3",
            "    check authorization header");
        TEST_RESULT_BOOL(driver->signingKey == lastSigningKey, true, "    check signing key was reused");

        // Change the date to generate a new signing key
        TEST_RESULT_VOID(
            storageS3Auth(
                driver, strNew("GET"), strNew("/"), query, strNew("20180814T080808Z"), header,
                strNew("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")),
            "    generate authorization");
        TEST_RESULT_STR(
            strPtr(httpHeaderGet(header, strNew("authorization"))),
            "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20180814/us-east-1/s3/aws4_request,"
                "SignedHeaders=host;x-amz-content-sha256;x-amz-date,"
                "Signature=d0fa9c36426eb94cdbaf287a7872c7a3b6c913f523163d0d7debba0758e36f49",
            "    check authorization header");
        TEST_RESULT_BOOL(driver->signingKey != lastSigningKey, true, "    check signing key was regenerated");

        // Test with security token
        // -------------------------------------------------------------------------------------------------------------------------
        driver = (StorageS3 *)storageDriver(
            storageS3New(
                path, true, NULL, bucket, endPoint, region, accessKey, secretAccessKey, securityToken, 16, 2, NULL, 0, 0, true,
                NULL, NULL));

        TEST_RESULT_VOID(
            storageS3Auth(
                driver, strNew("GET"), strNew("/"), query, strNew("20170606T121212Z"), header,
                strNew("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")),
            "generate authorization");
        TEST_RESULT_STR(
            strPtr(httpHeaderGet(header, strNew("authorization"))),
            "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20170606/us-east-1/s3/aws4_request,"
                "SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-security-token,"
                "Signature=c12565bf5d7e0ef623f76d66e09e5431aebef803f6a25a01c586525f17e474a3",
            "    check authorization header");
    }

    // *****************************************************************************************************************************
    if (testBegin("storageS3*(), StorageReadS3, and StorageWriteS3"))
    {
        testS3Server();

        Storage *s3 = storageS3New(
            path, true, NULL, bucket, endPoint, region, accessKey, secretAccessKey, NULL, 16, 2, host, port, 1000, true, NULL,
            NULL);

        // Coverage for noop functions
        // -------------------------------------------------------------------------------------------------------------------------
        TEST_RESULT_VOID(storagePathSyncNP(s3, strNew("path")), "path sync is a noop");

        // storageS3NewRead() and StorageS3FileRead
        // -------------------------------------------------------------------------------------------------------------------------
        TEST_RESULT_PTR(
            storageGetNP(storageNewReadP(s3, strNew("fi&le.txt"), .ignoreMissing = true)), NULL, "ignore missing file");
        TEST_ERROR(
            storageGetNP(storageNewReadNP(s3, strNew("file.txt"))), FileMissingError,
            "unable to open '/file.txt': No such file or directory");
        TEST_RESULT_STR(
            strPtr(strNewBuf(storageGetNP(storageNewReadNP(s3, strNew("file.txt"))))), "this is a sample file",
            "get file");
        TEST_RESULT_STR(strPtr(strNewBuf(storageGetNP(storageNewReadNP(s3, strNew("file0.txt"))))), "", "get zero-length file");

        StorageRead *read = NULL;
        TEST_ASSIGN(read, storageNewReadP(s3, strNew("file.txt"), .ignoreMissing = true), "new read file");
        TEST_RESULT_BOOL(storageReadIgnoreMissing(read), true, "    check ignore missing");
        TEST_RESULT_STR(strPtr(storageReadName(read)), "/file.txt", "    check name");

        TEST_ERROR(
            ioReadOpen(storageReadIo(read)), ProtocolError,
            "S3 request failed with 303: Some bad status\n"
            "*** URI/Query ***:\n"
            "/file.txt\n"
            "*** Request Headers ***:\n"
            "authorization: <redacted>\n"
            "content-length: 0\n"
            "host: " S3_TEST_HOST "\n"
            "x-amz-content-sha256: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n"
            "x-amz-date: <redacted>\n"
            "*** Response Headers ***:\n"
            "content-length: 7\n"
            "*** Response Content ***:\n"
            "CONTENT")

        // storageS3NewWrite() and StorageWriteS3
        // -------------------------------------------------------------------------------------------------------------------------
        // File is written all at once
        StorageWrite *write = NULL;
        TEST_ASSIGN(write, storageNewWriteNP(s3, strNew("file.txt")), "new write file");
        TEST_RESULT_VOID(storagePutNP(write, BUFSTRDEF("ABCD")), "put file all at once");

        TEST_RESULT_BOOL(storageWriteAtomic(write), true, "write is atomic");
        TEST_RESULT_BOOL(storageWriteCreatePath(write), true, "path will be created");
        TEST_RESULT_UINT(storageWriteModeFile(write), 0, "file mode is 0");
        TEST_RESULT_UINT(storageWriteModePath(write), 0, "path mode is 0");
        TEST_RESULT_STR(strPtr(storageWriteName(write)), "/file.txt", "check file name");
        TEST_RESULT_BOOL(storageWriteSyncFile(write), true, "file is synced");
        TEST_RESULT_BOOL(storageWriteSyncPath(write), true, "path is synced");

        TEST_RESULT_VOID(storageWriteS3Close((StorageWriteS3 *)storageWriteDriver(write)), "close file again");

        // Zero-length file
        TEST_ASSIGN(write, storageNewWriteNP(s3, strNew("file.txt")), "new write file");
        TEST_RESULT_VOID(storagePutNP(write, NULL), "write zero-length file");

        // File is written in chunks with nothing left over on close
        TEST_ASSIGN(write, storageNewWriteNP(s3, strNew("file.txt")), "new write file");
        TEST_RESULT_VOID(
            storagePutNP(write, BUFSTRDEF("12345678901234567890123456789012")),
            "write file in chunks -- nothing left on close");

        // File is written in chunks with something left over on close
        TEST_ASSIGN(write, storageNewWriteNP(s3, strNew("file.txt")), "new write file");
        TEST_RESULT_VOID(
            storagePutNP(write, BUFSTRDEF("12345678901234567890")),
            "write file in chunks -- something left on close");

        // storageDriverExists()
        // -------------------------------------------------------------------------------------------------------------------------
        TEST_RESULT_BOOL(storageExistsNP(s3, strNew("BOGUS")), false, "file does not exist");
        TEST_RESULT_BOOL(storageExistsNP(s3, strNew("subdir/file1.txt")), true, "file exists");

        // Info()
        // -------------------------------------------------------------------------------------------------------------------------
        TEST_RESULT_BOOL(storageInfoP(s3, strNew("BOGUS"), .ignoreMissing = true).exists, false, "file does not exist");

        StorageInfo info;
        TEST_ASSIGN(info, storageInfoNP(s3, strNew("subdir/file1.txt")), "file exists");
        TEST_RESULT_BOOL(info.exists, true, "    check exists");
        TEST_RESULT_UINT(info.size, 9999, "    check exists");

        // InfoList()
        // -------------------------------------------------------------------------------------------------------------------------
        TEST_ERROR(
            storageInfoListP(s3, strNew("/"), testStorageInfoListCallback, NULL, .errorOnMissing = true),
            AssertError, "assertion '!param.errorOnMissing || storageFeature(this, storageFeaturePath)' failed");

        TEST_RESULT_VOID(
            storageInfoListNP(s3, strNew("/path/to"), testStorageInfoListCallback, (void *)memContextCurrent()), "info list files");

        TEST_RESULT_UINT(testStorageInfoListSize, 2, "    file and path returned");
        TEST_RESULT_STR(strPtr(testStorageInfoList[0].name), "test_path", "    check name");
        TEST_RESULT_UINT(testStorageInfoList[0].size, 0, "    check size");
        TEST_RESULT_UINT(testStorageInfoList[0].type, storageTypePath, "    check type");
        TEST_RESULT_STR(strPtr(testStorageInfoList[1].name), "test_file", "    check name");
        TEST_RESULT_UINT(testStorageInfoList[1].size, 787, "    check size");
        TEST_RESULT_UINT(testStorageInfoList[1].type, storageTypeFile, "    check type");

        // storageDriverList()
        // -------------------------------------------------------------------------------------------------------------------------
        TEST_ERROR(
            storageListP(s3, strNew("/"), .errorOnMissing = true), AssertError,
            "assertion '!param.errorOnMissing || storageFeature(this, storageFeaturePath)' failed");
        TEST_ERROR(storageListNP(s3, strNew("/")), ProtocolError,
            "S3 request failed with 344: Another bad status\n"
            "*** URI/Query ***:\n"
            "/?delimiter=%2F&list-type=2\n"
            "*** Request Headers ***:\n"
            "authorization: <redacted>\n"
            "content-length: 0\n"
            "host: " S3_TEST_HOST "\n"
            "x-amz-content-sha256: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n"
            "x-amz-date: <redacted>");

        TEST_RESULT_STR(strPtr(strLstJoin(storageListNP(s3, strNew("/")), ",")), "path1,test1.txt", "list a file/path in root");
        TEST_RESULT_STR(
            strPtr(strLstJoin(storageListP(s3, strNew("/"), .expression = strNew("^test.*$")), ",")), "test1.txt",
            "list a file in root with expression");
        TEST_RESULT_STR(
            strPtr(strLstJoin(storageListNP(s3, strNew("/path/to")), ",")),
            "path1,test1.txt,test2.txt,path2,test3.txt", "list files with continuation");
        TEST_RESULT_STR(
            strPtr(strLstJoin(storageListP(s3, strNew("/path/to"), .expression = strNew("^test(1|3)")), ",")),
            "test1.path,test1.txt,test3.txt", "list files with expression");

        // storageDriverPathRemove()
        // -------------------------------------------------------------------------------------------------------------------------
        TEST_ERROR(
            storagePathRemoveNP(s3, strNew("/")), AssertError,
            "assertion 'param.recurse || storageFeature(this, storageFeaturePath)' failed");
        TEST_RESULT_VOID(storagePathRemoveP(s3, strNew("/"), .recurse = true), "remove root path");
        TEST_RESULT_VOID(storagePathRemoveP(s3, strNew("/path"), .recurse = true), "nothing to do in empty subpath");
        TEST_RESULT_VOID(storagePathRemoveP(s3, strNew("/path/to"), .recurse = true), "delete with continuation");
        TEST_ERROR(
            storagePathRemoveP(s3, strNew("/path"), .recurse = true), FileRemoveError,
            "unable to remove file 'sample2.txt': [AccessDenied] Access Denied");

        // storageDriverRemove()
        // -------------------------------------------------------------------------------------------------------------------------
        TEST_RESULT_VOID(storageRemoveNP(s3, strNew("/path/to/test.txt")), "remove file");
    }

    FUNCTION_HARNESS_RESULT_VOID();
}
