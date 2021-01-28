#ifndef _CERVER_HTTP_CONTENT_H_
#define _CERVER_HTTP_CONTENT_H_

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_CONTENT_TYPE_MAP(XX)							\
	XX(0, HTML,		html,	text/html; charset=UTF-8)		\
	XX(1, CSS,		css,	text/css)						\
	XX(2, JS,		js,		application/javascript)			\
	XX(3, JSON,		json,	application/json)				\
	XX(4, OCTET,	octet,	application/octet-stream)		\
	XX(5, JPG,		jpg,	image/jpg)						\
	XX(6, PNG,		png,	image/png)						\
	XX(7, ICO,		ico,	image/x-icon)					\
	XX(8, GIF,		gif,	image/gif)						\
	XX(9, MP3,		mp3,	audio/mp3)

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

CERVER_PUBLIC const char *http_content_type_by_extension (
	const char *extension
);

#ifdef __cplusplus
}
#endif

#endif