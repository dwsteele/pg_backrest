/***********************************************************************************************************************************
Test Tls Client
***********************************************************************************************************************************/
#include <unistd.h>

#include "common/time.h"

#include "common/harnessTls.h"

/***********************************************************************************************************************************
Test server with subject alternate names
***********************************************************************************************************************************/
static void
testTlsServerAltName(void)
{
    if (fork() == 0)
    {
        harnessTlsServerInit(
            harnessTlsTestPort(),
            strPtr(strNewFmt("%s/" TEST_CERTIFICATE_PREFIX "-alt-name.crt", testRepoPath())),
            strPtr(strNewFmt("%s/" TEST_CERTIFICATE_PREFIX ".key", testRepoPath())));

        // Certificate error on invalid ca path
        harnessTlsServerAccept();
        harnessTlsServerClose();

        if (testContainer())
        {
            // Success on valid ca file and match common name
            harnessTlsServerAccept();
            harnessTlsServerClose();

            // Success on valid ca file and match alt name
            harnessTlsServerAccept();
            harnessTlsServerClose();

            // Unable to find matching hostname in certificate
            harnessTlsServerAccept();
            harnessTlsServerClose();
        }

        // Certificate error
        harnessTlsServerAccept();
        harnessTlsServerClose();

        // Certificate ignored
        harnessTlsServerAccept();
        harnessTlsServerClose();

        exit(0);
    }
}

/***********************************************************************************************************************************
Test server
***********************************************************************************************************************************/
static void
testTlsServer(void)
{
    if (fork() == 0)
    {
        harnessTlsServerInitDefault();

        // First protocol exchange
        harnessTlsServerAccept();

        harnessTlsServerExpect("some protocol info");
        harnessTlsServerReply("something:0\n");

        sleepMSec(100);
        harnessTlsServerReply("some ");

        sleepMSec(100);
        harnessTlsServerReply("contentAND MORE");

        // This will cause the client to disconnect
        sleepMSec(500);

        // Second protocol exchange
        harnessTlsServerExpect("more protocol info");
        harnessTlsServerReply("0123456789AB");
        harnessTlsServerClose();

        // Need data in read buffer to test tlsWriteContinue()
        harnessTlsServerAccept();
        harnessTlsServerReply("0123456789AB");
        harnessTlsServerClose();

        exit(0);
    }
}

/***********************************************************************************************************************************
Test Run
***********************************************************************************************************************************/
void
testRun(void)
{
    FUNCTION_HARNESS_VOID();

    // Additional coverage not provided by testing with actual certificates
    // *****************************************************************************************************************************
    if (testBegin("asn1ToStr(), tlsClientHostVerify(), and tlsClientHostVerifyName()"))
    {
        TEST_ERROR(asn1ToStr(NULL), CryptoError, "TLS certificate name entry is missing");

        TEST_ERROR(
            tlsClientHostVerifyName(
                strNew("host"), strNewN("ab\0cd", 5)), CryptoError, "TLS certificate name contains embedded null");

        TEST_ERROR(tlsClientHostVerify(strNew("host"), NULL), CryptoError, "No certificate presented by the TLS server");

        TEST_RESULT_BOOL(tlsClientHostVerifyName(strNew("host"), strNew("**")), false, "invalid pattern");
        TEST_RESULT_BOOL(tlsClientHostVerifyName(strNew("host"), strNew("*.")), false, "invalid pattern");
        TEST_RESULT_BOOL(tlsClientHostVerifyName(strNew("a.bogus.host.com"), strNew("*.host.com")), false, "invalid host");
    }

    // Additional coverage not provided by other tests
    // *****************************************************************************************************************************
    if (testBegin("tlsError()"))
    {
        TlsClient *client = NULL;

        TEST_ASSIGN(client, tlsClientNew(strNew("99.99.99.99.99"), harnessTlsTestPort(), 0, true, NULL, NULL), "new client");

        TEST_RESULT_BOOL(tlsError(client, SSL_ERROR_WANT_READ), true, "continue after want read");
        TEST_RESULT_BOOL(tlsError(client, SSL_ERROR_ZERO_RETURN), false, "check connection closed error");
        TEST_ERROR(tlsError(client, SSL_ERROR_WANT_X509_LOOKUP), ServiceError, "tls error [4]");
    }

    // *****************************************************************************************************************************
    if (testBegin("TlsClient verification"))
    {
        TlsClient *client = NULL;

        // Connection errors
        // -------------------------------------------------------------------------------------------------------------------------
        TEST_ASSIGN(client, tlsClientNew(strNew("99.99.99.99.99"), harnessTlsTestPort(), 0, true, NULL, NULL), "new client");
        TEST_ERROR(
            tlsClientOpen(client), HostConnectError, "unable to get address for '99.99.99.99.99': [-2] Name or service not known");

        TEST_ASSIGN(client, tlsClientNew(strNew("localhost"), harnessTlsTestPort(), 100, true, NULL, NULL), "new client");
        TEST_ERROR_FMT(
            tlsClientOpen(client), HostConnectError, "unable to connect to 'localhost:%u': [111] Connection refused",
            harnessTlsTestPort());

        // Certificate location and validation errors
        // -------------------------------------------------------------------------------------------------------------------------
        // Add test hosts
        if (testContainer())
        {
            if (system(                                                                                 // {uncoverable_branch}
                    "echo \"127.0.0.1 test.pgbackrest.org host.test2.pgbackrest.org test3.pgbackrest.org\" |"
                        " sudo tee -a /etc/hosts > /dev/null") != 0)
            {
                THROW(AssertError, "unable to add test hosts to /etc/hosts");                           // {uncovered+}
            }
        }

        // Start server to test various certificate errors
        testTlsServerAltName();

        TEST_ERROR(
            tlsClientOpen(
                tlsClientNew(strNew("localhost"), harnessTlsTestPort(), 500, true, strNew("bogus.crt"), strNew("/bogus"))),
            CryptoError, "unable to set user-defined CA certificate location: [33558530] No such file or directory");
        TEST_ERROR_FMT(
            tlsClientOpen(tlsClientNew(strNew("localhost"), harnessTlsTestPort(), 500, true, NULL, strNew("/bogus"))),
            CryptoError, "unable to verify certificate presented by 'localhost:%u': [20] unable to get local issuer certificate",
            harnessTlsTestPort());

        if (testContainer())
        {
            TEST_RESULT_VOID(
                tlsClientOpen(
                    tlsClientNew(strNew("test.pgbackrest.org"), harnessTlsTestPort(), 500, true,
                    strNewFmt("%s/" TEST_CERTIFICATE_PREFIX "-ca.crt", testRepoPath()), NULL)),
                "success on valid ca file and match common name");
            TEST_RESULT_VOID(
                tlsClientOpen(
                    tlsClientNew(strNew("host.test2.pgbackrest.org"), harnessTlsTestPort(), 500, true,
                    strNewFmt("%s/" TEST_CERTIFICATE_PREFIX "-ca.crt", testRepoPath()), NULL)),
                "success on valid ca file and match alt name");
            TEST_ERROR(
                tlsClientOpen(
                    tlsClientNew(strNew("test3.pgbackrest.org"), harnessTlsTestPort(), 500, true,
                    strNewFmt("%s/" TEST_CERTIFICATE_PREFIX "-ca.crt", testRepoPath()), NULL)),
                CryptoError,
                "unable to find hostname 'test3.pgbackrest.org' in certificate common name or subject alternative names");
        }

        TEST_ERROR_FMT(
            tlsClientOpen(
                tlsClientNew(
                    strNew("localhost"), harnessTlsTestPort(), 500, true, strNewFmt("%s/" TEST_CERTIFICATE_PREFIX ".crt",
                    testRepoPath()),
                NULL)),
            CryptoError, "unable to verify certificate presented by 'localhost:%u': [20] unable to get local issuer certificate",
            harnessTlsTestPort());

        TEST_RESULT_VOID(
            tlsClientOpen(tlsClientNew(strNew("localhost"), harnessTlsTestPort(), 500, false, NULL, NULL)), "success on no verify");
    }
    // *****************************************************************************************************************************
    if (testBegin("TlsClient general usage"))
    {
        TlsClient *client = NULL;

        // Reset statistics
        tlsClientStatLocal = (TlsClientStat){0};
        TEST_RESULT_PTR(tlsClientStatStr(), NULL, "no stats yet");

        testTlsServer();
        ioBufferSizeSet(12);

        TEST_ASSIGN(
            client, tlsClientNew(harnessTlsTestHost(), harnessTlsTestPort(), 500, testContainer(), NULL, NULL), "new client");
        TEST_RESULT_VOID(tlsClientOpen(client), "open client");

        const Buffer *input = BUFSTRDEF("some protocol info");
        TEST_RESULT_VOID(ioWrite(tlsClientIoWrite(client), input), "write input");
        ioWriteFlush(tlsClientIoWrite(client));

        TEST_RESULT_STR_Z(ioReadLine(tlsClientIoRead(client)), "something:0", "read line");
        TEST_RESULT_BOOL(ioReadEof(tlsClientIoRead(client)), false, "    check eof = false");

        Buffer *output = bufNew(12);
        TEST_RESULT_UINT(ioRead(tlsClientIoRead(client), output), 12, "read output");
        TEST_RESULT_STR_Z(strNewBuf(output), "some content", "    check output");
        TEST_RESULT_BOOL(ioReadEof(tlsClientIoRead(client)), false, "    check eof = false");

        output = bufNew(8);
        TEST_RESULT_UINT(ioRead(tlsClientIoRead(client), output), 8, "read output");
        TEST_RESULT_STR_Z(strNewBuf(output), "AND MORE", "    check output");
        TEST_RESULT_BOOL(ioReadEof(tlsClientIoRead(client)), false, "    check eof = false");

        output = bufNew(12);
        TEST_ERROR_FMT(
            ioRead(tlsClientIoRead(client), output), FileReadError,
            "timeout after 500ms waiting for read from '%s:%u'", strPtr(harnessTlsTestHost()), harnessTlsTestPort());

        // -------------------------------------------------------------------------------------------------------------------------
        input = BUFSTRDEF("more protocol info");
        TEST_RESULT_VOID(tlsClientOpen(client), "open client again (it is already open)");
        TEST_RESULT_VOID(ioWrite(tlsClientIoWrite(client), input), "write input");
        ioWriteFlush(tlsClientIoWrite(client));

        output = bufNew(12);
        TEST_RESULT_UINT(ioRead(tlsClientIoRead(client), output), 12, "read output");
        TEST_RESULT_STR_Z(strNewBuf(output), "0123456789AB", "    check output");
        TEST_RESULT_BOOL(ioReadEof(tlsClientIoRead(client)), false, "    check eof = false");

        output = bufNew(12);
        TEST_RESULT_UINT(ioRead(tlsClientIoRead(client), output), 0, "read no output after eof");
        TEST_RESULT_BOOL(ioReadEof(tlsClientIoRead(client)), true, "    check eof = true");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_RESULT_VOID(tlsClientOpen(client), "open client again (was closed by server)");
        TEST_RESULT_BOOL(tlsWriteContinue(client, -1, SSL_ERROR_WANT_READ, 1), true, "continue on WANT_READ");
        TEST_RESULT_BOOL(tlsWriteContinue(client, 0, SSL_ERROR_NONE, 1), true, "continue on WANT_READ");
        TEST_ERROR(
            tlsWriteContinue(client, 77, 0, 88), FileWriteError,
            "unable to write to tls, write size 77 does not match expected size 88");
        TEST_ERROR(tlsWriteContinue(client, 0, SSL_ERROR_ZERO_RETURN, 1), FileWriteError, "unable to write to tls [6]");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_RESULT_BOOL(tlsClientStatStr() != NULL, true, "check statistics exist");

        TEST_RESULT_VOID(tlsClientFree(client), "free client");
    }

    FUNCTION_HARNESS_RESULT_VOID();
}
