/**
 * @file cerver/tls/cerver.h
 * @brief TLS of a cerver: its settings, its reload without a restart and its stats.
 */

#ifndef _CERVER_TLS_CERVER_H_
#define _CERVER_TLS_CERVER_H_

#include <stdbool.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/tls/tls.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;

/**
 * @defgroup tls_cerver Cerver
 * @ingroup tls
 * @brief TLS for every TCP cerver: CUSTOM, GAME, WEB, FILES and BALANCER.
 *
 * A cerver with TLS only listens TLS on its port. The handshake of a web cerver runs in the thread of its thpool
 * that receives the connection; the one of any other cerver in a pool of its own, before its info packet is sent.
 * When a web cerver is full, a connection is closed without its handshake.
 * @{
 */

/**
 * @brief Sets the TLS of a cerver; the cerver takes the settings.
 *
 * The certificates, the keys and the settings are read and validated in cerver_start (), and an error stops it:
 * a certificate that expired, a key that does not match, a chain out of order or a name in two certificates.
 *
 * @param[in,out] cerver a TCP cerver that has not started
 * @param[in] settings the settings, owned by the cerver even when it fails
 * @retval 0 the cerver uses TLS
 * @retval 1 a NULL argument, an UDP cerver, one with CERVER_HANDLER_TYPE_EVENTS (it has TLS with F8b), or no memory
 * @since 2.1b-22
 */
CERVER_EXPORT u8 cerver_set_tls (struct _Cerver *cerver, TlsSettings *settings);

/**
 * @brief Returns whether a cerver uses TLS.
 *
 * @param[in] cerver the cerver
 * @return true when it has TLS
 * @since 2.1b-22
 */
CERVER_EXPORT bool cerver_has_tls (const struct _Cerver *cerver);

/**
 * @brief Reads and validates the certificates and the settings again; the new connections use them, and the ones
 * that are alive end with the previous ones. When they fail, the previous ones are kept and the reason is logged.
 *
 * @param[in,out] cerver a cerver with TLS that is running
 * @retval 0 the new certificates are in use
 * @retval 1 a cerver without TLS, or the new ones failed
 * @threadsafety Safe from any thread.
 * @since 2.1b-22
 */
CERVER_EXPORT u8 cerver_tls_reload (struct _Cerver *cerver);

/**
 * @brief Reloads the certificates when the process gets a signal, off by default.
 *
 * The signal only marks the reload, that the thread of the TLS of the cerver does; the threads of cerver block it.
 *
 * @param[in,out] cerver a cerver with TLS that has not started
 * @param[in] signal_number the signal, like SIGHUP; 0 turns it off
 * @since 2.1b-22
 */
CERVER_EXPORT void cerver_tls_set_reload_signal (struct _Cerver *cerver, const int signal_number);

/**
 * @brief Reloads the certificates when their files change (inotify, Linux), off by default.
 *
 * The folders of the files are watched, because a renewal replaces them with rename (); the reload waits until no
 * file changed for the debounce.
 *
 * @param[in,out] cerver a cerver with TLS that has not started
 * @param[in] enable true to watch
 * @param[in] debounce_milliseconds the wait, 0 uses #TLS_RELOAD_DEBOUNCE_MILLISECONDS_DEFAULT
 * @since 2.1b-22
 */
CERVER_EXPORT void cerver_tls_set_reload_watch (
	struct _Cerver *cerver, const bool enable, const unsigned int debounce_milliseconds
);

/**
 * @brief Copies the stats of TLS of a cerver.
 *
 * @param[in] cerver a cerver with TLS
 * @param[out] stats where they are copied
 * @retval 0 copied
 * @retval 1 a NULL argument or a cerver without TLS
 * @threadsafety Safe from any thread.
 * @since 2.1b-22
 */
CERVER_EXPORT u8 cerver_tls_get_stats (const struct _Cerver *cerver, TlsStats *stats);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
