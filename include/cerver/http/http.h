#ifndef _CERVER_HTTP_H_
#define _CERVER_HTTP_H_

#include "cerver/handler.h"

extern void http_receive_handle (CerverReceive *cr, ssize_t rc, char *packet_buffer);

#endif