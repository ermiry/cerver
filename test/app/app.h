#ifndef _TEST_APP_H_
#define _TEST_APP_H_

#include <time.h>

#define APP_MESSAGE_LEN			512

#define APP_REQUEST_MAP(XX)					\
	XX(0,	NONE, 		None)				\
	XX(1,	TEST, 		Test)				\
	XX(2,	MESSAGE, 	Message)			\
	XX(3,	MULTI, 		Multi-Message)

typedef enum AppRequest {

	#define XX(num, name, string) APP_REQUEST_##name = num,
	APP_REQUEST_MAP (XX)
	#undef XX

} AppRequest;

extern const char *app_request_to_string (AppRequest type);

typedef struct AppMessage {

	time_t timestamp;

	size_t len;
	char message[APP_MESSAGE_LEN];

} AppMessage;

extern AppMessage *app_data_new (void);

extern void app_data_delete (void *app_data_ptr);

extern void app_message_create_internal (
	AppMessage *app_message, const char *message
);

extern AppMessage *app_data_create (const char *message);

extern void app_data_print (AppMessage *app_message);

#endif