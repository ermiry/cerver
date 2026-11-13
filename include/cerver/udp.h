#ifndef _CERVER_UDP_H_
#define _CERVER_UDP_H_

#include <stdbool.h>
#include <pthread.h>
#include <time.h>

#include <netinet/in.h>

#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"
#include "cerver/collections/htab.h"

#include "cerver/config.h"
#include "cerver/network.h"
#include "cerver/packets.h"

// a peer of an UDP cerver that does not send anything in these secs is removed
#define CERVER_UDP_DEFAULT_PEER_TIMEOUT		16

// the peers that an UDP cerver keeps at the same time, a datagram of a new address is dropped after it
#define CERVER_UDP_DEFAULT_MAX_PEERS		1024

// how often the receive loop removes the inactive peers, in secs
#define CERVER_UDP_PEERS_CHECK_INTERVAL		1

// the ip with its zone and the port of a peer, its key in the peers of the cerver
#define CERVER_UDP_PEER_KEY_SIZE			(NETWORK_ADDRESS_STRING_SIZE + 8)

// a peer whose address is not proven (it did not authenticate) gets answers of the cerver
// of up to these times the bytes that it sent, so a forged address can not be flooded
#define CERVER_UDP_AMPLIFICATION_FACTOR		4

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Connection;

// what follows the packet of a datagram
typedef enum UdpDatagram {

	UDP_DATAGRAM_INVALID	= 0,	// it does not have a single packet of a valid size
	UDP_DATAGRAM_PLAIN		= 1,	// only the packet
	UDP_DATAGRAM_TOKEN		= 2		// the packet and the token of an authenticated connection

} UdpDatagram;

typedef enum UdpPeerState {

	// the cerver requires authentication and the peer did not authenticate,
	// it only sends auth, test and cerver info packets
	UDP_PEER_STATE_PENDING			= 0,

	// the cerver does not require authentication, its datagrams do not have a token
	UDP_PEER_STATE_OPEN				= 1,

	// every datagram of the peer, and every answer, has its token after the packet
	UDP_PEER_STATE_AUTHENTICATED	= 2

} UdpPeerState;

// the logical connection of an UDP cerver with the address of a peer:
// every datagram of the address is a packet of its connection,
// the connection has no socket of its own, the cerver answers with its socket
// a peer is released with its last reference: the peers of the cerver hold one,
// and every received packet that a handler has holds another one
typedef struct UdpPeer {

	char key[CERVER_UDP_PEER_KEY_SIZE];

	struct _Cerver *cerver;

	// the client is NULL while the peer is pending, with sessions it is shared with
	// the other peers of the session, and it is released with its last connection
	struct _Client *client;
	struct _Connection *connection;

	UdpPeerState state;

	time_t created;
	time_t last_activity;

	// the bytes of a peer whose address is not proven, to limit the answers to it
	size_t received_bytes;
	size_t answered_bytes;

	unsigned int references;

} UdpPeer;

// the peers of an UDP cerver, only its receive loop changes them,
// the lock protects the clients and their sessions, that the handlers can release
typedef struct CerverUdp {

	DoubleList *peers;

	Htab *addresses;    // the peer of an ip:port
	Htab *tokens;       // the authenticated peer of a datagram token, a peer that moves keeps it
	Htab *sessions;     // the client of a session id

	u32 pending;

	pthread_mutex_t lock;

} CerverUdp;

// sets the secs without datagrams after which a peer is removed,
// 0 keeps the peers until the cerver ends
// the default value is CERVER_UDP_DEFAULT_PEER_TIMEOUT
CERVER_EXPORT void cerver_set_udp_peer_timeout (
	struct _Cerver *cerver, const u32 peer_timeout
);

// sets the peers that the cerver keeps at the same time, 0 does not limit them
// the default value is CERVER_UDP_DEFAULT_MAX_PEERS
CERVER_EXPORT void cerver_set_udp_max_peers (
	struct _Cerver *cerver, const u32 max_peers
);

// returns the number of peers of an UDP cerver, the pending ones too,
// a removed peer is already out of the stats of the cerver when it is not counted
CERVER_EXPORT size_t cerver_udp_peers_count (const struct _Cerver *cerver);

// reads the header of a datagram that has a single packet,
// followed by nothing or by a datagram token,
// the size of the packet must be between the size of its header and max_packet_size
CERVER_PRIVATE UdpDatagram udp_datagram_check (
	const char *datagram, const size_t size,
	const size_t max_packet_size, PacketHeader *header
);

// compares in constant time the token after the packet with the expected one
CERVER_PRIVATE bool udp_datagram_token_matches (
	const char *datagram, const PacketHeader *header, const u8 *token
);

// creates the packet of a datagram that udp_datagram_check () accepted
CERVER_PRIVATE Packet *udp_datagram_packet_create (
	const char *datagram, const PacketHeader *header
);

// receives the datagrams of the cerver until it is stopped,
// in a single thread whatever the handler type of the cerver (cerver_set_handler_type ()),
// the handlers with their own queues take the packets out of it; an UDP cerver does not take admins
// returns 0 when the cerver was stopped, 1 on error
CERVER_PRIVATE u8 cerver_udp_start (struct _Cerver *cerver);

// releases the peers of the cerver, after its handlers and its thpool ended
CERVER_PRIVATE void cerver_udp_end (struct _Cerver *cerver);

#ifdef __cplusplus
}
#endif

#endif
