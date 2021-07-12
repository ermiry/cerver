#ifndef _CERVER_TESTS_HTTP_DATA_H_
#define _CERVER_TESTS_HTTP_DATA_H_

#define DECODED_DATA_VALUE_SIZE			256
#define CUSTOM_DATA_VALUE_SIZE			256

typedef struct DecodedData {

	int len;
	char value[DECODED_DATA_VALUE_SIZE];

} DecodedData;

typedef struct CustomData {

	int len;
	char value[CUSTOM_DATA_VALUE_SIZE];

} CustomData;

#endif