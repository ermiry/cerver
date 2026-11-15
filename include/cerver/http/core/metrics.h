#ifndef _CERVER_HTTP_METRICS_H_
#define _CERVER_HTTP_METRICS_H_

#include <stddef.h>

#include "cerver/types/string.h"

#include "cerver/config.h"

// the content type of the Prometheus text exposition format
#define HTTP_METRICS_CONTENT_TYPE			"text/plain; version=0.0.4; charset=utf-8"

// the initial memory of the metrics, it grows with the endpoints
#define HTTP_METRICS_INITIAL_SIZE			(1 << 12)

// the longest line of a metric, its name, its labels and its value
#define HTTP_METRICS_LINE_SIZE				(1 << 10)

// the longest label value after it is escaped
#define HTTP_METRICS_LABEL_SIZE				(1 << 9)

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpCerver;

// generates the metrics of the cerver in the Prometheus text format:
// its version and uptime, the responses by status code, the rejected requests,
// the threads of its pool and, for every endpoint, its requests, its bytes
// and the histogram of its process times
// returns a new string that must be deleted with str_delete (), NULL on error
CERVER_PUBLIC String *http_cerver_metrics_generate (
	const struct _HttpCerver *http_cerver
);

#ifdef __cplusplus
}
#endif

#endif
