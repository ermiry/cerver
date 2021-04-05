#ifndef _CERVER_HTTP_CONTENT_H_
#define _CERVER_HTTP_CONTENT_H_

#include <stdbool.h>

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_CONTENT_TYPE_MAP(XX)								\
	XX(0,  NONE,	undefined,	undefined)						\
	XX(1,  HTML,	html,		text/html; charset=UTF-8)		\
	XX(2,  CSS,		css,		text/css)						\
	XX(3,  JS,		js,			application/javascript)			\
	XX(4,  JSON,	json,		application/json)				\
	XX(5,  OCTET,	octet,		application/octet-stream)		\
	XX(6,  JPG,		jpg,		image/jpg)						\
	XX(7,  PNG,		png,		image/png)						\
	XX(8,  ICO,		ico,		image/x-icon)					\
	XX(9,  GIF,		gif,		image/gif)						\
	XX(10, MP3,		mp3,		audio/mp3)

typedef enum ContentType {

	#define XX(num, name, string, description) HTTP_CONTENT_TYPE_##name = num,
	HTTP_CONTENT_TYPE_MAP(XX)
	#undef XX

} ContentType;

CERVER_PUBLIC const char *http_content_type_string (
	const ContentType content_type
);

CERVER_PUBLIC const char *http_content_type_description (
	const ContentType content_type
);

CERVER_PUBLIC ContentType http_content_type_by_string (
	const char *content_type_string
);

CERVER_PUBLIC const char *http_content_type_by_extension (
	const char *extension
);

CERVER_PUBLIC bool http_content_type_is_json (
	const char *description
);

#ifdef __cplusplus
}
#endif

#endif