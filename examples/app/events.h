#ifndef _EXAMPLE_APP_EVENTS_H_
#define _EXAMPLE_APP_EVENTS_H_

#include <cerver/cerver.h>
#include <cerver/client.h>

// #97: registers every cerver event that the example did not register,
// each one is logged with its description and its data is released
extern void app_cerver_events_register (Cerver *cerver);

// registers every client event that the example did not register
extern void app_client_events_register (Client *client);

#endif
