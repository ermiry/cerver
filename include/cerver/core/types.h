/**
 * @file cerver/core/types.h
 * @brief The global setup of cerver and the types and the policies of a cerver.
 */

#ifndef _CERVER_CORE_TYPES_H_
#define _CERVER_CORE_TYPES_H_

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup core_global Global
 * @ingroup core
 * @brief The values of cerver that the whole process shares.
 * @{
 */

/**
 * @brief Initializes the global values of cerver: the log.
 *
 * @note Call it only once, at the start of the program, before any other function of cerver.
 */
CERVER_EXPORT void cerver_init (void);

/**
 * @brief Releases the global values of cerver: the log.
 *
 * @note Call it only once, at the very end of the program, after every cerver has been torn down.
 */
CERVER_EXPORT void cerver_end (void);

/** @} */

/**
 * @defgroup core_types Types
 * @ingroup core
 * @brief The type of a cerver, how it handles its connections and what it does when it is full or drained.
 * @{
 */

/**
 * @brief The X-macro table of the types of a cerver: the number, the name and the string of each one.
 */
#define CERVER_TYPE_MAP(XX)					\
	XX(0,	NONE, 		None)				\
	XX(1,	CUSTOM, 	Custom)				\
	XX(2,	GAME, 		Game)				\
	XX(3,	WEB, 		Web)				\
	XX(4,	FILES, 		Files)				\
	XX(5,	BALANCER, 	Balancer)

/**
 * @brief The type of a cerver, one `CERVER_TYPE_<name>` for each row of #CERVER_TYPE_MAP.
 *
 * The type selects the data that cerver_create () attaches to the cerver (a game, the files cerver, ...).
 */
typedef enum CerverType {

	#define XX(num, name, string) CERVER_TYPE_##name = num,
	CERVER_TYPE_MAP (XX)
	#undef XX

} CerverType;

/**
 * @brief Returns the string of a type of cerver.
 *
 * @param[in] type the type
 * @return the string of its row in #CERVER_TYPE_MAP, or the one of CERVER_TYPE_NONE for a value out of the table;
 * a static string that must not be freed
 */
CERVER_EXPORT const char *cerver_type_to_string (CerverType type);

/**
 * @brief The X-macro table of the handler types: the number, the name, the string and the description of each one.
 */
#define CERVER_HANDLER_TYPE_MAP(XX)																\
	XX(0,	NONE, 		None, 		None)														\
	XX(1,	POLL, 		Poll, 		Handle connections using a single thread & poll ())			\
	XX(2,	THREADS, 	Threads, 	Handle each new connection in a dedicated thread)

/**
 * @brief How a cerver handles its connections, one `CERVER_HANDLER_TYPE_<name>` for each row of
 * #CERVER_HANDLER_TYPE_MAP.
 */
typedef enum CerverHandlerType {

	#define XX(num, name, string, description) CERVER_HANDLER_TYPE_##name = num,
	CERVER_HANDLER_TYPE_MAP (XX)
	#undef XX

} CerverHandlerType;

/**
 * @brief Returns the string of a handler type.
 *
 * @param[in] type the handler type
 * @return the string of its row in #CERVER_HANDLER_TYPE_MAP, or the one of CERVER_HANDLER_TYPE_NONE for a value
 * out of the table; a static string that must not be freed
 */
CERVER_EXPORT const char *cerver_handler_type_to_string (
	CerverHandlerType type
);

/**
 * @brief Returns the description of a handler type.
 *
 * @param[in] type the handler type
 * @return the description of its row in #CERVER_HANDLER_TYPE_MAP, or the one of CERVER_HANDLER_TYPE_NONE for a
 * value out of the table; a static string that must not be freed
 */
CERVER_EXPORT const char *cerver_handler_type_description (
	CerverHandlerType type
);

/**
 * @brief The X-macro table of what happens with a new connection when the queue of the thpool is full: the
 * number, the name, the string and the description of each policy.
 */
#define CERVER_OVERLOAD_POLICY_MAP(XX)																	\
	XX(0,	NONE, 		None, 		None)																\
	XX(1,	WAIT, 		Wait, 		Stop accepting until the thpool has space for the connection)		\
	XX(2,	REJECT, 	Reject, 	Reject the connection right away (503 in web cervers))

/**
 * @brief What happens with a new connection when the queue of the thpool is full, one
 * `CERVER_OVERLOAD_POLICY_<name>` for each row of #CERVER_OVERLOAD_POLICY_MAP.
 */
typedef enum CerverOverloadPolicy {

	#define XX(num, name, string, description) CERVER_OVERLOAD_POLICY_##name = num,
	CERVER_OVERLOAD_POLICY_MAP (XX)
	#undef XX

	/** @brief The number of policies, the first value that is not valid. */
	CERVER_OVERLOAD_POLICY_MAX

} CerverOverloadPolicy;

/**
 * @brief Returns the string of an overload policy.
 *
 * @param[in] policy the policy
 * @return the string of its row in #CERVER_OVERLOAD_POLICY_MAP, or the one of CERVER_OVERLOAD_POLICY_NONE for a
 * value out of the table; a static string that must not be freed
 */
CERVER_EXPORT const char *cerver_overload_policy_to_string (
	CerverOverloadPolicy policy
);

/**
 * @brief Returns the description of an overload policy.
 *
 * @param[in] policy the policy
 * @return the description of its row in #CERVER_OVERLOAD_POLICY_MAP, or the one of CERVER_OVERLOAD_POLICY_NONE for
 * a value out of the table; a static string that must not be freed
 */
CERVER_EXPORT const char *cerver_overload_policy_description (
	CerverOverloadPolicy policy
);

/**
 * @brief The X-macro table of what happens with the queued connections when the cerver is drained: the number,
 * the name, the string and the description of each policy.
 */
#define CERVER_DRAIN_POLICY_MAP(XX)																		\
	XX(0,	NONE, 		None, 		None)																\
	XX(1,	RUN, 		Run, 		Handle the queued connections until the drain timeout)				\
	XX(2,	REJECT, 	Reject, 	Reject the queued connections right away (503 in web cervers))

/**
 * @brief What happens with the queued connections when the cerver is drained, one `CERVER_DRAIN_POLICY_<name>`
 * for each row of #CERVER_DRAIN_POLICY_MAP.
 */
typedef enum CerverDrainPolicy {

	#define XX(num, name, string, description) CERVER_DRAIN_POLICY_##name = num,
	CERVER_DRAIN_POLICY_MAP (XX)
	#undef XX

	/** @brief The number of policies, the first value that is not valid. */
	CERVER_DRAIN_POLICY_MAX

} CerverDrainPolicy;

/**
 * @brief Returns the string of a drain policy.
 *
 * @param[in] policy the policy
 * @return the string of its row in #CERVER_DRAIN_POLICY_MAP, or the one of CERVER_DRAIN_POLICY_NONE for a value
 * out of the table; a static string that must not be freed
 */
CERVER_EXPORT const char *cerver_drain_policy_to_string (
	CerverDrainPolicy policy
);

/**
 * @brief Returns the description of a drain policy.
 *
 * @param[in] policy the policy
 * @return the description of its row in #CERVER_DRAIN_POLICY_MAP, or the one of CERVER_DRAIN_POLICY_NONE for a
 * value out of the table; a static string that must not be freed
 */
CERVER_EXPORT const char *cerver_drain_policy_description (
	CerverDrainPolicy policy
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
