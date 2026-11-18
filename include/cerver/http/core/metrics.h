/**
 * @file cerver/http/core/metrics.h
 * @brief The metrics of a CerverHttp in the Prometheus text format.
 */

#ifndef _CERVER_HTTP_METRICS_H_
#define _CERVER_HTTP_METRICS_H_

#include <stddef.h>

#include "cerver/types/string.h"

#include "cerver/config.h"

/**
 * @defgroup http_metrics Metrics
 * @ingroup http_core
 * @brief The metrics of a CerverHttp, for Prometheus.
 * @{
 */

/** @brief The content type of the Prometheus text exposition format. */
#define HTTP_METRICS_CONTENT_TYPE			"text/plain; version=0.0.4; charset=utf-8"

/** @brief The initial memory of the metrics, it grows with the endpoints. */
#define HTTP_METRICS_INITIAL_SIZE			(1 << 12)

/** @brief The longest line of a metric: its name, its labels and its value. */
#define HTTP_METRICS_LINE_SIZE				(1 << 10)

/** @brief The longest value of a label after it is escaped. */
#define HTTP_METRICS_LABEL_SIZE				(1 << 9)

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpCerver;

/**
 * @brief Generates the metrics of the cerver in the Prometheus text format.
 *
 * They have its version and its uptime, the responses by status code, the rejected, aborted and vanished
 * requests, the threads of its pool, its WebSocket connections and, for every endpoint, its requests, its bytes and
 * the histogram of its process times.
 *
 * @param[in] http_cerver the CerverHttp
 * @return a new string that must be deleted with str_delete (), empty if its memory could not be reserved; NULL for
 * a NULL CerverHttp or if the string could not be allocated
 */
CERVER_PUBLIC String *cerver_http_metrics_generate (
	const struct _HttpCerver *http_cerver
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
