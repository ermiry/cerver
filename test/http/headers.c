#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/headers.h>

#include "../test.h"

static const char *HEADER_ACCEPT = { "Accept" };
static const char *HEADER_ACCEPT_CHARSET = { "Accept-Charset" };
static const char *HEADER_ACCEPT_ENCODING = { "Accept-Encoding" };
static const char *HEADER_ACCEPT_LANGUAGE = { "Accept-Language" };

static const char *HEADER_ACCESS_CONTROL_REQUEST_HEADERS = { "Access-Control-Request-Headers" };

static const char *HEADER_AUTHORIZATION = { "Authorization" };

static const char *HEADER_CACHE_CONTROL = { "Cache-Control" };

static const char *HEADER_CONNECTION = { "Connection" };

static const char *HEADER_CONTENT_LENGTH = { "Content-Length" };
static const char *HEADER_CONTENT_TYPE = { "Content-Type" };

static const char *HEADER_COOKIE = { "Cookie" };

static const char *HEADER_DATE = { "Date" };

static const char *HEADER_EXPECT = { "Expect" };

static const char *HEADER_HOST = { "Host" };

static const char *HEADER_ORIGIN = { "Origin" };

static const char *HEADER_PROXY_AUTHORIZATION = { "Proxy-Authorization" };

static const char *HEADER_UPGRADE = { "Upgrade" };

static const char *HEADER_USER_AGENT = { "User-Agent" };

static const char *HEADER_WEB_SOCKET_KEY = { "Sec-WebSocket-Key" };
static const char *HEADER_WEB_SOCKET_VERSION = { "Sec-WebSocket-Version" };

static const char *HEADER_OTHER = { "Undefined" };

static void test_http_headers_get_string (void) {

	test_check_str_eq (http_header_string (HTTP_HEADER_ACCEPT), HEADER_ACCEPT, NULL);
	test_check_str_eq (http_header_string (HTTP_HEADER_ACCEPT_CHARSET), HEADER_ACCEPT_CHARSET, NULL);
	test_check_str_eq (http_header_string (HTTP_HEADER_ACCEPT_ENCODING), HEADER_ACCEPT_ENCODING, NULL);
	test_check_str_eq (http_header_string (HTTP_HEADER_ACCEPT_LANGUAGE), HEADER_ACCEPT_LANGUAGE, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_ACCESS_CONTROL_REQUEST_HEADERS), HEADER_ACCESS_CONTROL_REQUEST_HEADERS, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_AUTHORIZATION), HEADER_AUTHORIZATION, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_CACHE_CONTROL), HEADER_CACHE_CONTROL, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_CONNECTION), HEADER_CONNECTION, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_CONTENT_LENGTH), HEADER_CONTENT_LENGTH, NULL);
	test_check_str_eq (http_header_string (HTTP_HEADER_CONTENT_TYPE), HEADER_CONTENT_TYPE, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_COOKIE), HEADER_COOKIE, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_DATE), HEADER_DATE, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_EXPECT), HEADER_EXPECT, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_HOST), HEADER_HOST, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_ORIGIN), HEADER_ORIGIN, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_PROXY_AUTHORIZATION), HEADER_PROXY_AUTHORIZATION, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_UPGRADE), HEADER_UPGRADE, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_USER_AGENT), HEADER_USER_AGENT, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_SEC_WEBSOCKET_KEY), HEADER_WEB_SOCKET_KEY, NULL);
	test_check_str_eq (http_header_string (HTTP_HEADER_SEC_WEBSOCKET_VERSION), HEADER_WEB_SOCKET_VERSION, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_INVALID), HEADER_OTHER, NULL);
	test_check_str_eq (http_header_string ((const HttpHeader) 73), HEADER_OTHER, NULL);
	test_check_str_eq (http_header_string ((const HttpHeader) 85), HEADER_OTHER, NULL);
	test_check_str_eq (http_header_string ((const HttpHeader) 100), HEADER_OTHER, NULL);
	test_check_str_eq (http_header_string ((const HttpHeader) 1024), HEADER_OTHER, NULL);

}

static void test_http_headers_get_type_by_string (void) {

	test_check_unsigned_eq (http_header_type_by_string (HEADER_ACCEPT), HTTP_HEADER_ACCEPT, NULL);
	test_check_unsigned_eq (http_header_type_by_string (HEADER_ACCEPT_CHARSET), HTTP_HEADER_ACCEPT_CHARSET, NULL);
	test_check_unsigned_eq (http_header_type_by_string (HEADER_ACCEPT_ENCODING), HTTP_HEADER_ACCEPT_ENCODING, NULL);
	test_check_unsigned_eq (http_header_type_by_string (HEADER_ACCEPT_LANGUAGE), HTTP_HEADER_ACCEPT_LANGUAGE, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_ACCESS_CONTROL_REQUEST_HEADERS), HTTP_HEADER_ACCESS_CONTROL_REQUEST_HEADERS, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_AUTHORIZATION), HTTP_HEADER_AUTHORIZATION, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_CACHE_CONTROL), HTTP_HEADER_CACHE_CONTROL, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_CONNECTION), HTTP_HEADER_CONNECTION, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_CONTENT_LENGTH), HTTP_HEADER_CONTENT_LENGTH, NULL);
	test_check_unsigned_eq (http_header_type_by_string (HEADER_CONTENT_TYPE), HTTP_HEADER_CONTENT_TYPE, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_COOKIE), HTTP_HEADER_COOKIE, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_DATE), HTTP_HEADER_DATE, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_EXPECT), HTTP_HEADER_EXPECT, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_HOST), HTTP_HEADER_HOST, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_ORIGIN), HTTP_HEADER_ORIGIN, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_PROXY_AUTHORIZATION), HTTP_HEADER_PROXY_AUTHORIZATION, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_UPGRADE), HTTP_HEADER_UPGRADE, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_USER_AGENT), HTTP_HEADER_USER_AGENT, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_WEB_SOCKET_KEY), HTTP_HEADER_SEC_WEBSOCKET_KEY, NULL);
	test_check_unsigned_eq (http_header_type_by_string (HEADER_WEB_SOCKET_VERSION), HTTP_HEADER_SEC_WEBSOCKET_VERSION, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_OTHER), HTTP_HEADER_INVALID, NULL);
	test_check_unsigned_eq (http_header_type_by_string ("hola"), HTTP_HEADER_INVALID, NULL);
	test_check_unsigned_eq (http_header_type_by_string ("adios"), HTTP_HEADER_INVALID, NULL);
	test_check_unsigned_eq (http_header_type_by_string ("erick"), HTTP_HEADER_INVALID, NULL);

}

void http_tests_headers (void) {

	(void) printf ("Testing HTTP headers...\n");

	test_http_headers_get_string ();

	test_http_headers_get_type_by_string ();

	(void) printf ("Done!\n");

}