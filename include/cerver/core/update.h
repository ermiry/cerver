#ifndef _CERVER_CORE_UPDATE_H_
#define _CERVER_CORE_UPDATE_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region update

// aux structure for cerver update methods
struct _CerverUpdate {

	Cerver *cerver;
	void *args;

};

typedef struct _CerverUpdate CerverUpdate;

CERVER_PRIVATE CerverUpdate *cerver_update_new (
	Cerver *cerver, void *args
);

CERVER_PUBLIC void cerver_update_delete (void *cerver_update_ptr);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
