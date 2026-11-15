/**
 * @file cerver/network/udp.h
 * @brief The peers of an UDP cerver: the logical connections of the addresses that send it datagrams.
 */

#ifndef _CERVER_UDP_H_
#define _CERVER_UDP_H_

#include <stdbool.h>
#include <time.h>

#include <netinet/in.h>
#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"
#include "cerver/collections/htab.h"

#include "cerver/config.h"

#include "cerver/network/network.h"
#include "cerver/packets/packets.h"

/**
 * @defgroup network_udp UDP
 * @ingroup network
 * @brief The datagrams and the peers of an UDP cerver.
 * @{
 */

/** @brief The seconds without datagrams after which a peer of an UDP cerver is removed. */
#define CERVER_UDP_DEFAULT_PEER_TIMEOUT		16

/** @brief The peers that an UDP cerver keeps at the same time, a datagram of a new address is dropped after it. */
#define CERVER_UDP_DEFAULT_MAX_PEERS		1024

/** @brief How often, in seconds, the receive loop removes the inactive peers. */
#define CERVER_UDP_PEERS_CHECK_INTERVAL		1

/** @brief The size of the key of a peer in the peers of the cerver: its ip with its zone and its port. */
#define CERVER_UDP_PEER_KEY_SIZE			(NETWORK_ADDRESS_STRING_SIZE + 8)

/**
 * @brief A peer whose address is not proven (it did not authenticate) gets answers of the cerver of up to these
 * times the bytes that it sent, so a forged address can not be flooded.
 */
#define CERVER_UDP_AMPLIFICATION_FACTOR		4

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Connection;

/**
 * @brief What follows the packet of a datagram.
 */
typedef enum UdpDatagram {

	/** @brief It does not have a single packet of a valid size. */
	UDP_DATAGRAM_INVALID	= 0,
	/** @brief Only the packet. */
	UDP_DATAGRAM_PLAIN		= 1,
	/** @brief The packet and the token of an authenticated connection. */
	UDP_DATAGRAM_TOKEN		= 2

} UdpDatagram;

/**
 * @brief The state of a peer.
 */
typedef enum UdpPeerState {

	/** @brief The cerver requires authentication and the peer did not authenticate: it only sends auth, test and cerver info packets. */
	UDP_PEER_STATE_PENDING			= 0,

	/** @brief The cerver does not require authentication, the datagrams of the peer do not have a token. */
	UDP_PEER_STATE_OPEN				= 1,

	/** @brief Every datagram of the peer, and every answer, has its token after the packet. */
	UDP_PEER_STATE_AUTHENTICATED	= 2

} UdpPeerState;

/**
 * @brief The logical connection of an UDP cerver with the address of a peer.
 *
 * Every datagram of the address is a packet of its connection; the connection has no socket of its own, the cerver
 * answers with its socket. A peer is released with its last reference: the peers of the cerver hold one, and every
 * received packet that a handler has holds another one.
 */
typedef struct UdpPeer {

	/** @brief The ip with its zone and the port of the peer, its key in the peers of the cerver. */
	char key[CERVER_UDP_PEER_KEY_SIZE];

	/** @brief The cerver of the peer. */
	struct _Cerver *cerver;

	/**
	 * @brief The client of the peer: NULL while the peer is pending; with sessions it is shared with the other
	 * peers of the session, and it is released with its last connection.
	 */
	struct _Client *client;
	/** @brief The connection of the peer. */
	struct _Connection *connection;

	/** @brief The state of the peer. */
	UdpPeerState state;

	/** @brief When the peer was created. */
	time_t created;
	/** @brief When the peer sent its last datagram. */
	time_t last_activity;

	/** @brief The bytes that a peer whose address is not proven sent, to limit the answers to it. */
	size_t received_bytes;
	/** @brief The bytes that the cerver answered to a peer whose address is not proven. */
	size_t answered_bytes;

	/** @brief The references to the peer. */
	unsigned int references;

} UdpPeer;

/**
 * @brief The peers of an UDP cerver.
 *
 * Only its receive loop changes them; the lock protects the clients and their sessions, that the handlers can
 * release.
 */
typedef struct CerverUdp {

	/** @brief The peers. */
	DoubleList *peers;

	/** @brief The peer of an ip:port. */
	Htab *addresses;
	/** @brief The authenticated peer of a datagram token; a peer that moves keeps it. */
	Htab *tokens;
	/** @brief The client of a session id. */
	Htab *sessions;

	/** @brief The pending peers. */
	u32 pending;

	/** @brief Protects the clients and their sessions. */
	pthread_mutex_t lock;

} CerverUdp;

/**
 * @brief Sets the seconds without datagrams after which a peer is removed.
 *
 * @param[in,out] cerver the cerver
 * @param[in] peer_timeout the seconds, 0 keeps the peers until the cerver ends; the default is
 * #CERVER_UDP_DEFAULT_PEER_TIMEOUT
 */
CERVER_EXPORT void cerver_set_udp_peer_timeout (
	struct _Cerver *cerver, const u32 peer_timeout
);

/**
 * @brief Sets the peers that the cerver keeps at the same time.
 *
 * @param[in,out] cerver the cerver
 * @param[in] max_peers the peers, 0 does not limit them; the default is #CERVER_UDP_DEFAULT_MAX_PEERS
 */
CERVER_EXPORT void cerver_set_udp_max_peers (
	struct _Cerver *cerver, const u32 max_peers
);

/**
 * @brief Returns the number of peers of an UDP cerver, the pending ones too.
 *
 * A removed peer is already out of the stats of the cerver when it is not counted.
 *
 * @param[in] cerver the cerver
 * @return the peers, 0 for a NULL cerver
 * @threadsafety Safe from any thread, the count is read with __atomic.
 */
CERVER_EXPORT size_t cerver_udp_peers_count (const struct _Cerver *cerver);

/**
 * @brief Reads the header of a datagram that has a single packet, followed by nothing or by a datagram token.
 *
 * A header with another format, unknown flags or reserved bytes is invalid; the size of the packet must be between
 * the size of its header and max_packet_size.
 *
 * @param[in] datagram the bytes of the datagram
 * @param[in] size the size of the datagram
 * @param[in] max_packet_size the biggest packet
 * @param[out] header the header of the packet
 * @return what follows the packet, UDP_DATAGRAM_INVALID for a datagram that is not valid
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE UdpDatagram udp_datagram_check (
	const char *datagram, const size_t size,
	const size_t max_packet_size, PacketHeader *header
);

/**
 * @brief Compares in constant time the token after the packet with the expected one.
 *
 * @param[in] datagram a datagram of UDP_DATAGRAM_TOKEN
 * @param[in] header the header of its packet
 * @param[in] token the expected token, of CONNECTION_DATAGRAM_TOKEN_SIZE bytes
 * @return true if the tokens are equal
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE bool udp_datagram_token_matches (
	const char *datagram, const PacketHeader *header, const u8 *token
);

/**
 * @brief Creates the packet of a datagram that udp_datagram_check () accepted.
 *
 * @param[in] datagram the bytes of the datagram
 * @param[in] header the header of its packet
 * @return the new packet, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE Packet *udp_datagram_packet_create (
	const char *datagram, const PacketHeader *header
);

/**
 * @brief Receives the datagrams of the cerver until it is stopped.
 *
 * It runs in a single thread whatever the handler type of the cerver (cerver_set_handler_type ()); the handlers with
 * their own queues take the packets out of it. An UDP cerver does not take admins.
 *
 * @param[in,out] cerver the cerver
 * @retval 0 the cerver was stopped
 * @retval 1 on error
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 cerver_udp_start (struct _Cerver *cerver);

/**
 * @brief Releases the peers of the cerver, after its handlers and its thpool ended.
 *
 * @param[in,out] cerver the cerver, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_udp_end (struct _Cerver *cerver);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
