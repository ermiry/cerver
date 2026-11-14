#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/http/requests/url.h"

static const char hex[] = "0123456789abcdef";

// the length of a percent escape, "%XX"
static const size_t url_escape_length = 3;

// converts a hex character to its integer value
static const char from_hex (const char ch) {

	return isdigit (ch) ? ch - '0' : tolower (ch) - 'a' + 10;

}

// converts an integer value to its hex character
static const char to_hex (const char code) {

	return hex[code & 15];

}

// returns a newly allocated url-encoded version of str that should be deleted after use,
// NULL for a NULL str
char *http_url_encode (const char *str) {

	char *buf = NULL;

	if (str) {
		buf = (char *) malloc (strlen (str) * url_escape_length + 1);
		if (buf) {
			char *pbuf = buf;

			// the bytes are unsigned: a byte over 0x7f is negative as a char
			for (const unsigned char *pstr = (const unsigned char *) str; *pstr; pstr++) {
				if (isalnum (*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~')
					*pbuf++ = (char) *pstr;

				else if (*pstr == ' ') *pbuf++ = '+';

				else *pbuf++ = '%', *pbuf++ = to_hex ((char) (*pstr >> 4)), *pbuf++ = to_hex ((char) (*pstr & 15));
			}

			*pbuf = '\0';
		}
	}

	return buf;

}

// percent-decodes length bytes of a path segment into destination,
// that has room for length bytes, a '+' is kept (it is a space only in a query)
// and an incomplete or invalid escape is copied as it is
// returns the length of the decoded value
size_t http_url_decode_path (
	char *destination, const char *source, const size_t length
) {

	size_t decoded = 0;

	size_t idx = 0;
	while (idx < length) {
		if (
			(source[idx] == '%')
			&& ((idx + url_escape_length) <= length)
			&& isxdigit ((unsigned char) source[idx + 1])
			&& isxdigit ((unsigned char) source[idx + 2])
		) {
			destination[decoded++] = (char) (
				(from_hex (source[idx + 1]) << 4) | from_hex (source[idx + 2])
			);

			idx += url_escape_length;
		}

		else {
			destination[decoded++] = source[idx++];
		}
	}

	return decoded;

}

// returns a newly allocated url-decoded version of str that should be deleted after use,
// NULL for a NULL str; an incomplete or invalid escape is copied as it is,
// like http_url_decode_path () does
char *http_url_decode (const char *str) {

	char *buf = NULL;

	if (str) {
		buf = (char *) malloc (strlen (str) + 1);
		if (buf) {
			char *pbuf = buf;

			const char *pstr = str;
			while (*pstr) {
				if (
					(*pstr == '%')
					&& isxdigit ((unsigned char) pstr[1])
					&& isxdigit ((unsigned char) pstr[2])
				) {
					*pbuf++ = (char) ((from_hex (pstr[1]) << 4) | from_hex (pstr[2]));
					pstr += url_escape_length;
				}

				else {
					*pbuf++ = (*pstr == '+') ? ' ' : *pstr;
					pstr++;
				}
			}

			*pbuf = '\0';
		}
	}

	return buf;

}
