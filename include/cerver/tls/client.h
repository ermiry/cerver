/**
 * @file cerver/tls/client.h
 * @brief TLS of a client: every TCP connection of the client runs its handshake after its connect ().
 */

#ifndef _CERVER_TLS_CLIENT_H_
#define _CERVER_TLS_CLIENT_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/tls/tls.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Client;

/**
 * @defgroup tls_client Client
 * @ingroup tls
 * @brief TLS for the TCP connections of a client.
 *
 * The client asks for the host of each connection with SNI (or for the name of tls_settings_set_server_name ()),
 * and verifies the certificate of its cerver against the store of the system or the authorities of
 * tls_settings_set_ca (), with its name in the subject alternative name (RFC 9525); the verification is never off.
 * The sessions are kept by their destination and resumed in the next connections.
 * @{
 */

/**
 * @brief Sets the TLS of the TCP connections of a client; the client takes the settings.
 *
 * @param[in,out] client the client
 * @param[in] settings the settings, owned by the client even when it fails
 * @retval 0 the next connections use TLS
 * @retval 1 a NULL argument or no memory
 * @since 2.1b-22
 */
CERVER_EXPORT u8 client_set_tls (struct _Client *client, TlsSettings *settings);

/**
 * @brief Copies the stats of TLS of a client.
 *
 * @param[in] client a client with TLS
 * @param[out] stats where they are copied
 * @retval 0 copied
 * @retval 1 a NULL argument or a client without TLS
 * @threadsafety Safe from any thread.
 * @since 2.1b-22
 */
CERVER_EXPORT u8 client_tls_get_stats (const struct _Client *client, TlsStats *stats);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
