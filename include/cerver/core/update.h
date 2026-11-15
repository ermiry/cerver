/**
 * @file cerver/core/update.h
 * @brief What the update methods of a cerver get.
 */

#ifndef _CERVER_CORE_UPDATE_H_
#define _CERVER_CORE_UPDATE_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup core_update Update
 * @ingroup core
 * @brief The argument of the methods of cerver_set_update () and cerver_set_update_interval ().
 * @{
 */

/**
 * @brief What an update method gets: the cerver and the arguments of the method.
 */
struct _CerverUpdate {

	/** @brief The cerver. */
	Cerver *cerver;
	/** @brief The arguments that were set with the method. */
	void *args;

};

/** @brief The argument of an update method, see struct _CerverUpdate. */
typedef struct _CerverUpdate CerverUpdate;

/**
 * @brief Allocates the argument of an update method.
 *
 * @param[in] cerver the cerver
 * @param[in] args the arguments of the method
 * @return the new argument, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE CerverUpdate *cerver_update_new (
	Cerver *cerver, void *args
);

/**
 * @brief Releases the argument of an update method, not its arguments.
 *
 * @param[in] cerver_update_ptr the argument, it can be NULL
 */
CERVER_PUBLIC void cerver_update_delete (void *cerver_update_ptr);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
