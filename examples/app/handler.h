#ifndef _EXAMPLE_APP_HANDLER_H_
#define _EXAMPLE_APP_HANDLER_H_

#include <cerver/packets.h>

extern void send_test_message (const Packet *packet);

extern void app_handler (void *packet_ptr);

#endif