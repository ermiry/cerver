#include "cerver/http/method.h"

static const char *http_method_none = { "NONE" };

const char *method_strings[] = {

	#define XX(num, name, string) #string,
	HTTP_METHOD_MAP(XX)
	#undef XX

};

const char *http_method_str (const enum http_method m) {

	switch (m) {
		#define XX(num, name, string) case HTTP_##name: return #string;
		HTTP_METHOD_MAP(XX)
		#undef XX
	}

	return http_method_none;

}