#ifndef _CERVER_CORE_REPORT_H_
#define _CERVER_CORE_REPORT_H_

#include <stdbool.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/types.h"
#include "cerver/network/network.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Client;
struct _Connection;

#pragma region report

// information that we get from another cerver when connecting to it
struct _CerverReport {

	CerverType type;

	String *name;
	String *welcome;

	bool use_ipv6;
	Protocol protocol;
	u16 port;

	bool auth_required;
	bool uses_sessions;

};

typedef struct _CerverReport CerverReport;

CERVER_PRIVATE void cerver_report_delete (void *ptr);

CERVER_PRIVATE u8 cerver_report_check_info (
	CerverReport *cerver_report,
	struct _Client *client, struct _Connection *connection
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
