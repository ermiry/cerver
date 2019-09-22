#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "cerver/utils/base64.h"

static char base64_low6 (unsigned char c) {

	unsigned char tc = c & 0x3F;
	if (tc < 26) {
		return tc + 'A';
	} else if (tc < 52) {
		return tc - 26 + 'a';
	} else if (tc < 62) {
		return tc - 52 + '0';
	} else if (tc == 62) {
		return '+';
	} else {
		return '/';
	}
	
}

static unsigned char low6_base64(char c) {

	if ('A' <= c && c <= 'Z') {
		return c - 'A';
	} else if ('a' <= c && c <= 'z') {
		return 26 + c - 'a';
	} else if ('0' <= c && c <= '9') {
		return 52 + c - '0';
	} else if (c == '+') {
		return 62;
	} else if (c == '/') {
		return 63;
	} else {
		return 0xC0;
	}

}

char *base64_encode (size_t* enclen, size_t len, unsigned char* data) {

	unsigned short state = 0;
	size_t i = 0;
	size_t j = 0;
	char* result = (char*)malloc((4 * ceil(len / 3.0)) + 1);
	if (result == NULL) {
		*enclen = 0;
		return (char*)NULL;
	}

	for (i = 0; i < len; i++) {
		unsigned char c = data[i];
		unsigned char tc;
		switch (state) {
		case 0:
			result[j++] = base64_low6(c >> 2);
			state = (c & 0x03) + 1;
			break;
		case 1:
			result[j++] = (c >> 4) + 'A';
			state = (c & 0x0F) + 5;
			break;
		case 2:
			tc = c >> 4;
			if (tc < 10) {
				result[j++] = tc + 'Q';
			} else {
				result[j++] = tc - 10 + 'a';
			}
			state = (c & 0x0F) + 5;
			break;
		case 3:
			result[j++] = (c >> 4) + 'g';
			state = (c & 0x0F) + 5;
			break;
		case 4:
			tc = c >> 4;
			if (tc < 4) {
				result[j++] = tc + 'w';
			} else if (tc < 14) {
				result[j++] = tc - 4 + '0';
			} else if (tc == 14) {
				result[j++] = '+';
			} else {
				result[j++] = '/';
			}
			state = (c & 0x0F) + 5;
			break;
		case 5:
			result[j++] = (c >> 6) + 'A';
			result[j++] = base64_low6(c);
			state = 0;
			break;
		case 6:
			result[j++] = (c >> 6) + 'E';
			result[j++] = base64_low6(c);
			state = 0;
			break;
		case 7:
			result[j++] = (c >> 6) + 'I';
			result[j++] = base64_low6(c);
			state = 0;
			break;
		case 8:
			result[j++] = (c >> 6) + 'M';
			result[j++] = base64_low6(c);
			state = 0;
			break;
		case 9:
			result[j++] = (c >> 6) + 'Q';
			result[j++] = base64_low6(c);
			state = 0;
			break;
		case 10:
			result[j++] = (c >> 6) + 'U';
			result[j++] = base64_low6(c);
			state = 0;
			break;
		case 11:
			if (c < 0x80) {
				result[j++] = (c >> 6) + 'Y';
			} else {
				result[j++] = (c >> 6) - 2 + 'a';
			}
			result[j++] = base64_low6(c);
			state = 0;
			break;
		case 12:
			result[j++] = (c >> 6) + 'c';
			result[j++] = base64_low6(c);
			state = 0;
			break;
		case 13:
			result[j++] = (c >> 6) + 'g';
			result[j++] = base64_low6(c);
			state = 0;
			break;
		case 14:
			result[j++] = (c >> 6) + 'k';
			result[j++] = base64_low6(c);
			state = 0;
			break;
		case 15:
			result[j++] = (c >> 6) + 'o';
			result[j++] = base64_low6(c);
			state = 0;
			break;
		case 16:
			result[j++] = (c >> 6) + 's';
			result[j++] = base64_low6(c);
			state = 0;
			break;
		case 17:
			result[j++] = (c >> 6) + 'w';
			result[j++] = base64_low6(c);
			state = 0;
			break;
		case 18:
			result[j++] = (c >> 6) + '0';
			result[j++] = base64_low6(c);
			state = 0;
			break;
		case 19:
			result[j++] = (c >> 6) + '4';
			result[j++] = base64_low6(c);
			state = 0;
			break;
		case 20:
			if (c < 0x80) {
				result[j++] = (c >> 6) + '8';
			} else if (c >= 0xC0) {
				result[j++] = '/';
			} else {
				result[j++] = '+';
			}
			result[j++] = base64_low6(c);
			state = 0;
			break;
		}
	}
	switch (state) {
	case 1:
		result[j++] = 'A';
		result[j++] = '=';
		result[j++] = '=';
		break;
	case 2:
		result[j++] = 'Q';
		result[j++] = '=';
		result[j++] = '=';
		break;
	case 3:
		result[j++] = 'g';
		result[j++] = '=';
		result[j++] = '=';
		break;
	case 4:
		result[j++] = 'w';
		result[j++] = '=';
		result[j++] = '=';
		break;
	case 5:
		result[j++] = 'A';
		result[j++] = '=';
		break;
	case 6:
		result[j++] = 'E';
		result[j++] = '=';
		break;
	case 7:
		result[j++] = 'I';
		result[j++] = '=';
		break;
	case 8:
		result[j++] = 'M';
		result[j++] = '=';
		break;
	case 9:
		result[j++] = 'Q';
		result[j++] = '=';
		break;
	case 10:
		result[j++] = 'U';
		result[j++] = '=';
		break;
	case 11:
		result[j++] = 'Y';
		result[j++] = '=';
		break;
	case 12:
		result[j++] = 'c';
		result[j++] = '=';
		break;
	case 13:
		result[j++] = 'g';
		result[j++] = '=';
		break;
	case 14:
		result[j++] = 'k';
		result[j++] = '=';
		break;
	case 15:
		result[j++] = 'o';
		result[j++] = '=';
		break;
	case 16:
		result[j++] = 's';
		result[j++] = '=';
		break;
	case 17:
		result[j++] = 'w';
		result[j++] = '=';
		break;
	case 18:
		result[j++] = '0';
		result[j++] = '=';
		break;
	case 19:
		result[j++] = '4';
		result[j++] = '=';
		break;
	case 20:
		result[j++] = '8';
		result[j++] = '=';
		break;
	}

	result[j++] = '\0';
	*enclen = j;
	return result;
}

unsigned char *base64_decode (size_t* declen, size_t len, char* data) {

	size_t reallen = strnlen(data, len);
	size_t reslen;
	unsigned char* result;
	size_t i = 0;
	size_t j = 0;
	unsigned char prevchar = 0x00;

	if ((reallen % 4) != 0) {
		*declen = 0;
		return (unsigned char*)NULL;
	}

	reslen = (reallen / 4) * 3;

	if (data[reallen - 1] == '=') {
		reallen--;
		reslen--;
		if (data[reallen - 1] == '=') {
			reallen--;
			reslen--;
			if ((low6_base64(data[reallen - 1]) & 0x0F) != 0) {
				*declen = 0;
				return (unsigned char*)NULL;
			}
		} else if ((low6_base64(data[reallen - 1]) & 0x03) != 0) {
			*declen = 0;
			return (unsigned char*)NULL;
		}
	}

	result = (unsigned char*)malloc(reslen);
	if (result == NULL) {
		*declen = 0;
		return (unsigned char*)NULL;
	}

	for (i = 0; i < reallen; i++) {
		unsigned char c = low6_base64(data[i]);
		if (c > 0x3F) {
			*declen = 0;
			return (unsigned char*)NULL;
		}

		switch (i % 4) {
		case 0:
			prevchar = c;
			break;
		case 1:
			result[j++] = (prevchar << 2) | (c >> 4);
			prevchar = c;
			break;
		case 2:
			result[j++] = (prevchar << 4) | (c >> 2);
			prevchar = c;
			break;
		case 3:
			result[j++] = (prevchar << 6) | c;
			break;
		}
	}

	*declen = j;
	return result;

}