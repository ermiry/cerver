/*
 * llhttp 9.4.3 (https://github.com/nodejs/llhttp), split and restyled for cerver
 * the errors, the flags and the lenient flags of the parser,
 * the methods are the ones of cerver/http/method.h and the statuses the ones of cerver/http/status.h
 *
 * This software is licensed under the MIT License.
 *
 * Copyright Fedor Indutny, 2018.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _CERVER_HTTP_PARSER_HELPERS_H_
#define _CERVER_HTTP_PARSER_HELPERS_H_

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_ERRNO_MAP(XX)															\
	XX(0, OK, OK)																	\
	XX(1, INTERNAL, INTERNAL)														\
	XX(2, STRICT, STRICT)															\
	XX(3, LF_EXPECTED, LF_EXPECTED)													\
	XX(4, UNEXPECTED_CONTENT_LENGTH, UNEXPECTED_CONTENT_LENGTH)						\
	XX(5, CLOSED_CONNECTION, CLOSED_CONNECTION)										\
	XX(6, INVALID_METHOD, INVALID_METHOD)											\
	XX(7, INVALID_URL, INVALID_URL)													\
	XX(8, INVALID_CONSTANT, INVALID_CONSTANT)										\
	XX(9, INVALID_VERSION, INVALID_VERSION)											\
	XX(10, INVALID_HEADER_TOKEN, INVALID_HEADER_TOKEN)								\
	XX(11, INVALID_CONTENT_LENGTH, INVALID_CONTENT_LENGTH)							\
	XX(12, INVALID_CHUNK_SIZE, INVALID_CHUNK_SIZE)									\
	XX(13, INVALID_STATUS, INVALID_STATUS)											\
	XX(14, INVALID_EOF_STATE, INVALID_EOF_STATE)									\
	XX(15, INVALID_TRANSFER_ENCODING, INVALID_TRANSFER_ENCODING)					\
	XX(16, CB_MESSAGE_BEGIN, CB_MESSAGE_BEGIN)										\
	XX(17, CB_HEADERS_COMPLETE, CB_HEADERS_COMPLETE)								\
	XX(18, CB_MESSAGE_COMPLETE, CB_MESSAGE_COMPLETE)								\
	XX(19, CB_CHUNK_HEADER, CB_CHUNK_HEADER)										\
	XX(20, CB_CHUNK_COMPLETE, CB_CHUNK_COMPLETE)									\
	XX(21, PAUSED, PAUSED)															\
	XX(22, PAUSED_UPGRADE, PAUSED_UPGRADE)											\
	XX(23, PAUSED_H2_UPGRADE, PAUSED_H2_UPGRADE)									\
	XX(24, USER, USER)																\
	XX(25, CR_EXPECTED, CR_EXPECTED)												\
	XX(26, CB_URL_COMPLETE, CB_URL_COMPLETE)										\
	XX(27, CB_STATUS_COMPLETE, CB_STATUS_COMPLETE)									\
	XX(28, CB_HEADER_FIELD_COMPLETE, CB_HEADER_FIELD_COMPLETE)						\
	XX(29, CB_HEADER_VALUE_COMPLETE, CB_HEADER_VALUE_COMPLETE)						\
	XX(30, UNEXPECTED_SPACE, UNEXPECTED_SPACE)										\
	XX(31, CB_RESET, CB_RESET)														\
	XX(32, CB_METHOD_COMPLETE, CB_METHOD_COMPLETE)									\
	XX(33, CB_VERSION_COMPLETE, CB_VERSION_COMPLETE)								\
	XX(34, CB_CHUNK_EXTENSION_NAME_COMPLETE, CB_CHUNK_EXTENSION_NAME_COMPLETE)		\
	XX(35, CB_CHUNK_EXTENSION_VALUE_COMPLETE, CB_CHUNK_EXTENSION_VALUE_COMPLETE)	\
	XX(38, CB_PROTOCOL_COMPLETE, CB_PROTOCOL_COMPLETE)

enum llhttp_errno {

	#define XX(num, name, string) HPE_##name = num,
	HTTP_ERRNO_MAP(XX)
	#undef XX

};

typedef enum llhttp_errno llhttp_errno_t;

enum llhttp_flags {

	F_CONNECTION_KEEP_ALIVE		= 0x1,
	F_CONNECTION_CLOSE			= 0x2,
	F_CONNECTION_UPGRADE		= 0x4,
	F_CHUNKED					= 0x8,
	F_UPGRADE					= 0x10,
	F_CONTENT_LENGTH			= 0x20,
	F_SKIPBODY					= 0x40,
	F_TRAILING					= 0x80,
	F_TRANSFER_ENCODING			= 0x200

};

typedef enum llhttp_flags llhttp_flags_t;

// what each lenient flag accepts, all of them are disabled by default (strict mode)
// the descriptions are stringified with #, they can not have commas
// the flag 0x400 of llhttp (header value relaxed) is left out: in 9.4.3 a NUL byte in a header value
// makes llhttp_execute () loop forever with it, so cerver never enables it
#define LLHTTP_LENIENT_HEADER_VALUE_RELAXED		0x400

#define LLHTTP_LENIENT_MAP(XX)																										\
	XX(0x1,		HEADERS,					headers,					invalid header values without checks)						\
	XX(0x2,		CHUNKED_LENGTH,				chunked_length,				Transfer-Encoding with Content-Length)						\
	XX(0x4,		KEEP_ALIVE,					keep_alive,					the data after a request with Connection close)				\
	XX(0x8,		TRANSFER_ENCODING,			transfer_encoding,			a value after chunked in Transfer-Encoding)					\
	XX(0x10,	VERSION,					version,					any HTTP version)											\
	XX(0x20,	DATA_AFTER_CLOSE,			data_after_close,			and discards the data after a request with Connection close)	\
	XX(0x40,	OPTIONAL_LF_AFTER_CR,		optional_lf_after_cr,		a CR without LF at the end of a line)						\
	XX(0x80,	OPTIONAL_CRLF_AFTER_CHUNK,	optional_crlf_after_chunk,	a chunk right after the previous one without CRLF)			\
	XX(0x100,	OPTIONAL_CR_BEFORE_LF,		optional_cr_before_lf,		a LF without CR at the end of a line)						\
	XX(0x200,	SPACES_AFTER_CHUNK_SIZE,	spaces_after_chunk_size,	spaces after the size of a chunk)

enum llhttp_lenient_flags {

	#define XX(num, name, string, description) LENIENT_##name = num,
	LLHTTP_LENIENT_MAP(XX)
	#undef XX

};

typedef enum llhttp_lenient_flags llhttp_lenient_flags_t;

enum llhttp_type {

	HTTP_BOTH		= 0,
	HTTP_REQUEST	= 1,
	HTTP_RESPONSE	= 2

};

typedef enum llhttp_type llhttp_type_t;

enum llhttp_finish {

	HTTP_FINISH_SAFE			= 0,
	HTTP_FINISH_SAFE_WITH_CB	= 1,
	HTTP_FINISH_UNSAFE			= 2

};

typedef enum llhttp_finish llhttp_finish_t;

#ifdef __cplusplus
}
#endif

#endif
