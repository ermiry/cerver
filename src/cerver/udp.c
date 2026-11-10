#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <time.h>

#include <sys/random.h>
#include <sys/socket.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"
#include "cerver/collections/htab.h"

#include "cerver/auth.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/errors.h"
#include "cerver/events.h"
#include "cerver/handler.h"
#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/sessions.h"
#include "cerver/udp.h"

#include "cerver/utils/log.h"

// the size of the maps of the peers when the cerver starts
static const size_t CERVER_UDP_MAP_SIZE = 64;

// the connection of a peer has no socket of its own, its packets are sent with the cerver's socket
static const i32 CERVER_UDP_PEER_SOCK_FD = -1;

// the answer to a failed authentication, to limit the answers to a peer that is not proven
static const size_t CERVER_UDP_AUTH_FAILED_SIZE = sizeof (PacketHeader) + sizeof (SError);

#define CERVER_UDP_PEER_KEY_FORMAT		"%s:%u"

static const char *udp_auth_missing_data = { "Missing auth data!" };
static const char *udp_auth_invalid_session = { "Session id is invalid!" };
static const char *udp_auth_internal_error = { "Internal cerver error!" };

#pragma region datagrams

// reads the header of a datagram that has a single packet,
// followed by nothing or by a datagram token,
// the size of the packet must be between the size of its header and max_packet_size
UdpDatagram udp_datagram_check (
	const char *datagram, const size_t size,
	const size_t max_packet_size, PacketHeader *header
) {

	UdpDatagram kind = UDP_DATAGRAM_INVALID;

	// a header with another format, unknown flags or reserved bytes is invalid
	if (
		datagram && (size >= PACKET_HEADER_SIZE)
		&& (packet_header_decode ((const u8 *) datagram, header) == PACKET_HEADER_RESULT_NONE)
	) {
		const bool valid_size = (header->packet_size >= PACKET_HEADER_SIZE)
			&& (header->packet_size <= (u64) max_packet_size)
			&& (header->packet_size <= (u64) size);

		if (valid_size) {
			const size_t trailing = size - (size_t) header->packet_size;

			if (!trailing) kind = UDP_DATAGRAM_PLAIN;
			else if (trailing == CONNECTION_DATAGRAM_TOKEN_SIZE) kind = UDP_DATAGRAM_TOKEN;
		}
	}

	return kind;

}

// compares in constant time the token after the packet with the expected one
bool udp_datagram_token_matches (
	const char *datagram, const PacketHeader *header, const u8 *token
) {

	const u8 *received = (const u8 *) datagram + header->packet_size;

	u8 difference = 0;
	for (size_t i = 0; i < CONNECTION_DATAGRAM_TOKEN_SIZE; i++) {
		difference |= (u8) (received[i] ^ token[i]);
	}

	return !difference;

}

// creates the packet of a datagram that udp_datagram_check () accepted
Packet *udp_datagram_packet_create (
	const char *datagram, const PacketHeader *header
) {

	Packet *packet = packet_create_with_data ((size_t) header->packet_size - PACKET_HEADER_SIZE);
	if (packet) {
		(void) memcpy (&packet->header, header, sizeof (PacketHeader));
		packet->packet_type = header->packet_type;
		packet->req_type = header->request_type;
		packet->packet_size = (size_t) header->packet_size;

		if (packet->data_size) {
			(void) memcpy (packet->data, datagram + PACKET_HEADER_SIZE, packet->data_size);
		}
	}

	return packet;

}

// a datagram that does not belong to a peer and can not create one
static void udp_datagram_dropped (Cerver *cerver) {

	CERVER_STATS_ADD (cerver->stats->received_packets->n_bad_packets, 1);

}

#pragma endregion

#pragma region clients

static void udp_session_insert (Cerver *cerver, Client *client) {

	if (client->session_id) {
		(void) htab_insert (
			cerver->udp->sessions,
			client->session_id->str, (size_t) client->session_id->len,
			client, sizeof (Client)
		);
	}

}

static void udp_session_remove (Cerver *cerver, const Client *client) {

	if (client->session_id) {
		(void) htab_remove (
			cerver->udp->sessions,
			client->session_id->str, (size_t) client->session_id->len
		);
	}

}

// the connection leaves its client, that is released with its last connection
// the lock of the peers must be held
static void udp_client_release (Cerver *cerver, Client *client, Connection *connection) {

	(void) dlist_remove (client->connections, connection, NULL);

	if (!dlist_size (client->connections)) {
		udp_session_remove (cerver, client);

		CERVER_STATS_SUBTRACT (cerver->stats->current_n_connected_clients, 1);

		client_delete (client);
	}

}

// a new client of the cerver, with its first connection
// returns 0 on success, 1 on error
static u8 udp_client_register (
	Cerver *cerver, Client *client, Connection *connection
) {

	u8 retval = 1;

	(void) pthread_mutex_lock (&cerver->udp->lock);

	if (!connection_register_to_client (client, connection)) {
		udp_session_insert (cerver, client);

		CERVER_STATS_ADD (cerver->stats->total_n_clients, 1);
		CERVER_STATS_ADD (cerver->stats->current_n_connected_clients, 1);

		retval = 0;
	}

	(void) pthread_mutex_unlock (&cerver->udp->lock);

	return retval;

}

// the connection joins the client of the session id, the client can not be released meanwhile
// returns the client, or NULL if there is not a client with the session id
static Client *udp_client_join_session (
	Cerver *cerver, Connection *connection, const String *session_id
) {

	Client *client = NULL;

	(void) pthread_mutex_lock (&cerver->udp->lock);

	Client *found = (Client *) htab_get (
		cerver->udp->sessions, session_id->str, (size_t) session_id->len
	);

	if (found && !connection_register_to_client (found, connection)) {
		client = found;
	}

	(void) pthread_mutex_unlock (&cerver->udp->lock);

	return client;

}

#pragma endregion

#pragma region peers

static void udp_peer_reference (UdpPeer *peer) {

	(void) __atomic_add_fetch (&peer->references, 1, __ATOMIC_ACQ_REL);

}

// the peer leaves its client, and the connection is released with it
static void udp_peer_free (UdpPeer *peer) {

	Cerver *cerver = peer->cerver;

	if (peer->client) {
		(void) pthread_mutex_lock (&cerver->udp->lock);
		udp_client_release (cerver, peer->client, peer->connection);
		(void) pthread_mutex_unlock (&cerver->udp->lock);
	}

	connection_delete (peer->connection);

	free (peer);

}

// the removal of the peer and packet_delete () release its references
static void udp_peer_release (void *peer_ptr) {

	UdpPeer *peer = (UdpPeer *) peer_ptr;

	if (!__atomic_sub_fetch (&peer->references, 1, __ATOMIC_ACQ_REL)) {
		udp_peer_free (peer);
	}

}

// writes the ip and the port of the address as the key of a peer
// returns 0 on success, 1 on error
static unsigned int udp_peer_key (
	const struct sockaddr_storage *address, char *key
) {

	unsigned int retval = 1;

	char ip[NETWORK_ADDRESS_STRING_SIZE] = { 0 };
	if (!sock_ip_to_string_actual ((const struct sockaddr *) address, ip)) {
		(void) snprintf (
			key, CERVER_UDP_PEER_KEY_SIZE, CERVER_UDP_PEER_KEY_FORMAT,
			ip, (unsigned int) sock_ip_port ((const struct sockaddr *) address)
		);

		retval = 0;
	}

	return retval;

}

static UdpPeer *udp_peer_create (
	Cerver *cerver, const struct sockaddr_storage *address, const char *key,
	const UdpPeerState state
) {

	UdpPeer *peer = (UdpPeer *) calloc (1, sizeof (UdpPeer));
	if (peer) {
		(void) snprintf (peer->key, CERVER_UDP_PEER_KEY_SIZE, "%s", key);

		peer->cerver = cerver;
		peer->connection = connection_create (CERVER_UDP_PEER_SOCK_FD, address, PROTOCOL_UDP);

		peer->state = state;

		peer->created = time (NULL);
		peer->last_activity = peer->created;

		// the peers of the cerver hold the first reference
		peer->references = 1;

		if (peer->connection) {
			peer->connection->auth_tries = cerver->max_auth_tries;
		}

		else {
			free (peer);
			peer = NULL;
		}
	}

	return peer;

}

// the peer is found by its address and kept by the peers of the cerver
// returns 0 on success, 1 on error
static u8 udp_peer_insert (Cerver *cerver, UdpPeer *peer) {

	u8 retval = 1;

	const size_t key_size = strlen (peer->key);

	if (!htab_insert (cerver->udp->addresses, peer->key, key_size, peer, sizeof (UdpPeer))) {
		if (!dlist_insert_at_end (cerver->udp->peers, peer)) {
			(void) __atomic_add_fetch (&cerver->udp_peers_count, 1, __ATOMIC_RELAXED);

			retval = 0;
		}

		else {
			(void) htab_remove (cerver->udp->addresses, peer->key, key_size);
		}
	}

	return retval;

}

static void udp_peer_remove_stats (Cerver *cerver, const UdpPeer *peer) {

	if (peer->state == UDP_PEER_STATE_PENDING) {
		cerver->udp->pending -= 1;
		CERVER_STATS_SUBTRACT (cerver->stats->current_n_hold_connections, 1);
	}

	else {
		CERVER_STATS_SUBTRACT (cerver->stats->current_active_client_connections, 1);
	}

}

// removes the peer from the structures of the cerver, it is released with its last reference,
// the event (CERVER_EVENT_NONE for none) is triggered while the peer is still referenced
static void udp_peer_remove (
	Cerver *cerver, ListElement *element, const CerverEventType event
) {

	UdpPeer *peer = (UdpPeer *) dlist_remove_element (cerver->udp->peers, element);
	if (peer) {
		(void) htab_remove (cerver->udp->addresses, peer->key, strlen (peer->key));

		if (peer->state == UDP_PEER_STATE_AUTHENTICATED) {
			(void) htab_remove (
				cerver->udp->tokens,
				peer->connection->datagram_token, CONNECTION_DATAGRAM_TOKEN_SIZE
			);
		}

		(void) __atomic_sub_fetch (&cerver->udp_peers_count, 1, __ATOMIC_RELAXED);

		udp_peer_remove_stats (cerver, peer);

		if ((event != CERVER_EVENT_NONE) && peer->client) {
			cerver_event_trigger (event, cerver, peer->client, peer->connection);
		}

		udp_peer_release (peer);
	}

}

static void udp_peer_remove_by_value (
	Cerver *cerver, const UdpPeer *peer, const CerverEventType event
) {

	for (ListElement *element = dlist_start (cerver->udp->peers); element; element = element->next) {
		if (element->data == peer) {
			udp_peer_remove (cerver, element, event);
			break;
		}
	}

}

// a peer that did not authenticate is removed after the on hold timeout,
// any other one after the peer timeout without datagrams
static bool udp_peer_expired (
	const Cerver *cerver, const UdpPeer *peer, const time_t now
) {

	bool expired = false;

	if (peer->state == UDP_PEER_STATE_PENDING) {
		expired = cerver->on_hold_timeout
			&& ((now - peer->created) >= (time_t) cerver->on_hold_timeout);
	}

	else {
		expired = cerver->udp_peer_timeout
			&& ((now - peer->last_activity) >= (time_t) cerver->udp_peer_timeout);
	}

	return expired;

}

static void udp_peers_check (Cerver *cerver, const time_t now) {

	ListElement *element = dlist_start (cerver->udp->peers);
	while (element) {
		ListElement *next = element->next;

		if (udp_peer_expired (cerver, (const UdpPeer *) element->data, now)) {
			udp_peer_remove (cerver, element, CERVER_EVENT_CLIENT_DROPPED);
		}

		element = next;
	}

}

// a peer whose address is not proven gets answers of the cerver
// of up to CERVER_UDP_AMPLIFICATION_FACTOR times the bytes that it sent
// returns true if the answer can be sent, and counts it
static bool udp_peer_answer_allowed (UdpPeer *peer, const size_t size) {

	bool allowed = true;

	if (peer->state != UDP_PEER_STATE_AUTHENTICATED) {
		allowed = (peer->answered_bytes + size)
			<= (peer->received_bytes * CERVER_UDP_AMPLIFICATION_FACTOR);

		if (allowed) peer->answered_bytes += size;
	}

	return allowed;

}

static u8 udp_peer_max_bad_packets (const Cerver *cerver, const UdpPeer *peer) {

	return (peer->state == UDP_PEER_STATE_PENDING)
		? cerver->on_hold_max_bad_packets : cerver->max_bad_packets;

}

static bool udp_peer_reached_max_bad_packets (const Cerver *cerver, const UdpPeer *peer) {

	const u8 max_bad_packets = udp_peer_max_bad_packets (cerver, peer);

	return max_bad_packets && (peer->connection->bad_packets >= max_bad_packets);

}

// a bad datagram or packet of the peer, it is removed when it reaches the max bad packets
static void udp_peer_bad (Cerver *cerver, UdpPeer *peer) {

	CERVER_STATS_ADD (cerver->stats->received_packets->n_bad_packets, 1);

	peer->connection->bad_packets += 1;

	if (udp_peer_reached_max_bad_packets (cerver, peer)) {
		cerver_log_warning (
			"Cerver %s UDP peer %s sent %u bad packets - removing it...",
			cerver->info->name, peer->key, (unsigned int) peer->connection->bad_packets
		);

		udp_peer_remove_by_value (cerver, peer, CERVER_EVENT_CLIENT_DROPPED);
	}

}

#pragma endregion

#pragma region connect

// a peer of a cerver without authentication has its own client from its first datagram,
// its connection is counted first, the removal of a peer that failed subtracts it
// returns 0 on success, 1 on error
static u8 udp_peer_open (Cerver *cerver, UdpPeer *peer) {

	u8 retval = 1;

	CERVER_STATS_ADD (cerver->stats->total_client_connections, 1);
	CERVER_STATS_ADD (cerver->stats->current_active_client_connections, 1);

	Client *client = client_create ();
	if (client) {
		if (!udp_client_register (cerver, client, peer->connection)) {
			peer->client = client;

			cerver_event_trigger (
				CERVER_EVENT_CLIENT_CONNECTED,
				cerver,
				peer->client, peer->connection
			);

			retval = 0;
		}

		else {
			client_delete (client);
		}
	}

	return retval;

}

static void udp_peer_pending (Cerver *cerver) {

	cerver->udp->pending += 1;

	CERVER_STATS_ADD (cerver->stats->total_on_hold_connections, 1);
	CERVER_STATS_ADD (cerver->stats->current_n_hold_connections, 1);

}

// a datagram of a new address can create a peer while the cerver has room for it
static bool udp_peer_can_create (const Cerver *cerver) {

	const u32 peers = __atomic_load_n (&cerver->udp_peers_count, __ATOMIC_RELAXED);

	bool can_create = !cerver->udp_max_peers || (peers < cerver->udp_max_peers);

	if (can_create && cerver->auth_required) {
		can_create = cerver->udp->pending < cerver->max_on_hold_connections;
	}

	return can_create;

}

// a pending peer is only created by the packets that it can send before it authenticates
static bool udp_peer_pending_packet (const PacketHeader *header) {

	return (header->packet_type == PACKET_TYPE_AUTH)
		|| (header->packet_type == PACKET_TYPE_CERVER)
		|| (header->packet_type == PACKET_TYPE_TEST);

}

// the peer of a new address
// returns NULL if it could not be created
static UdpPeer *udp_peer_connect (
	Cerver *cerver, const struct sockaddr_storage *address, const char *key
) {

	const UdpPeerState state = cerver->auth_required
		? UDP_PEER_STATE_PENDING : UDP_PEER_STATE_OPEN;

	UdpPeer *peer = udp_peer_create (cerver, address, key, state);
	if (peer) {
		if (!udp_peer_insert (cerver, peer)) {
			if (state == UDP_PEER_STATE_PENDING) {
				udp_peer_pending (cerver);
			}

			else if (udp_peer_open (cerver, peer)) {
				udp_peer_remove_by_value (cerver, peer, CERVER_EVENT_NONE);
				peer = NULL;
			}
		}

		else {
			udp_peer_release (peer);
			peer = NULL;
		}
	}

	return peer;

}

// the peer moves to a new address, like when its NAT changes its port,
// the write mutex of its socket protects the address that the senders read
// returns 0 on success, 1 if the peer could not be found by its new address
static u8 udp_peer_migrate (
	Cerver *cerver, UdpPeer *peer,
	const struct sockaddr_storage *address, const char *key
) {

	(void) htab_remove (cerver->udp->addresses, peer->key, strlen (peer->key));

	cerver_log_debug (
		"Cerver %s UDP peer %s moved to %s",
		cerver->info->name, peer->key, key
	);

	(void) snprintf (peer->key, CERVER_UDP_PEER_KEY_SIZE, "%s", key);

	(void) pthread_mutex_lock (peer->connection->socket->write_mutex);
	(void) memcpy (&peer->connection->address, address, sizeof (struct sockaddr_storage));
	connection_get_values (peer->connection);
	(void) pthread_mutex_unlock (peer->connection->socket->write_mutex);

	return (u8) htab_insert (
		cerver->udp->addresses, peer->key, strlen (peer->key), peer, sizeof (UdpPeer)
	);

}

#pragma endregion

#pragma region auth

// the success of an authentication, that the peer reads before it has the token:
// the session token when the cerver uses sessions, followed by the datagram token
static void udp_auth_success_send (Cerver *cerver, const UdpPeer *peer) {

	const size_t session_size = cerver->use_sessions ? sizeof (SToken) : 0;

	char data[sizeof (SToken) + CONNECTION_DATAGRAM_TOKEN_SIZE] = { 0 };

	if (session_size) {
		(void) client_get_session_id (peer->client, data, TOKEN_SIZE);
	}

	(void) memcpy (data + session_size, peer->connection->datagram_token, CONNECTION_DATAGRAM_TOKEN_SIZE);

	Packet *success = packet_generate_request (
		PACKET_TYPE_AUTH, AUTH_PACKET_TYPE_SUCCESS,
		data, session_size + CONNECTION_DATAGRAM_TOKEN_SIZE
	);

	if (success) {
		packet_set_network_values (success, cerver, peer->client, peer->connection, NULL);

		(void) packet_send_without_datagram_token (success);

		packet_delete (success);
	}

}

// a new datagram token that no other peer has
// returns 0 on success, 1 on error
static u8 udp_auth_token_generate (Cerver *cerver, u8 *token) {

	u8 retval = 1;

	if (!getentropy (token, CONNECTION_DATAGRAM_TOKEN_SIZE)) {
		retval = htab_contains_key (cerver->udp->tokens, token, CONNECTION_DATAGRAM_TOKEN_SIZE) ? 1 : 0;
	}

	return retval;

}

static void udp_auth_failed (Cerver *cerver, UdpPeer *peer, const char *message) {

	cerver_event_trigger (
		CERVER_EVENT_CLIENT_FAILED_AUTH,
		cerver,
		NULL, peer->connection
	);

	if (udp_peer_answer_allowed (peer, CERVER_UDP_AUTH_FAILED_SIZE)) {
		auth_send_failed_packet (cerver, peer->connection, message);
	}

	if (peer->connection->auth_tries) peer->connection->auth_tries -= 1;

	if (!peer->connection->auth_tries) {
		cerver_log_warning (
			"Cerver %s UDP peer %s reached its max auth tries - removing it...",
			cerver->info->name, peer->key
		);

		udp_peer_remove_by_value (cerver, peer, CERVER_EVENT_NONE);
	}

}

// a pending peer that could not finish its authentication leaves the client that it got
static void udp_auth_client_release (Cerver *cerver, UdpPeer *peer) {

	(void) pthread_mutex_lock (&cerver->udp->lock);
	udp_client_release (cerver, peer->client, peer->connection);
	(void) pthread_mutex_unlock (&cerver->udp->lock);

	peer->client = NULL;

	udp_auth_failed (cerver, peer, udp_auth_internal_error);

}

// the peer has a client: it gets its datagram token and every datagram has it from now on
static void udp_auth_success (Cerver *cerver, UdpPeer *peer, const bool joined) {

	u8 token[CONNECTION_DATAGRAM_TOKEN_SIZE];
	if (udp_auth_token_generate (cerver, token)) {
		udp_auth_client_release (cerver, peer);
		return;
	}

	connection_set_datagram_token (peer->connection, token);

	if (htab_insert (
		cerver->udp->tokens,
		peer->connection->datagram_token, CONNECTION_DATAGRAM_TOKEN_SIZE,
		peer, sizeof (UdpPeer)
	)) {
		connection_clear_datagram_token (peer->connection);
		udp_auth_client_release (cerver, peer);
		return;
	}

	udp_peer_remove_stats (cerver, peer);

	peer->state = UDP_PEER_STATE_AUTHENTICATED;
	peer->last_activity = time (NULL);

	peer->connection->authenticated = true;
	peer->connection->bad_packets = 0;

	CERVER_STATS_ADD (cerver->stats->total_client_connections, 1);
	CERVER_STATS_ADD (cerver->stats->current_active_client_connections, 1);

	udp_auth_success_send (cerver, peer);

	cerver_event_trigger (
		CERVER_EVENT_CLIENT_SUCCESS_AUTH,
		cerver,
		peer->client, peer->connection
	);

	cerver_event_trigger (
		joined ? CERVER_EVENT_CLIENT_NEW_CONNECTION : CERVER_EVENT_CLIENT_CONNECTED,
		cerver,
		peer->client, peer->connection
	);

}

// a new client for credentials that the authenticate method accepted,
// with the session id of the cerver's generator when it uses sessions
static Client *udp_auth_client_create (
	Cerver *cerver, Packet *packet, AuthData *auth_data, Connection *connection
) {

	Client *client = client_create ();
	if (client) {
		const bool failed = (cerver->use_sessions && auth_session_create (packet, auth_data, client))
			|| udp_client_register (cerver, client, connection);

		if (failed) {
			client_delete (client);
			client = NULL;
		}
	}

	return client;

}

// a session token joins the client of its session, any other auth data runs the authenticate method
static void udp_auth (Cerver *cerver, UdpPeer *peer, Packet *packet) {

	AuthData *auth_data = auth_data_get (packet);
	if (!auth_data) {
		udp_auth_failed (cerver, peer, udp_auth_missing_data);
		return;
	}

	String *message = NULL;
	bool joined = false;

	if (cerver->use_sessions && auth_data->token) {
		peer->client = udp_client_join_session (cerver, peer->connection, auth_data->token);
		joined = (peer->client != NULL);

		if (!joined) message = str_new (udp_auth_invalid_session);
	}

	else if (!auth_authenticate (packet, cerver->authenticate, auth_data, &message)) {
		peer->client = udp_auth_client_create (cerver, packet, auth_data, peer->connection);

		if (!peer->client) message = str_new (udp_auth_internal_error);
	}

	if (peer->client) udp_auth_success (cerver, peer, joined);
	else udp_auth_failed (cerver, peer, message ? message->str : NULL);

	str_delete (message);
	auth_data_release (auth_data);

}

// the auth packets of a pending peer, an admin can not authenticate with UDP
static void udp_auth_packet (Cerver *cerver, UdpPeer *peer, Packet *packet) {

	const bool valid = (packet->header.request_type == AUTH_PACKET_TYPE_CLIENT_AUTH)
		&& cerver->authenticate;

	if (valid) udp_auth (cerver, peer, packet);
	else udp_peer_bad (cerver, peer);

}

#pragma endregion

#pragma region packets

// the cerver info is only sent to a peer that asks for it,
// and to a peer that is not proven only up to the bytes that it sent
static void udp_info_answer (Cerver *cerver, UdpPeer *peer, const PacketHeader *header) {

	const Packet *info = cerver->info->cerver_info_packet;

	if (
		(header->request_type == CERVER_PACKET_TYPE_INFO)
		&& info && udp_peer_answer_allowed (peer, info->packet_size)
	) {
		(void) cerver_info_send_info_packet (cerver, peer->client, peer->connection);
	}

}

// a test packet goes back to the peer, to one that is not proven only up to the bytes that it sent
static void udp_test_answer (UdpPeer *peer, Packet *packet) {

	if (udp_peer_answer_allowed (peer, PACKET_HEADER_SIZE)) {
		cerver_test_packet_handler (packet);
	}

}

// every peer of the client is removed, the client is released with them
static void udp_peers_disconnect (Cerver *cerver, const Client *client) {

	ListElement *element = dlist_start (cerver->udp->peers);
	while (element) {
		ListElement *next = element->next;

		if (((const UdpPeer *) element->data)->client == client) {
			udp_peer_remove (cerver, element, CERVER_EVENT_NONE);
		}

		element = next;
	}

	cerver_event_trigger (
		CERVER_EVENT_CLIENT_DISCONNECTED,
		cerver,
		NULL, NULL
	);

}

// the client packets end the peer, or every peer of its client
static void udp_client_packet (Cerver *cerver, UdpPeer *peer, const PacketHeader *header) {

	switch (header->request_type) {
		case CLIENT_PACKET_TYPE_CLOSE_CONNECTION:
			udp_peer_remove_by_value (cerver, peer, CERVER_EVENT_CLIENT_CLOSE_CONNECTION);
			break;

		case CLIENT_PACKET_TYPE_DISCONNECT:
			udp_peers_disconnect (cerver, peer->client);
			break;

		default: break;
	}

}

// the packets that the handlers have hold a reference to their peer,
// a peer that reached its max bad packets is removed after them
static void udp_peer_packet_handle (Cerver *cerver, UdpPeer *peer, Packet *packet) {

	udp_peer_reference (peer);

	packet->release = udp_peer_release;
	packet->release_args = peer;

	(void) cerver_udp_packet_handle (packet);

	if (udp_peer_reached_max_bad_packets (cerver, peer)) {
		udp_peer_remove_by_value (cerver, peer, CERVER_EVENT_CLIENT_DROPPED);
	}

}

// a packet of a peer that is open or authenticated
static void udp_peer_packet (Cerver *cerver, UdpPeer *peer, Packet *packet) {

	peer->last_activity = time (NULL);
	peer->client->last_activity = peer->last_activity;

	switch (packet->header.packet_type) {
		case PACKET_TYPE_CLIENT:
			udp_client_packet (cerver, peer, &packet->header);
			packet_delete (packet);
			break;

		case PACKET_TYPE_CERVER:
			udp_info_answer (cerver, peer, &packet->header);
			packet_delete (packet);
			break;

		// the files are sent with the socket of a connection, and the players of the lobbies
		// are found by the socket of their connection, a peer does not have one
		case PACKET_TYPE_REQUEST:
		case PACKET_TYPE_GAME:
			udp_peer_bad (cerver, peer);
			packet_delete (packet);
			break;

		default:
			udp_peer_packet_handle (cerver, peer, packet);
			break;
	}

}

// a packet of a peer that did not authenticate yet
static void udp_pending_packet (Cerver *cerver, UdpPeer *peer, Packet *packet) {

	if (cerver->on_hold_check_packets && !packet_check (packet)) {
		udp_peer_bad (cerver, peer);
	}

	else {
		switch (packet->header.packet_type) {
			case PACKET_TYPE_AUTH: udp_auth_packet (cerver, peer, packet); break;
			case PACKET_TYPE_CERVER: udp_info_answer (cerver, peer, &packet->header); break;
			case PACKET_TYPE_TEST: udp_test_answer (peer, packet); break;

			default: udp_peer_bad (cerver, peer); break;
		}
	}

	packet_delete (packet);

}

// the packet of a valid datagram of the peer
static void udp_peer_accept (
	Cerver *cerver, UdpPeer *peer,
	const char *datagram, const PacketHeader *header
) {

	Packet *packet = udp_datagram_packet_create (datagram, header);
	if (packet) {
		packet->cerver = cerver;
		packet->client = peer->client;
		packet->connection = peer->connection;
		packet->lobby = NULL;

		if (peer->state == UDP_PEER_STATE_PENDING) udp_pending_packet (cerver, peer, packet);
		else udp_peer_packet (cerver, peer, packet);
	}

}

// an auth packet without the token asks again for the success that the peer did not get,
// its address can be forged, so the success is sent only for a datagram big enough for it
static bool udp_auth_repeat_allowed (
	const Cerver *cerver, const PacketHeader *header, const size_t size
) {

	const size_t session_size = cerver->use_sessions ? sizeof (SToken) : 0;
	const size_t success_size = sizeof (PacketHeader) + session_size + CONNECTION_DATAGRAM_TOKEN_SIZE;

	return (header->packet_type == PACKET_TYPE_AUTH)
		&& ((size * CERVER_UDP_AMPLIFICATION_FACTOR) >= success_size);

}

// an authenticated peer sends every datagram with its token
static void udp_authenticated_datagram (
	Cerver *cerver, UdpPeer *peer,
	const char *datagram, const size_t size,
	const PacketHeader *header, const UdpDatagram kind
) {

	if (
		(kind == UDP_DATAGRAM_TOKEN)
		&& udp_datagram_token_matches (datagram, header, peer->connection->datagram_token)
	) {
		udp_peer_accept (cerver, peer, datagram, header);
	}

	else if ((kind == UDP_DATAGRAM_PLAIN) && udp_auth_repeat_allowed (cerver, header, size)) {
		udp_auth_success_send (cerver, peer);
	}

	else {
		udp_peer_bad (cerver, peer);
	}

}

// a datagram from the address of a peer
static void udp_peer_datagram (
	Cerver *cerver, UdpPeer *peer,
	const char *datagram, const size_t size,
	const PacketHeader *header, const UdpDatagram kind
) {

	if (peer->state != UDP_PEER_STATE_AUTHENTICATED) {
		peer->received_bytes += size;
	}

	switch (peer->state) {
		case UDP_PEER_STATE_PENDING:
		case UDP_PEER_STATE_OPEN:
			if (kind == UDP_DATAGRAM_PLAIN) udp_peer_accept (cerver, peer, datagram, header);
			else udp_peer_bad (cerver, peer);
			break;

		case UDP_PEER_STATE_AUTHENTICATED:
			udp_authenticated_datagram (cerver, peer, datagram, size, header, kind);
			break;

		default: break;
	}

}

// a datagram with the token of a peer from another address moves the peer to it
static void udp_token_datagram (
	Cerver *cerver, const struct sockaddr_storage *address, const char *key,
	const char *datagram, const PacketHeader *header
) {

	UdpPeer *peer = (UdpPeer *) htab_get (
		cerver->udp->tokens,
		datagram + header->packet_size, CONNECTION_DATAGRAM_TOKEN_SIZE
	);

	if (peer && udp_datagram_token_matches (datagram, header, peer->connection->datagram_token)) {
		if (!udp_peer_migrate (cerver, peer, address, key)) {
			udp_peer_accept (cerver, peer, datagram, header);
		}

		else {
			udp_peer_remove_by_value (cerver, peer, CERVER_EVENT_CLIENT_DROPPED);
		}
	}

	else {
		udp_datagram_dropped (cerver);
	}

}

// a datagram from an address without a peer
static void udp_address_datagram (
	Cerver *cerver, const struct sockaddr_storage *address, const char *key,
	const char *datagram, const size_t size,
	const PacketHeader *header, const UdpDatagram kind
) {

	if (kind == UDP_DATAGRAM_TOKEN) {
		udp_token_datagram (cerver, address, key, datagram, header);
	}

	else if (
		(kind == UDP_DATAGRAM_PLAIN)
		&& udp_peer_can_create (cerver)
		&& (!cerver->auth_required || udp_peer_pending_packet (header))
	) {
		UdpPeer *peer = udp_peer_connect (cerver, address, key);
		if (peer) udp_peer_datagram (cerver, peer, datagram, size, header, kind);
	}

	else {
		CERVER_STATS_ADD (cerver->stats->total_rejected_connections, 1);
		udp_datagram_dropped (cerver);
	}

}

// the datagram is checked before its peer is found or created
static void udp_datagram_handle (
	Cerver *cerver, const struct sockaddr_storage *address,
	const char *datagram, const size_t size
) {

	CERVER_STATS_ADD (cerver->stats->total_n_receives_done, 1);
	CERVER_STATS_ADD (cerver->stats->total_bytes_received, (u64) size);

	char key[CERVER_UDP_PEER_KEY_SIZE] = { 0 };
	if (udp_peer_key (address, key)) return;

	PacketHeader header;
	const UdpDatagram kind = udp_datagram_check (
		datagram, size, cerver->max_received_packet_size, &header
	);

	UdpPeer *peer = (UdpPeer *) htab_get (cerver->udp->addresses, key, strlen (key));

	if (peer) udp_peer_datagram (cerver, peer, datagram, size, &header, kind);
	else if (kind != UDP_DATAGRAM_INVALID) udp_address_datagram (cerver, address, key, datagram, size, &header, kind);
	else udp_datagram_dropped (cerver);

}

#pragma endregion

#pragma region receive

// receives one datagram when the socket has one before the poll timeout,
// the shutdown () of cerver_stop_request () wakes it up without an address
// returns true if the loop can go on
static bool udp_receive (Cerver *cerver, char *buffer, const size_t buffer_size) {

	bool proceed = true;

	struct pollfd poll_fd = { .fd = cerver->sock, .events = POLLIN, .revents = 0 };
	const int ready = poll (&poll_fd, 1, (int) cerver->poll_timeout);

	if (ready > 0) {
		struct sockaddr_storage address;
		socklen_t address_size = sizeof (struct sockaddr_storage);

		const ssize_t received = recvfrom (
			cerver->sock, buffer, buffer_size, 0,
			(struct sockaddr *) &address, &address_size
		);

		if ((received >= 0) && address_size && cerver_is_running (cerver)) {
			udp_datagram_handle (cerver, &address, buffer, (size_t) received);
		}

		else if ((received < 0) && (errno != EINTR) && (errno != EAGAIN)) {
			proceed = false;
		}
	}

	else if ((ready < 0) && (errno != EINTR)) {
		proceed = false;
	}

	return proceed;

}

static CerverUdp *udp_create (void) {

	CerverUdp *udp = (CerverUdp *) calloc (1, sizeof (CerverUdp));
	if (udp) {
		udp->peers = dlist_init (NULL, NULL);
		udp->addresses = htab_create (CERVER_UDP_MAP_SIZE, NULL, NULL);
		udp->tokens = htab_create (CERVER_UDP_MAP_SIZE, NULL, NULL);
		udp->sessions = htab_create (CERVER_UDP_MAP_SIZE, NULL, NULL);

		udp->pending = 0;

		(void) pthread_mutex_init (&udp->lock, NULL);
	}

	return udp;

}

static bool udp_is_valid (const CerverUdp *udp) {

	return udp && udp->peers && udp->addresses && udp->tokens && udp->sessions;

}

static void udp_receive_loop (Cerver *cerver, char *buffer, bool *proceed) {

	time_t last_check = time (NULL);

	while (*proceed && cerver_is_running (cerver)) {
		*proceed = udp_receive (cerver, buffer, MAX_UDP_PACKET_SIZE);

		const time_t now = time (NULL);
		if ((now - last_check) >= CERVER_UDP_PEERS_CHECK_INTERVAL) {
			udp_peers_check (cerver, now);
			last_check = now;
		}
	}

}

// receives the datagrams of the cerver until it is stopped
// returns 0 when the cerver was stopped, 1 on error
u8 cerver_udp_start (Cerver *cerver) {

	u8 retval = 1;

	cerver->udp = udp_create ();

	char *buffer = (char *) malloc (MAX_UDP_PACKET_SIZE);

	if (udp_is_valid (cerver->udp) && buffer) {
		cerver_log_success ("Cerver %s receives UDP datagrams in port %u", cerver->info->name, cerver->port);

		bool proceed = true;
		udp_receive_loop (cerver, buffer, &proceed);

		// a failed receive while the cerver is still running is an error
		retval = (proceed || !cerver_is_running (cerver)) ? 0 : 1;
	}

	else {
		cerver_log_error ("Failed to init the UDP peers of cerver %s!", cerver->info->name);
	}

	free (buffer);

	return retval;

}

// releases the peers of the cerver, after its handlers and its thpool ended
void cerver_udp_end (Cerver *cerver) {

	if (cerver && cerver->udp) {
		CerverUdp *udp = cerver->udp;

		if (udp->peers) {
			while (dlist_start (udp->peers)) {
				udp_peer_remove (cerver, dlist_start (udp->peers), CERVER_EVENT_NONE);
			}
		}

		dlist_delete (udp->peers);

		htab_destroy (udp->addresses);
		htab_destroy (udp->tokens);
		htab_destroy (udp->sessions);

		(void) pthread_mutex_destroy (&udp->lock);

		free (udp);

		cerver->udp = NULL;
	}

}

#pragma endregion

#pragma region configuration

// sets the secs without datagrams after which a peer is removed,
// 0 keeps the peers until the cerver ends
void cerver_set_udp_peer_timeout (Cerver *cerver, const u32 peer_timeout) {

	if (cerver) cerver->udp_peer_timeout = peer_timeout;

}

// sets the peers that the cerver keeps at the same time, 0 does not limit them
void cerver_set_udp_max_peers (Cerver *cerver, const u32 max_peers) {

	if (cerver) cerver->udp_max_peers = max_peers;

}

// returns the number of peers of an UDP cerver, the pending ones too
size_t cerver_udp_peers_count (const Cerver *cerver) {

	return cerver ? (size_t) __atomic_load_n (&cerver->udp_peers_count, __ATOMIC_RELAXED) : 0;

}

#pragma endregion
