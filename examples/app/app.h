#ifndef _EXAMPLE_APP_H_
#define _EXAMPLE_APP_H_

#include <time.h>

#define APP_MESSAGE_SIZE			512

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

extern const char *app_request_to_string (const AppRequest type);

typedef struct AppMessage {

	size_t id;

	size_t len;
	char message[APP_MESSAGE_SIZE];

} AppMessage;

extern AppMessage *app_message_new (void);

extern void app_message_delete (void *app_message_ptr);

extern void app_message_create_internal (
	AppMessage *app_message,
	const size_t id, const char *message
);

extern AppMessage *app_message_create (
	const size_t id, const char *message
);

extern void app_message_print (const AppMessage *app_message);

#endif