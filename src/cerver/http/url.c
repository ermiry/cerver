#include <stdlib.h>
#include <string.h>

#include <ctype.h>

static const char hex[] = "0123456789abcdef";

// converts a hex character to its integer value
static const char from_hex (const char ch) {

	return isdigit (ch) ? ch - '0' : tolower (ch) - 'a' + 10;

}

// converts an integer value to its hex character
static const char to_hex (const char code) {

	return hex[code & 15];

}

// returns a newly allocated url-encoded version of str that should be deleted after use
char *http_url_encode (const char *str) {

	char *pstr = (char *) str, *buf = (char *) malloc (strlen (str) * 3 + 1), *pbuf = buf;

	if (buf) {
		while (*pstr) {
			if (isalnum (*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~')
				*pbuf++ = *pstr;

			else if (*pstr == ' ') *pbuf++ = '+';

			else *pbuf++ = '%', *pbuf++ = to_hex (*pstr >> 4), *pbuf++ = to_hex (*pstr & 15);

			pstr++;
		}

		*pbuf = '\0';
	}

	return buf;

}

// returns a newly allocated url-decoded version of str that should be deleted after use
char *http_url_decode (const char *str) {

	char *pstr = (char *) str, *buf = (char *) malloc (strlen (str) + 1), *pbuf = buf;

	if (buf) {
		while (*pstr) {
			if (*pstr == '%') {
				if (pstr[1] && pstr[2]) {
					*pbuf++ = from_hex (pstr[1]) << 4 | from_hex (pstr[2]);
					pstr += 2;
				}
			}

			else if (*pstr == '+') *pbuf++ = ' ';

			else *pbuf++ = *pstr;

			pstr++;
		}

		*pbuf = '\0';
	}

	return buf;

}
