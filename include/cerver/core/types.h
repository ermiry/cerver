#ifndef _CERVER_CORE_TYPES_H_
#define _CERVER_CORE_TYPES_H_

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region global

// initializes global cerver values
// should be called only once at the start of the program
CERVER_EXPORT void cerver_init (void);

// correctly disposes global values
// should be called only once at the very end of the program
CERVER_EXPORT void cerver_end (void);

#pragma endregion

#pragma region types

#define CERVER_TYPE_MAP(XX)					\
	XX(0,	NONE, 		None)				\
	XX(1,	CUSTOM, 	Custom)				\
	XX(2,	GAME, 		Game)				\
	XX(3,	WEB, 		Web)				\
	XX(4,	FILES, 		Files)				\
	XX(5,	BALANCER, 	Balancer)

typedef enum CerverType {

	#define XX(num, name, string) CERVER_TYPE_##name = num,
	CERVER_TYPE_MAP (XX)
	#undef XX

} CerverType;

CERVER_EXPORT const char *cerver_type_to_string (CerverType type);

#define CERVER_HANDLER_TYPE_MAP(XX)																\
	XX(0,	NONE, 		None, 		None)														\
	XX(1,	POLL, 		Poll, 		Handle connections using a single thread & poll ())			\
	XX(2,	THREADS, 	Threads, 	Handle each new connection in a dedicated thread)

typedef enum CerverHandlerType {

	#define XX(num, name, string, description) CERVER_HANDLER_TYPE_##name = num,
	CERVER_HANDLER_TYPE_MAP (XX)
	#undef XX

} CerverHandlerType;

CERVER_EXPORT const char *cerver_handler_type_to_string (
	CerverHandlerType type
);

CERVER_EXPORT const char *cerver_handler_type_description (
	CerverHandlerType type
);

// what happens with a new connection when the thpool queue is full
#define CERVER_OVERLOAD_POLICY_MAP(XX)																	\
	XX(0,	NONE, 		None, 		None)																\
	XX(1,	WAIT, 		Wait, 		Stop accepting until the thpool has space for the connection)		\
	XX(2,	REJECT, 	Reject, 	Reject the connection right away (503 in web cervers))

typedef enum CerverOverloadPolicy {

	#define XX(num, name, string, description) CERVER_OVERLOAD_POLICY_##name = num,
	CERVER_OVERLOAD_POLICY_MAP (XX)
	#undef XX

	CERVER_OVERLOAD_POLICY_MAX

} CerverOverloadPolicy;

CERVER_EXPORT const char *cerver_overload_policy_to_string (
	CerverOverloadPolicy policy
);

CERVER_EXPORT const char *cerver_overload_policy_description (
	CerverOverloadPolicy policy
);

// what happens with the queued connections when the cerver is drained
#define CERVER_DRAIN_POLICY_MAP(XX)																		\
	XX(0,	NONE, 		None, 		None)																\
	XX(1,	RUN, 		Run, 		Handle the queued connections until the drain timeout)				\
	XX(2,	REJECT, 	Reject, 	Reject the queued connections right away (503 in web cervers))

typedef enum CerverDrainPolicy {

	#define XX(num, name, string, description) CERVER_DRAIN_POLICY_##name = num,
	CERVER_DRAIN_POLICY_MAP (XX)
	#undef XX

	CERVER_DRAIN_POLICY_MAX

} CerverDrainPolicy;

CERVER_EXPORT const char *cerver_drain_policy_to_string (
	CerverDrainPolicy policy
);

CERVER_EXPORT const char *cerver_drain_policy_description (
	CerverDrainPolicy policy
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
