#include "cerver/http/method.h"

#include "cerver/config.h"

const char *method_strings[] = {

	#define XX(num, name, string) #string,
	HTTP_METHOD_MAP(XX)
	#undef XX

};

const char *http_method_str (enum http_method m) {

  return ELEM_AT (method_strings, m, "<unknown>");

}