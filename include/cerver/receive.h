#ifndef _CERVER_RECEIVE_H_
#define _CERVER_RECEIVE_H_

typedef enum ReceiveType {

    RECEIVE_TYPE_NONE            = 0,

    RECEIVE_TYPE_NORMAL          = 1,
    RECEIVE_TYPE_ON_HOLD         = 2,
    RECEIVE_TYPE_ADMIN           = 3,

} ReceiveType;

#endif