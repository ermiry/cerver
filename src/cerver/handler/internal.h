/**
 * @file handler/internal.h
 * @internal
 * @brief What the events and the errors of the cerver share, never exported by libcerver.so.
 */

#ifndef _CERVER_HANDLER_INTERNAL_H_
#define _CERVER_HANDLER_INTERNAL_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @brief The arguments of an action, shared by its event and by the data of each trigger: the last one that releases
 * them deletes them, so an event that is replaced or unregistered while its action runs in a thread does not delete
 * the arguments that the action uses.
 */
struct _CerverActionArgs {

	/** @internal @brief The arguments of the action. */
	void *args;
	/** @internal @brief Releases the arguments, NULL to keep them. */
	Action delete_args;
	/** @internal @brief The event and the data of its triggers that point to them, read with __atomic. */
	unsigned int references;

};

/** @internal @brief The shared arguments of an action, see struct _CerverActionArgs. */
typedef struct _CerverActionArgs CerverActionArgs;

/**
 * @internal
 * @brief Creates the shared arguments of an action with one reference, the one of its event.
 *
 * @param[in] args the arguments, NULL creates nothing
 * @param[in] delete_args releases the arguments, it can be NULL
 * @return the shared arguments, NULL for NULL arguments or without memory
 */
CERVER_HIDDEN CerverActionArgs *cerver_action_args_create (void *args, Action delete_args);

/**
 * @internal
 * @brief Takes a reference to the shared arguments of an action.
 *
 * @param[in,out] action_args the shared arguments, it can be NULL
 * @return the same shared arguments
 */
CERVER_HIDDEN CerverActionArgs *cerver_action_args_reference (CerverActionArgs *action_args);

/**
 * @internal
 * @brief Releases a reference to the shared arguments of an action; the last one deletes the arguments.
 *
 * @param[in,out] action_args the shared arguments, it can be NULL
 */
CERVER_HIDDEN void cerver_action_args_release (CerverActionArgs *action_args);

#ifdef __cplusplus
}
#endif

#endif
