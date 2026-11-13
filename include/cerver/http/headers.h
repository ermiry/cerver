#ifndef _CERVER_HTTP_HEADERS_H_
#define _CERVER_HTTP_HEADERS_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#define HTTP_HEADERS_MAX				63
#define HTTP_HEADERS_SIZE				64

// the fields that a list of headers keeps inside it, more of them are allocated
#define HTTP_HEADERS_FIELDS_SIZE		32
#define HTTP_HEADERS_FIELDS_MAX			256

// the bytes of the lines that a list of headers keeps inside it, more of them are allocated
#define HTTP_HEADERS_BUFFER_SIZE		512
#define HTTP_HEADERS_BUFFER_MAX			16384

// the longest name of a header that is not one of the known ones
#define HTTP_HEADER_NAME_MAX			128

// the open addressing table that finds a header type by its name,
// a power of 2 bigger than twice the number of headers
#define HTTP_HEADERS_TABLE_SIZE			128

#define HTTP_HEADER_VALUE_SIZE			256

#define HTTP_HEADER_TEMP_SIZE			128
#define HTTP_HEADER_ACTUAL_SIZE			128

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_HEADERS_MAP(XX)																																																																	\
	/* Authentication */																																																																		\
	XX(0,	AUTHENTICATE,      					WWW-Authenticate,					(Defines the authentication method that should be used to access a resource.))																																\
	XX(1,	AUTHORIZATION,      				Authorization,						(Contains the credentials to authenticate a user-agent with a server.))																																		\
	XX(2,	PROXY_AUTHENTICATE,      			Proxy-Authenticate,					(Defines the authentication method that should be used to access a resource behind a proxy server.))																										\
	XX(3,	PROXY_AUTHORIZATION,      			Proxy-Authorization,				(Contains the credentials to authenticate a user agent with a proxy server.))																																\
	/* Caching */																																																																				\
	XX(4,	AGE,								Age,								(The time (in seconds) that the object has been in a proxy cache.))																																			\
	XX(5,	CACHE_CONTROL,						Cache-Control,						(Directives for caching mechanisms in both requests and responses.))																																		\
	XX(6,	CLEAR_SITE_DATA,					Clear-Site-Data,					(Clears browsing data (e.g. cookies, storage, cache) associated with the requesting website.))																												\
	XX(7,	EXPIRES,							Expires,							(The date/time after which the response is considered stale.))																																				\
	XX(8,	WARNING,							Warning,							(General warning information about possible problems.))																																						\
	/* Connection management */																																																																	\
	XX(9,	CONNECTION,							Connection,							(Controls whether the network connection stays open after the current transaction finishes.))																												\
	XX(10,  KEEP_ALIVE,							Keep-Alive,							(Controls how long a persistent connection should stay open.))																																				\
	/* Content negotiation */																																																																	\
	XX(11,  ACCEPT,								Accept,								(Informs the server about the types of data that can be sent back.))																																		\
	XX(12,  ACCEPT_CHARSET,						Accept-Charset,						(Which character encodings the client understands.))																																						\
	XX(13,  ACCEPT_ENCODING,					Accept-Encoding,					(The encoding algorithm, usually a compression algorithm, that can be used on the resource sent back.))																										\
	XX(14,  ACCEPT_LANGUAGE,					Accept-Language,					(Informs the server about the human language the server is expected to send back.))																															\
	/* Controls */																																																																				\
	XX(15,  EXPECT,								Expect,								(Indicates expectations that need to be fulfilled by the server to properly handle the request.))																											\
	/* Cookies */																																																																				\
	XX(16,  COOKIE,								Cookie,								(Contains stored HTTP cookies previously sent by the server with the Set-Cookie header.))																													\
	XX(17,  SET_COOKIE,							Set-Cookie,							(Send cookies from the server to the user-agent.))																																							\
	/* CORS */																																																																					\
	XX(18,  ACCESS_CONTROL_ALLOW_ORIGIN,		Access-Control-Allow-Origin,		(Indicates whether the response can be shared.))																																							\
	XX(19,  ACCESS_CONTROL_ALLOW_CREDENTIALS,	Access-Control-Allow-Credentials,	(Indicates whether the response to the request can be exposed when the credentials flag is true.))																											\
	XX(20,  ACCESS_CONTROL_ALLOW_HEADERS,		Access-Control-Allow-Headers, 		(Used in response to a preflight request to indicate which HTTP headers can be used when making the actual request.))																						\
	XX(21,  ACCESS_CONTROL_ALLOW_METHODS,		Access-Control-Allow-Methods, 		(Specifies the methods allowed when accessing the resource in response to a preflight request.))																											\
	XX(22,  ACCESS_CONTROL_EXPOSE_HEADER,		Access-Control-Expose-Headers, 		(Indicates which headers can be exposed as part of the response by listing their names.))																													\
	XX(23,  ACCESS_CONTROL_MAX_AGE,				Access-Control-Max-Age,				(Indicates how long the results of a preflight request can be cached.))																																		\
	XX(24,  ACCESS_CONTROL_REQUEST_HEADERS,		Access-Control-Request-Headers,		(Used when issuing a preflight request to let the server know which HTTP headers will be used when the actual request is made.))																			\
	XX(25,  ACCESS_CONTROL_REQUEST_METHOD,		Access-Control-Request-Method,		(Used when issuing a preflight request to let the server know which HTTP method will be used when the actual request is made.))																				\
	XX(26,  ORIGIN,								Origin, 							(Indicates where a fetch originates from.))																																									\
	XX(27,  TIMING_ALLOW_ORIGIN,				Timing-Allow-Origin,				(Specifies origins that are allowed to see values of attributes retrieved via features of the Resource Timing API.))																						\
	/* Downloads */																																																																				\
	XX(28,  CONTENT_DISPOSITION,				Content-Disposition,				(Indicates if the resource should be handled like a download and the browser should present a "Save As" dialog.))																							\
	/* Message body information */																																																																\
	XX(29,  CONTENT_LENGTH,						Content-Length,						(The size of the resource, in decimal number of bytes.))																																					\
	XX(30,  CONTENT_TYPE,						Content-Type,						(Indicates the media type of the resource.))																																								\
	XX(31,  CONTENT_ENCODING,					Content-Encoding,					(Used to specify the compression algorithm.))																																								\
	XX(32,  CONTENT_LANGUAGE,					Content-Language,					(Describes the human language(s) intended for the audience.))																																				\
	XX(33,  CONTENT_LOCATION,					Content-Location,					(Indicates an alternate location for the returned data.))																																					\
	/* Proxies */																																																																				\
	XX(34,  FORWARDED,							Forwarded,							(Contains information from the client-facing side of proxy servers that is altered or lost when a proxy is involved in the path of the request.))															\
	XX(35,  VIA,								Via,								(Added by proxies, both forward and reverse proxies, and can appear in the request headers and the response headers.))																						\
	/* Redirects */																																																																				\
	XX(36,  LOCATION,							Location,							(Indicates the URL to redirect a page to.))																																									\
	/* Request context */																																																																		\
	XX(37,  FROM,								From,								(Contains an Internet email address for a human user who controls the requesting user agent.))																												\
	XX(38,  HOST,								Host,								(Specifies the domain name of the server (for virtual hosting), and (optionally) the TCP port number on which the server is listening.))																	\
	XX(39,  REFERER,							Referer,							(The address of the previous web page from which a link to the currently requested page was followed.))																										\
	XX(40,  REFERRER_POLICY,					Referrer-Policy,					(Governs which referrer information sent in the Referer header should be included with requests made.))																										\
	XX(41,  USER_AGENT,							User-Agent,							(Contains a characteristic string that allows the network protocol peers to identify the application type, operating system, software vendor or software version of the requesting software user agent.))	\
	/* Response context */																																																																		\
	XX(42,  ALLOW,								Allow,								(Lists the set of HTTP request methods supported by a resource.))																																			\
	XX(43,  SERVER,								Server,								(Contains information about the software used by the origin server to handle the request.))																													\
	/* Range requests */																																																																		\
	XX(44,  ACCEPT_RANGES,						Accept-Ranges,						(Indicates if the server supports range requests, and if so in which unit the range can be expressed.))																										\
	XX(45,  RANGE,								Range,								(Indicates the part of a document that the server should return.))																																			\
	XX(46,  IF_RANGE,							If-Range,							(Creates a conditional range request that is only fulfilled if the given etag or date matches the remote resource. Used to prevent downloading two ranges from incompatible version of the resource.))		\
	XX(47,  CONTENT_RANGE,						Content-Range,						(Indicates where in a full body message a partial message belongs.))																																		\
	/* WebSockets */																																																																			\
	XX(48,  SEC_WEBSOCKET_KEY,					Sec-WebSocket-Key,					(No description))																																															\
	XX(49,  SEC_WEBSOCKET_EXTENSIONS,			Sec-WebSocket-Extensions,			(No description))																																															\
	XX(50,  SEC_WEBSOCKET_ACCEPT,				Sec-WebSocket-Accept,				(No description))																																															\
	XX(51,  SEC_WEBSOCKET_PROTOCOL,				Sec-WebSocket-Protocol,				(No description))																																															\
	XX(52,  SEC_WEBSOCKET_VERSION,				Sec-WebSocket-Version,				(No description))																																															\
	/* Other */																																																																					\
	XX(53,  DATE,								Date,								(Contains the date and time at which the message was originated.))																																			\
	XX(54,  UPGRADE,							Upgrade,							(The standard establishes rules for upgrading or changing to a different protocol on the current client, server, transport protocol connection.))															\
	/* Transfer coding */																																																																		\
	XX(55,  TRANSFER_ENCODING,					Transfer-Encoding,					(Specifies the form of encoding used to safely transfer the payload body to the user.))																														\
	/* Conditionals */																																																																			\
	XX(56,  IF_NONE_MATCH,						If-None-Match,						(Makes the request conditional, the resource is sent only if none of the given ETags match.))																												\
	XX(57,  IF_MODIFIED_SINCE,					If-Modified-Since,					(Makes the request conditional, the resource is sent only if it has been modified after the given date.))																									\
	XX(58,  ETAG,								ETag,								(A unique string identifying the version of the resource.))																																					\
	XX(59,  LAST_MODIFIED,						Last-Modified,						(The last modification date of the resource, used to compare several versions of the same resource.))																										\
	/* Tracing */																																																																				\
	XX(60,  REQUEST_ID,							X-Request-Id,						(Identifies a request between the client, the proxies and the servers.))																																	\
	XX(61,  FORWARDED_FOR,						X-Forwarded-For,					(Identifies the originating IP addresses of a client connecting through a proxy.))																															\
	XX(62,  REAL_IP,							X-Real-IP,							(The IP address of the client that a proxy received the request from.))																																		\
	XX(63,  UNDEFINED,							Undefined,							(Undefined Header))																																															\

typedef enum http_header {

	#define XX(num, name, string, description) HTTP_HEADER_##name = num,
	HTTP_HEADERS_MAP(XX)
	#undef XX

} http_header;

CERVER_PUBLIC const char *http_header_string (
	const http_header header
);

CERVER_PUBLIC const char *http_header_description (
	const http_header header
);

CERVER_PUBLIC const http_header http_header_type_by_string (
	const char *header_type_string
);

struct _HttpHeader {

	int len;
	char value[HTTP_HEADER_VALUE_SIZE];

};

typedef struct _HttpHeader HttpHeader;

CERVER_PRIVATE void http_header_reset (HttpHeader *header);

CERVER_PRIVATE void http_header_set (
	HttpHeader *header, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

CERVER_PUBLIC void http_header_print (
	const HttpHeader *header
);

// finds a header in a compiled header ("Name: value\r\n" lines after its status line),
// the name is compared without case and it stops at the empty line that ends the header
// returns true if it was found, and sets its value without the surrounding spaces
CERVER_PUBLIC bool http_headers_buffer_find (
	const char *buffer, const size_t length,
	const char *name, const size_t name_length,
	const char **value, size_t *value_length
);

// a header of a list, its line "Name: value\r\n" is in the buffer of the list
struct _HttpHeaderField {

	// HTTP_HEADER_UNDEFINED when its name is not one of the known headers
	http_header type;

	unsigned int offset;

	unsigned short name_length;
	unsigned int value_length;

};

typedef struct _HttpHeaderField HttpHeaderField;

// a list of headers in the order they were added, a name can be repeated,
// its lines are kept one after the other, so they are sent as they are in HTTP/1.1
// and a protocol that encodes its headers gets each name and value
struct _HttpHeaders {

	unsigned int count;

	// the allocated fields, NULL while they fit inside the list
	HttpHeaderField *fields;
	unsigned int fields_size;

	size_t length;

	// the allocated lines, NULL while they fit inside the list
	char *buffer;
	size_t buffer_size;

	HttpHeaderField inside_fields[HTTP_HEADERS_FIELDS_SIZE];
	char inside_buffer[HTTP_HEADERS_BUFFER_SIZE];

};

typedef struct _HttpHeaders HttpHeaders;

// a name must be a token (RFC 9110) of up to HTTP_HEADER_NAME_MAX bytes
CERVER_PRIVATE bool http_header_name_is_valid (
	const char *name, const size_t name_length
);

// a value can not have a CR, a LF or a NUL, as they would end its line
CERVER_PRIVATE bool http_header_value_is_valid (
	const char *value, const size_t value_length
);

CERVER_PRIVATE void http_headers_init (HttpHeaders *headers);

// removes all the headers and releases the allocated memory
CERVER_PRIVATE void http_headers_reset (HttpHeaders *headers);

// adds a header at the end of the list, the ones with the same type are kept
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_headers_add (
	HttpHeaders *headers, const http_header type,
	const char *value, const size_t value_length
);

// works like http_headers_add () with any name,
// the name of a known header is added with its type
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_headers_add_by_name (
	HttpHeaders *headers,
	const char *name, const size_t name_length,
	const char *value, const size_t value_length
);

// adds a header after removing the ones with the same type
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_headers_set (
	HttpHeaders *headers, const http_header type,
	const char *value, const size_t value_length
);

// works like http_headers_set () with any name
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_headers_set_by_name (
	HttpHeaders *headers,
	const char *name, const size_t name_length,
	const char *value, const size_t value_length
);

// returns the number of headers that were removed
CERVER_PRIVATE unsigned int http_headers_remove (
	HttpHeaders *headers, const http_header type
);

CERVER_PRIVATE unsigned int http_headers_remove_by_name (
	HttpHeaders *headers, const char *name, const size_t name_length
);

CERVER_PRIVATE unsigned int http_headers_count (
	const HttpHeaders *headers, const http_header type
);

CERVER_PRIVATE unsigned int http_headers_count_by_name (
	const HttpHeaders *headers, const char *name, const size_t name_length
);

// returns the header with the type in the position between the ones that have it
CERVER_PRIVATE const HttpHeaderField *http_headers_find (
	const HttpHeaders *headers, const http_header type, const unsigned int position
);

CERVER_PRIVATE const HttpHeaderField *http_headers_find_by_name (
	const HttpHeaders *headers,
	const char *name, const size_t name_length, const unsigned int position
);

// returns the header in the index of the list, NULL after the last one
CERVER_PRIVATE const HttpHeaderField *http_headers_get (
	const HttpHeaders *headers, const unsigned int index
);

// the name of a header of the list, it does not end with '\0'
CERVER_PRIVATE const char *http_headers_field_name (
	const HttpHeaders *headers, const HttpHeaderField *field, size_t *name_length
);

// the value of a header of the list, it does not end with '\0'
CERVER_PRIVATE const char *http_headers_field_value (
	const HttpHeaders *headers, const HttpHeaderField *field, size_t *value_length
);

// the line "Name: value\r\n" of a header of the list
CERVER_PRIVATE const char *http_headers_field_line (
	const HttpHeaders *headers, const HttpHeaderField *field, size_t *line_length
);

// the lines "Name: value\r\n" of all the headers, in the order they were added
CERVER_PRIVATE const char *http_headers_lines (
	const HttpHeaders *headers, size_t *length
);

#ifdef __cplusplus
}
#endif

#endif
