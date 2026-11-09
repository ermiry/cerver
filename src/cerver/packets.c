#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <errno.h>

#include "cerver/config.h"

#include <fcntl.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include "cerver/types/types.h"

#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/socket.h"

#include "cerver/game/lobby.h"

#ifdef PACKETS_DEBUG
#include "cerver/utils/log.h"
#endif

#define PACKET_ROUTE_BUFFER_SIZE			4096

// a split packet is sent as its header and its data
#define PACKET_SPLIT_PARTS					2

// a datagram has the header and the data of the packet, and the token of the connection
#define PACKET_DATAGRAM_MAX_PARTS			3

#pragma region protocol

static ProtocolID protocol_id = 0;
static ProtocolVersion protocol_version = { 0, 0 };

ProtocolID packets_get_protocol_id (void) {
	
	return protocol_id;
	
}

void packets_set_protocol_id (ProtocolID proto_id) {
	
	protocol_id = proto_id;
	
}

ProtocolVersion packets_get_protocol_version (void) {
	
	return protocol_version;
	
}

void packets_set_protocol_version (ProtocolVersion version) {

	protocol_version = version;

}

PacketVersion packet_version_local (void) {

	PacketVersion version = { protocol_id, protocol_version };

	return version;

}

#pragma endregion

#pragma region version

PacketVersion *packet_version_new (void) {

	PacketVersion *version = (PacketVersion *) malloc (sizeof (PacketVersion));
	if (version) {
		version->protocol_id = 0;
		version->protocol_version.minor = 0;
		version->protocol_version.major = 0;
	}

	return version;

}

void packet_version_delete (PacketVersion *version) {
	
	if (version) free (version);
	
}

PacketVersion *packet_version_create (void) {

	PacketVersion *version = (PacketVersion *) malloc (sizeof (PacketVersion));
	if (version) {
		version->protocol_id = protocol_id;
		version->protocol_version = protocol_version;
	}

	return version;

}

// copies the data from the source version to the destination
// returns 0 on success, 1 on error
u8 packet_version_copy (
	PacketVersion *dest, const PacketVersion *source
) {

	u8 retval = 1;

	if (dest && source) {
		(void) memcpy (dest, source, sizeof (PacketVersion));
		retval = 0;
	}

	return retval;

}

void packet_version_print (
	const PacketVersion *version
) {

	if (version) {
		(void) printf (
			"Protocol id: %u\n",
			version->protocol_id
		);

		(void) printf (
			"Protocol version: { %u - %u }\n",
			version->protocol_version.major,
			version->protocol_version.minor
		);
	}

}

#pragma endregion

#pragma region types

PacketsPerType *packets_per_type_new (void) {

	PacketsPerType *packets_per_type = (PacketsPerType *) malloc (sizeof (PacketsPerType));
	if (packets_per_type) {
		(void) memset (packets_per_type, 0, sizeof (PacketsPerType));
	}

	return packets_per_type;

}

void packets_per_type_delete (void *packets_per_type_ptr) {
		
	if (packets_per_type_ptr) free (packets_per_type_ptr);

}

void packets_per_type_print (
	const PacketsPerType *packets_per_type
) {

	if (packets_per_type) {
		cerver_log_msg ("Cerver:            %ld", CERVER_STATS_GET (packets_per_type->n_cerver_packets));
		cerver_log_msg ("Client:            %ld", CERVER_STATS_GET (packets_per_type->n_client_packets));
		cerver_log_msg ("Error:             %ld", CERVER_STATS_GET (packets_per_type->n_error_packets));
		cerver_log_msg ("Request:           %ld", CERVER_STATS_GET (packets_per_type->n_request_packets));
		cerver_log_msg ("Auth:              %ld", CERVER_STATS_GET (packets_per_type->n_auth_packets));
		cerver_log_msg ("Game:              %ld", CERVER_STATS_GET (packets_per_type->n_game_packets));
		cerver_log_msg ("App:               %ld", CERVER_STATS_GET (packets_per_type->n_app_packets));
		cerver_log_msg ("App Error:         %ld", CERVER_STATS_GET (packets_per_type->n_app_error_packets));
		cerver_log_msg ("Custom:            %ld", CERVER_STATS_GET (packets_per_type->n_custom_packets));
		cerver_log_msg ("Test:              %ld", CERVER_STATS_GET (packets_per_type->n_test_packets));
		cerver_log_msg ("Unknown:           %ld", CERVER_STATS_GET (packets_per_type->n_unknown_packets));
		cerver_log_msg ("Bad:               %ld", CERVER_STATS_GET (packets_per_type->n_bad_packets));
	}

}

void packets_per_type_array_print (
	const u64 packets[PACKETS_MAX_TYPES]
) {

	cerver_log_msg ("\tCerver:              %ld", packets[PACKET_TYPE_CERVER]);
	cerver_log_msg ("\tClient:              %ld", packets[PACKET_TYPE_CLIENT]);
	cerver_log_msg ("\tError:               %ld", packets[PACKET_TYPE_ERROR]);
	cerver_log_msg ("\tRequest:             %ld", packets[PACKET_TYPE_REQUEST]);
	cerver_log_msg ("\tAuth:                %ld", packets[PACKET_TYPE_AUTH]);
	cerver_log_msg ("\tGame:                %ld", packets[PACKET_TYPE_GAME]);
	cerver_log_msg ("\tApp:                 %ld", packets[PACKET_TYPE_APP]);
	cerver_log_msg ("\tApp Error:           %ld", packets[PACKET_TYPE_APP_ERROR]);
	cerver_log_msg ("\tCustom:              %ld", packets[PACKET_TYPE_CUSTOM]);
	cerver_log_msg ("\tTest:                %ld", packets[PACKET_TYPE_TEST]);
	cerver_log_msg ("\tBad:                 %ld", packets[PACKET_TYPE_BAD]);

}

#pragma endregion

#pragma region header

PacketHeader *packet_header_new (void) {

	PacketHeader *header = (PacketHeader *) malloc (sizeof (PacketHeader));
	if (header) {
		(void) memset (header, 0, sizeof (PacketHeader));
	}

	return header;

}

void packet_header_delete (PacketHeader *header) {
	
	if (header) free (header);
	
}

// a header is sent as it is in memory, so its padding is zeroed too,
// it would send uninitialized bytes of the process otherwise
static void packet_header_init (
	PacketHeader *header,
	const PacketType packet_type,
	const size_t packet_size,
	const u32 req_type
) {

	(void) memset (header, 0, sizeof (PacketHeader));

	header->packet_type = packet_type;
	header->packet_size = packet_size;

	header->request_type = req_type;

	header->version = packet_version_local ();

}

PacketHeader *packet_header_create (
	const PacketType packet_type,
	const size_t packet_size,
	const u32 req_type
) {

	PacketHeader *header = (PacketHeader *) malloc (sizeof (PacketHeader));
	if (header) {
		packet_header_init (header, packet_type, packet_size, req_type);
	}

	return header;

}

// allocates a new packet header and copies the values from source
PacketHeader *packet_header_create_from (const PacketHeader *source) {

	PacketHeader *header = packet_header_new ();
	if (header && source) {
		(void) memcpy (header, source, sizeof (PacketHeader));
	}

	return header;

}

// copies the data from the source header to the destination
// returns 0 on success, 1 on error
u8 packet_header_copy (PacketHeader *dest, const PacketHeader *source) {

	u8 retval = 1;

	if (dest && source) {
		(void) memcpy (dest, source, sizeof (PacketHeader));
		retval = 0;
	}

	return retval;

}

void packet_header_print (const PacketHeader *header) {

	if (header) {
		(void) printf ("Header size: %lu\n", sizeof (PacketHeader));
		(void) printf ("Packet type [%lu]: %u\n", sizeof (PacketType), header->packet_type);
		(void) printf ("Packet size: [%lu] %lu\n", sizeof (size_t), header->packet_size);
		(void) printf ("Handler id [%lu]: %u\n", sizeof (u8), header->handler_id);
		(void) printf ("Request type [%lu]: %u\n", sizeof (u32), header->request_type);
		(void) printf ("Sock fd [%lu]: %u\n", sizeof (u16), header->sock_fd);
	}

}

void packet_header_log (const PacketHeader *header) {

	if (header) {
		cerver_log_msg ("Packet type: %u", header->packet_type);
		cerver_log_msg ("Packet size: %lu", header->packet_size);
		cerver_log_msg ("Handler id: %u", header->handler_id);
		cerver_log_msg ("Request type: %u", header->request_type);
		cerver_log_msg ("Sock fd: %u", header->sock_fd);
	}

}

#pragma endregion

#pragma region packets

u8 packet_append_data (
	Packet *packet,
	const void *data, const size_t data_size
);

Packet *packet_new (void) {

	Packet *packet = (Packet *) malloc (sizeof (Packet));
	if (packet) {
		packet->cerver = NULL;
		packet->client = NULL;
		packet->connection = NULL;
		packet->lobby = NULL;

		packet->packet_type = PACKET_TYPE_NONE;
		packet->req_type = 0;

		packet->data_size = 0;
		packet->data = NULL;
		packet->data_ptr = NULL;
		packet->data_end = NULL;
		packet->data_ref = false;

		packet->remaining_data = 0;

		packet_header_init (&packet->header, PACKET_TYPE_NONE, 0, 0);

		// a received packet takes the version of its header
		packet->version = packet_version_local ();

		packet->packet_size = 0;
		packet->packet = NULL;
		packet->packet_ref = false;

		packet->release = NULL;
		packet->release_args = NULL;
	}

	return packet;

}

void packet_delete (void *packet_ptr) {

	if (packet_ptr) {
		Packet *packet = (Packet *) packet_ptr;

		if (packet->release) packet->release (packet->release_args);

		packet->cerver = NULL;
		packet->client = NULL;
		packet->connection = NULL;
		packet->lobby = NULL;

		if (!packet->data_ref) {
			if (packet->data) free (packet->data);
		}

		if (!packet->packet_ref) {
			if (packet->packet) free (packet->packet);
		}

		free (packet);
	}

}

// create a new packet with the option to pass values directly
// data is copied into packet buffer and can be safely freed
Packet *packet_create (
	const PacketType type, const u32 req_type,
	const void *data, const size_t data_size
) {

	Packet *packet = packet_new ();
	if (packet) {
		packet->packet_type = type;
		packet->req_type = req_type;

		if (data) {
			packet_append_data (packet, data, data_size);
		}
	}

	return packet;

}

// creates a packet with a data buffer of the specified size
Packet *packet_create_with_data (
	const size_t data_size
) {

	Packet *packet = packet_new ();
	if (packet) {
		if (data_size > 0) {
			packet->data = malloc (data_size);
			if (packet->data) {
				packet->data_size = data_size;
				packet->data_end = packet->data;
				packet->remaining_data = data_size;
			}

			else {
				packet_delete (packet);
				packet = NULL;
			}
		}
	}

	return packet;

}

// sets the pakcet destinatary
void packet_set_network_values (
	Packet *packet,
	Cerver *cerver,
	Client *client, Connection *connection,
	Lobby *lobby
) {

	if (packet) {
		packet->cerver = cerver;
		packet->client = client;
		packet->connection = connection;
		packet->lobby = lobby;
	}

}


// sets the packet's header
// copies the header's values into the packet
void packet_set_header (
	Packet *packet, const PacketHeader *header
) {

	if (packet && header) {
		(void) memcpy (&packet->header, header, sizeof (PacketHeader));
	}

}

// sets the packet's header values
// if the packet does NOT yet have a header, it will be created
void packet_set_header_values (
	Packet *packet,
	PacketType packet_type, size_t packet_size,
	u8 handler_id, u32 request_type,
	u16 sock_fd
) {

	if (packet) {
		packet->header.packet_type = packet_type;
		packet->header.packet_size = packet_size;
		packet->header.handler_id = handler_id;
		packet->header.request_type = request_type;
		packet->header.sock_fd = sock_fd;
	}

}

// allocates the packet's data with size data_size
// data can be added using packet_add_data ()
// returns 0 on success, 1 on error
unsigned int packet_create_data (
	Packet *packet, const size_t data_size
) {

	unsigned int retval = 1;

	if (packet && (data_size > 0)) {
		packet->data = malloc (data_size);
		if (packet->data) {
			packet->data_size = data_size;
			packet->data_end = packet->data;
			packet->remaining_data = data_size;

			retval = 0;
		}
	}

	return retval;

}

// sets the data of the packet -> copies the data into the packet
// if the packet had data before it is deleted and replaced with the new one
// returns 0 on success, 1 on error
u8 packet_set_data (
	Packet *packet,
	const void *data, const size_t data_size
) {

	u8 retval = 1;

	if (packet && data) {
		// check if there was data in the packet before
		if (!packet->data_ref) {
			if (packet->data) free (packet->data);
		}

		packet->data_size = data_size;
		packet->data = malloc (packet->data_size);
		if (packet->data) {
			(void) memcpy (packet->data, data, data_size);
			packet->data_end = (char *) packet->data;
			packet->data_end += packet->data_size;

			// point to the start of the data
			packet->data_ptr = (char *) packet->data;

			retval = 0;
		}
	}

	return retval;

}

// adds the data to the packet's existing data buffer
// the data size must be <= the packet's remaining data
// returns 0 on success, 1 on error
u8 packet_add_data (
	Packet *packet,
	const void *data, const size_t data_size
) {

	u8 retval = 1;

	if (packet && data) {
		// check that we can copy the data
		if (data_size <= packet->remaining_data) {
			(void) memcpy (packet->data_end, data, data_size);

			packet->data_end += data_size;
			packet->remaining_data -= data_size;

			retval = 0;
		}
	}

	return retval;

}

// appends the data to the end if the packet already has data
// if the packet is empty, creates a new buffer
// it creates a new copy of the data and the original can be safely freed
// this does not work if the data has been set using a reference
u8 packet_append_data (
	Packet *packet,
	const void *data, const size_t data_size
) {

	u8 retval = 1;

	if (packet && data) {
		// append the data to the end if the packet already has data
		if (packet->data) {
			size_t new_size = packet->data_size + data_size;
			void *new_data = realloc (packet->data, new_size);
			if (new_data) {
				packet->data_end = (char *) new_data;
				packet->data_end += packet->data_size;

				// copy the new buffer
				(void) memcpy (packet->data_end, data, data_size);
				packet->data_end += data_size;

				packet->data = new_data;
				packet->data_size = new_size;

				// point to the start of the data
				packet->data_ptr = (char *) packet->data;

				retval = 0;
			}

			else {
				#ifdef PACKETS_DEBUG
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_NONE,
					"Failed to realloc packet data!"
				);
				#endif
				packet->data = NULL;
				packet->data_size = 0;
			}
		}

		// if the packet is empty, create a new buffer
		else {
			packet->data_size = data_size;
			packet->data = malloc (packet->data_size);
			if (packet->data) {
				// copy the data to the packet data buffer
				(void) memcpy (packet->data, data, data_size);
				packet->data_end = (char *) packet->data;
				packet->data_end += packet->data_size;

				// point to the start of the data
				packet->data_ptr = (char *) packet->data;

				retval = 0;
			}

			else {
				#ifdef PACKETS_DEBUG
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_NONE,
					"Failed to allocate packet data!"
				);
				#endif
				packet->data = NULL;
				packet->data_size = 0;
			}
		}
	}

	return retval;

}

// sets a reference to a data buffer to send
// data will not be copied into the packet and will not be freed after use
// this method is usefull for example if you just want to send a raw json packet to a non-cerver
// use this method with packet_send () with the raw flag on
u8 packet_set_data_ref (
	Packet *packet, void *data, size_t data_size
) {

	u8 retval = 1;

	if (packet && data) {
		if (!packet->data_ref) {
			if (packet->data) free (packet->data);
		}

		packet->data = data;
		packet->data_size = data_size;
		packet->data_ref = true;

		retval = 0;
	}

	return retval;

}

// sets a the packet's packet using by copying the passed data
// deletes the previuos packet's packet
// returns 0 on succes, 1 on error
u8 packet_set_packet (
	Packet *packet, void *data, size_t data_size
) {

	u8 retval = 1;

	if (packet && data) {
		if (!packet->packet_ref) {
			if (packet->packet) free (packet->packet);
		}

		packet->packet_size = data_size;
		packet->packet = malloc (packet->packet_size);
		if (packet->packet) {
			(void) memcpy (packet->packet, data, data_size);

			retval = 0;
		}
	}

	return retval;

}

// sets a reference to a data buffer to send as the packet
// data will not be copied into the packet and will not be freed after use
// usefull when you need to generate your own cerver type packet by hand
u8 packet_set_packet_ref (
	Packet *packet, void *data, size_t packet_size
) {

	u8 retval = 1;

	if (packet && data) {
		if (!packet->packet_ref) {
			if (packet->packet) free (packet->packet);
		}

		packet->packet = data;
		packet->packet_size = packet_size;
		packet->packet_ref = true;

		retval = 0;
	}

	return retval;

}

// prepares the packet to be ready to be sent
// returns 0 on sucess, 1 on error
u8 packet_generate (Packet *packet) {

	u8 retval = 0;

	if (packet) {
		// a request packet references its own header
		if (packet->packet && !packet->packet_ref) {
			free (packet->packet);
		}

		packet->packet = NULL;
		packet->packet_size = 0;
		packet->packet_ref = false;

		packet->packet_size = sizeof (PacketHeader) + packet->data_size;

		packet->header.packet_type = packet->packet_type;
		packet->header.packet_size = packet->packet_size;
		packet->header.request_type = packet->req_type;
		packet->header.version = packet_version_local ();

		// create the packet buffer to be sent
		packet->packet = malloc (packet->packet_size);
		if (packet->packet) {
			char *end = (char *) packet->packet;
			(void) memcpy (end, &packet->header, sizeof (PacketHeader));

			if (packet->data_size > 0) {
				end += sizeof (PacketHeader);
				(void) memcpy (end, packet->data, packet->data_size);
			}

			retval = 0;
		}
	}

	return retval;

}

// creates a request packet that is ready to be sent
// returns a newly allocated packet
Packet *packet_create_request (
	const PacketType packet_type,
	const u32 request_type
) {

	Packet *packet = (Packet *) malloc (sizeof (Packet));
	if (packet) {
		*packet = (Packet) {
			.cerver = NULL,
			.client = NULL,
			.connection = NULL,
			.lobby = NULL,

			.packet_type = packet_type,
			.req_type = request_type,

			.data_size = 0,
			.data = NULL,
			.data_ptr = NULL,
			.data_end = NULL,
			.data_ref = false,

			.header = (PacketHeader) {
				.packet_type = packet_type,
				.packet_size = sizeof (PacketHeader),

				.handler_id = 0,

				.request_type = request_type,

				.sock_fd = 0,

				.version = packet_version_local ()
			},

			.version = packet_version_local (),

			.packet_size = sizeof (PacketHeader),
			.packet = (void *) &packet->header,
			.packet_ref = true,

			.release = NULL,
			.release_args = NULL
		};

		// the header is sent from the packet itself, the literal does not zero its padding
		packet_header_init (&packet->header, packet_type, sizeof (PacketHeader), request_type);
	}

	return packet;

}

// creates a new ping packet (PACKET_TYPE_TEST)
// returns a newly allocated packet
Packet *packet_create_ping (void) {

	return packet_create_request (PACKET_TYPE_TEST, 0);

}

// initializes an already allocated packet with request related values
void packet_init_request (
	Packet *packet,
	const PacketType packet_type,
	const u32 request_type
) {

	*packet = (Packet) {
		.cerver = NULL,
		.client = NULL,
		.connection = NULL,
		.lobby = NULL,

		.packet_type = packet_type,
		.req_type = request_type,

		.data_size = 0,
		.data = NULL,
		.data_ptr = NULL,
		.data_end = NULL,
		.data_ref = false,

		.version = packet_version_local (),

		.packet_size = sizeof (PacketHeader),
		.packet = (void *) &packet->header,
		.packet_ref = true,

		.release = NULL,
		.release_args = NULL
	};

	// the header is sent from the packet itself, the literal does not zero its padding
	packet_header_init (&packet->header, packet_type, sizeof (PacketHeader), request_type);

}

// initializes an already allocated packet as a ping request
void packet_init_ping (Packet *packet) {

	packet_init_request (packet, PACKET_TYPE_TEST, 0);

}

// generates a simple request packet of the requested type reday to be sent,
// and with option to pass some data
Packet *packet_generate_request (
	const PacketType packet_type, const u32 req_type,
	const void *data, const size_t data_size
) {

	Packet *packet = packet_new ();
	if (packet) {
		packet->packet_type = packet_type;
		packet->req_type = req_type;

		// if there is data, append it to the packet data buffer
		if (data) {
			if (packet_append_data (packet, data, data_size)) {
				// we failed to appedn the data into the packet
				packet_delete (packet);
				packet = NULL;
			}
		}

		// make the packet ready to be sent, and check for errors
		if (packet_generate (packet)) {
			packet_delete (packet);
			packet = NULL;
		}
	}

	return packet;

}

static inline u8 packet_send_tcp_actual (
	const Packet *packet,
	Connection *connection,
	int flags, size_t *total_sent, bool raw
) {

	// every byte that was sent, send () can send the packet in many parts
	return socket_send_buffer (
		connection->socket,
		raw ? packet->data : packet->packet,
		raw ? packet->data_size : packet->packet_size,
		flags, (time_t) connection->send_timeout,
		total_sent, NULL
	);

}

// sends a packet directly using the tcp protocol and the packet sock fd
// returns 0 on success, 1 on error
static u8 packet_send_tcp (
	const Packet *packet,
	Connection *connection,
	int flags, size_t *total_sent, bool raw
) {

	u8 retval = 1;

	(void) pthread_mutex_lock (connection->socket->write_mutex);

	retval = packet_send_tcp_actual (
		packet, connection, flags, total_sent, raw
	);

	(void) pthread_mutex_unlock (connection->socket->write_mutex);

	return retval;

}

// sends a packet to the socket in two parts, first the header & then the data
// returns 0 on success, 1 on error
static u8 packet_send_split_tcp (
	const Packet *packet,
	Connection *connection,
	int flags, size_t *total_sent
) {

	u8 retval = 1;

	if (packet && connection) {
		// the header and the data in a single call when the socket takes them
		struct iovec parts[PACKET_SPLIT_PARTS] = {
			{ .iov_base = (void *) &packet->header, .iov_len = sizeof (PacketHeader) },
			{ .iov_base = packet->data, .iov_len = packet->data_size }
		};

		(void) pthread_mutex_lock (connection->socket->write_mutex);

		retval = socket_send_vector (
			connection->socket,
			parts, (packet->data && packet->data_size) ? PACKET_SPLIT_PARTS : 1,
			flags, (time_t) connection->send_timeout,
			total_sent, NULL
		);

		(void) pthread_mutex_unlock (connection->socket->write_mutex);
	}

	return retval;

}

// the connection of a peer of an UDP cerver has no socket of its own,
// its packets are sent with the cerver's socket to the address of the peer
static i32 packet_send_udp_sock_fd (const Cerver *cerver, const Connection *connection) {

	i32 sock_fd = connection->socket ? connection->socket->sock_fd : -1;
	if ((sock_fd < 0) && cerver) sock_fd = cerver->sock;

	return sock_fd;

}

// the connection of a client has its own socket, that is connected to the cerver
static bool packet_send_udp_is_connected (const Connection *connection) {

	return connection->socket && (connection->socket->sock_fd > -1);

}

// the parts of the packet that go in the datagram:
// only its data (raw), its header and its data (split), or the generated packet
// returns the number of parts
static int packet_send_udp_parts (
	const Packet *packet, bool raw, bool split, struct iovec *parts
) {

	int count = 0;

	if (raw) {
		parts[count++] = (struct iovec) { .iov_base = packet->data, .iov_len = packet->data_size };
	}

	else if (split) {
		parts[count++] = (struct iovec) { .iov_base = (void *) &packet->header, .iov_len = sizeof (PacketHeader) };

		if (packet->data && packet->data_size) {
			parts[count++] = (struct iovec) { .iov_base = packet->data, .iov_len = packet->data_size };
		}
	}

	else {
		parts[count++] = (struct iovec) { .iov_base = packet->packet, .iov_len = packet->packet_size };
	}

	return count;

}

static size_t packet_send_udp_size (const struct iovec *parts, const int count) {

	size_t size = 0;
	for (int i = 0; i < count; i++) size += parts[i].iov_len;

	return size;

}

// sends the packet in a single datagram to the address of the connection,
// followed by the token of the connection when it has one and with_token is set
// a packet that does not fit in a datagram is not sent
// returns 0 on success, 1 on error
static u8 packet_send_udp (
	const Packet *packet,
	const Cerver *cerver, const Connection *connection,
	int flags, size_t *total_sent,
	bool raw, bool split, bool with_token
) {

	u8 retval = 1;

	struct iovec parts[PACKET_DATAGRAM_MAX_PARTS];
	int count = packet_send_udp_parts (packet, raw, split, parts);

	u8 token[CONNECTION_DATAGRAM_TOKEN_SIZE];
	if (with_token && connection_get_datagram_token (connection, token)) {
		parts[count++] = (struct iovec) { .iov_base = token, .iov_len = CONNECTION_DATAGRAM_TOKEN_SIZE };
	}

	const size_t size = packet_send_udp_size (parts, count);
	const i32 sock_fd = packet_send_udp_sock_fd (cerver, connection);

	if (
		connection->socket && parts[0].iov_base && size
		&& (size <= MAX_UDP_PACKET_SIZE) && (sock_fd > -1)
	) {
		struct msghdr message;
		(void) memset (&message, 0, sizeof (struct msghdr));
		message.msg_iov = parts;
		message.msg_iovlen = (size_t) count;

		// the address of a peer changes when it migrates, the write mutex protects it
		(void) pthread_mutex_lock (connection->socket->write_mutex);

		// a connected socket does not take an address, macOS fails with EISCONN
		if (!packet_send_udp_is_connected (connection)) {
			message.msg_name = (void *) &connection->address;
			message.msg_namelen = sock_address_size (&connection->address);
		}

		const ssize_t sent = sendmsg (sock_fd, &message, flags | MSG_NOSIGNAL);

		(void) pthread_mutex_unlock (connection->socket->write_mutex);

		if (sent == (ssize_t) size) {
			if (total_sent) *total_sent = size;
			retval = 0;
		}
	}

	return retval;

}

static void packet_send_update_stats (
	PacketType packet_type, size_t sent,
	Cerver *cerver,
	Client *client, Connection *connection,
	Lobby *lobby
) {

	// they are only used with CLIENT_STATS and CONNECTION_STATS
	(void) client;
	(void) connection;

	if (cerver) {
		CERVER_STATS_ADD (cerver->stats->n_packets_sent, 1);
		CERVER_STATS_ADD (cerver->stats->total_bytes_sent, sent);
	}

	#ifdef CLIENT_STATS
	if (client) {
		CERVER_STATS_ADD (client->stats->n_packets_sent, 1);
		CERVER_STATS_ADD (client->stats->total_bytes_sent, sent);
	}
	#endif

	#ifdef CONNECTION_STATS
	connection->stats->n_packets_sent += 1;
	connection->stats->total_bytes_sent += sent;
	#endif

	if (lobby) {
		lobby->stats->n_packets_sent += 1;
		lobby->stats->bytes_sent += sent;
	}

	switch (packet_type) {
		case PACKET_TYPE_NONE: break;

		case PACKET_TYPE_CERVER:
			if (cerver) CERVER_STATS_ADD (cerver->stats->sent_packets->n_cerver_packets, 1);
			#ifdef CLIENT_STATS
			if (client) client->stats->sent_packets->n_cerver_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			connection->stats->sent_packets->n_cerver_packets += 1;
			#endif
			if (lobby) lobby->stats->sent_packets->n_cerver_packets += 1;
			break;

		case PACKET_TYPE_CLIENT:
			if (cerver) CERVER_STATS_ADD (cerver->stats->sent_packets->n_client_packets, 1);
			#ifdef CLIENT_STATS
			if (client) client->stats->sent_packets->n_client_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			connection->stats->sent_packets->n_client_packets += 1;
			#endif
			if (lobby) lobby->stats->sent_packets->n_client_packets += 1;
			break;

		case PACKET_TYPE_ERROR:
			if (cerver) CERVER_STATS_ADD (cerver->stats->sent_packets->n_error_packets, 1);
			#ifdef CLIENT_STATS
			if (client) client->stats->sent_packets->n_error_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			connection->stats->sent_packets->n_error_packets += 1;
			#endif
			if (lobby) lobby->stats->sent_packets->n_error_packets += 1;
			break;

		case PACKET_TYPE_REQUEST:
			if (cerver) CERVER_STATS_ADD (cerver->stats->sent_packets->n_request_packets, 1);
			#ifdef CLIENT_STATS
			if (client) client->stats->sent_packets->n_request_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			connection->stats->sent_packets->n_request_packets += 1;
			#endif
			if (lobby) lobby->stats->sent_packets->n_request_packets += 1;
			break;

		case PACKET_TYPE_AUTH:
			if (cerver) CERVER_STATS_ADD (cerver->stats->sent_packets->n_auth_packets, 1);
			#ifdef CLIENT_STATS
			if (client) client->stats->sent_packets->n_auth_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			connection->stats->sent_packets->n_auth_packets += 1;
			#endif
			if (lobby) lobby->stats->sent_packets->n_auth_packets += 1;
			break;

		case PACKET_TYPE_GAME:
			if (cerver) CERVER_STATS_ADD (cerver->stats->sent_packets->n_game_packets, 1);
			#ifdef CLIENT_STATS
			if (client) client->stats->sent_packets->n_game_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			connection->stats->sent_packets->n_game_packets += 1;
			#endif
			if (lobby) lobby->stats->sent_packets->n_game_packets += 1;
			break;

		case PACKET_TYPE_APP:
			if (cerver) CERVER_STATS_ADD (cerver->stats->sent_packets->n_app_packets, 1);
			#ifdef CLIENT_STATS
			if (client) client->stats->sent_packets->n_app_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			connection->stats->sent_packets->n_app_packets += 1;
			#endif
			if (lobby) lobby->stats->sent_packets->n_app_packets += 1;
			break;

		case PACKET_TYPE_APP_ERROR:
			if (cerver) CERVER_STATS_ADD (cerver->stats->sent_packets->n_app_error_packets, 1);
			#ifdef CLIENT_STATS
			if (client) client->stats->sent_packets->n_app_error_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			connection->stats->sent_packets->n_app_error_packets += 1;
			#endif
			if (lobby) lobby->stats->sent_packets->n_app_error_packets += 1;
			break;

		case PACKET_TYPE_CUSTOM:
			if (cerver) CERVER_STATS_ADD (cerver->stats->sent_packets->n_custom_packets, 1);
			#ifdef CLIENT_STATS
			if (client) client->stats->sent_packets->n_custom_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			connection->stats->sent_packets->n_custom_packets += 1;
			#endif
			if (lobby) lobby->stats->sent_packets->n_custom_packets += 1;
			break;

		case PACKET_TYPE_TEST:
			if (cerver) CERVER_STATS_ADD (cerver->stats->sent_packets->n_test_packets, 1);
			#ifdef CLIENT_STATS
			if (client) client->stats->sent_packets->n_test_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			connection->stats->sent_packets->n_test_packets += 1;
			#endif
			if (lobby) lobby->stats->sent_packets->n_test_packets += 1;
			break;

		default:
			if (cerver) CERVER_STATS_ADD (cerver->stats->sent_packets->n_unknown_packets, 1);
			#ifdef CLIENT_STATS
			if (client) client->stats->sent_packets->n_unknown_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			connection->stats->sent_packets->n_unknown_packets += 1;
			#endif
			if (lobby) lobby->stats->sent_packets->n_unknown_packets += 1;
			break;
	}

}

// sends a packet of a connection's send queue,
// the socket's write mutex is held so the packet is not mixed with others
// returns 0 on success, 1 on error
u8 packet_send_actual (
	const Packet *packet,
	int flags, size_t *total_sent,
	Client *client, Connection *connection
) {

	u8 retval = 1;

	size_t sent = 0;
	const u8 failed = (connection->protocol == PROTOCOL_UDP)
		? packet_send_udp (packet, NULL, connection, flags, &sent, false, false, true)
		: packet_send_tcp (packet, connection, flags, &sent, false);

	if (!failed) {
		if (total_sent) *total_sent = sent;

		packet_send_update_stats (
			packet->packet_type, sent,
			NULL, client, connection, NULL
		);

		retval = 0;
	}

	return retval;

}

static void packet_send_failed_stats (
	Cerver *cerver, Client *client, Connection *connection,
	size_t *total_sent
) {

	// they are only used with CLIENT_STATS and CONNECTION_STATS
	(void) client;
	(void) connection;

	#ifdef PACKETS_DEBUG
	printf ("\n");
	perror ("packet_send_internal () - Error");
	printf ("\n");
	#endif

	if (cerver) CERVER_STATS_ADD (cerver->stats->sent_packets->n_bad_packets, 1);

	#ifdef CLIENT_STATS
	if (client) client->stats->sent_packets->n_bad_packets += 1;
	#endif

	#ifdef CONNECTION_STATS
	if (connection) connection->stats->sent_packets->n_bad_packets += 1;
	#endif

	if (total_sent) *total_sent = 0;

}

static inline u8 packet_send_internal (
	const Packet *packet,
	int flags, size_t *total_sent,
	bool raw, bool split, bool unsafe,
	Cerver *cerver,
	Client *client, Connection *connection,
	Lobby *lobby
) {

	u8 retval = 1;

	if (packet && connection) {
		switch (connection->protocol) {
			case PROTOCOL_TCP: {
				size_t sent = 0;

				if (!(split ? packet_send_split_tcp (packet, connection, flags, &sent)
					: unsafe ? packet_send_tcp_actual (packet, connection, flags, &sent, raw) 
						: packet_send_tcp (packet, connection, flags, &sent, raw))
				) {
					if (total_sent) *total_sent = sent;

					packet_send_update_stats (
						packet->packet_type, sent,
						cerver, client, connection, lobby
					);

					retval = 0;
				}

				else {
					packet_send_failed_stats (cerver, client, connection, total_sent);
				}
			} break;

			case PROTOCOL_UDP: {
				size_t sent = 0;

				if (!packet_send_udp (packet, cerver, connection, flags, &sent, raw, split, true)) {
					if (total_sent) *total_sent = sent;

					packet_send_update_stats (
						packet->packet_type, sent,
						cerver, client, connection, lobby
					);

					retval = 0;
				}

				else {
					packet_send_failed_stats (cerver, client, connection, total_sent);
				}
			} break;

			default: break;
		}
	}

	return retval;

}

// sends a packet using its network values
// raw flag to send a raw packet (only the data that was set to the packet, without any header)
// returns 0 on success, 1 on error
u8 packet_send (
	const Packet *packet, int flags, size_t *total_sent, bool raw
) {

	return packet_send_internal (
		packet, flags, total_sent,
		raw, false, false,
		packet->cerver, packet->client, packet->connection, packet->lobby
	);

}

// sends the packet in a datagram without the token of its UDP connection,
// like the answers to an authentication, that the peer reads before it has the token
// returns 0 on success, 1 on error
u8 packet_send_without_datagram_token (const Packet *packet) {

	u8 retval = 1;

	if (packet && packet->connection) {
		size_t sent = 0;
		if (!packet_send_udp (
			packet, packet->cerver, packet->connection,
			0, &sent, false, false, false
		)) {
			packet_send_update_stats (
				packet->packet_type, sent,
				packet->cerver, packet->client, packet->connection, packet->lobby
			);

			retval = 0;
		}

		else {
			packet_send_failed_stats (packet->cerver, packet->client, packet->connection, NULL);
		}
	}

	return retval;

}

// works just as packet_send () but the socket's write mutex won't be locked
// useful when you need to lock the mutex manually
// returns 0 on success, 1 on error
u8 packet_send_unsafe (
	const Packet *packet, int flags, size_t *total_sent, bool raw
) {

	return packet_send_internal (
		packet, flags, total_sent,
		raw, false, true,
		packet->cerver, packet->client, packet->connection, packet->lobby
	);

}

// sends a packet to the specified destination
// sets flags to 0
// at least a packet & an active connection are required for this method to succeed
// raw flag to send a raw packet (only the data that was set to the packet, without any header)
// returns 0 on success, 1 on error
u8 packet_send_to (
	const Packet *packet,
	size_t *total_sent, bool raw,
	Cerver *cerver,
	Client *client, Connection *connection,
	Lobby *lobby
) {

	return packet_send_internal (
		packet, 0, total_sent,
		raw, false, false,
		cerver, client, connection, lobby
	);

}

// sends a packet to the socket in two parts, first the header & then the data
// this method can be useful when trying to forward a big received packet without the overhead of
// performing and additional copy to create a continuos data (packet) buffer
// the socket's write mutex will be locked to ensure that the packet
// is sent correctly and to avoid race conditions
// returns 0 on success, 1 on error
u8 packet_send_split (
	const Packet *packet, int flags, size_t *total_sent
) {

	return packet_send_internal (
		packet, flags, total_sent,
		false, true, false,
		packet->cerver, packet->client, packet->connection, packet->lobby
	);

}

// sends a packet to the socket in two parts, first the header & then the data
// works just as packet_send_split () but with the flags set to 0
// returns 0 on success, 1 on error
u8 packet_send_to_split (
	const Packet *packet,
	size_t *total_sent,
	Cerver *cerver,
	Client *client, Connection *connection,
	Lobby *lobby
) {

	return packet_send_internal (
		packet, 0, total_sent,
		false, true, false,
		cerver, client, connection, lobby
	);

}

static u8 packet_send_pieces_actual (
	Connection *connection,
	char *data, size_t data_size,
	int flags,
	size_t *actual_sent
) {

	size_t sent = 0;

	const u8 retval = socket_send_buffer (
		connection->socket, data, data_size,
		flags, (time_t) connection->send_timeout,
		&sent, NULL
	);

	*actual_sent += sent;

	return retval;

}

// sends a packet in pieces, taking the header from the packet's field
// sends each buffer as they are with they respective sizes
// socket mutex will be locked for the entire operation
// returns 0 on success, 1 on error
u8 packet_send_pieces (
	const Packet *packet,
	void **pieces, size_t *sizes, u32 n_pieces,
	int flags,
	size_t *total_sent
) {

	u8 retval = 1;

	if (packet && pieces && sizes) {
		(void) pthread_mutex_lock (packet->connection->socket->write_mutex);

		size_t actual_sent = 0;

		// first send the header
		if (!packet_send_pieces_actual (
			packet->connection,
			(char *) &packet->header, sizeof (PacketHeader),
			flags,
			&actual_sent
		)) {
			// send the pieces of data, a failed one stops the packet
			retval = 0;
			for (u32 i = 0; (i < n_pieces) && !retval; i++) {
				retval = packet_send_pieces_actual (
					packet->connection,
					(char *) pieces[i], sizes[i],
					flags,
					&actual_sent
				);
			}
		}

		packet_send_update_stats (
			packet->packet_type, actual_sent,
			packet->cerver, packet->client, packet->connection, packet->lobby
		);

		if (total_sent) *total_sent = actual_sent;

		(void) pthread_mutex_unlock (packet->connection->socket->write_mutex);
	}

	return retval;

}

// sends a packet directly to the socket
// raw flag to send a raw packet (only the data that was set to the packet, without any header)
// returns 0 on success, 1 on error
u8 packet_send_to_socket (
	const Packet *packet,
	Socket *socket, int flags, size_t *total_sent, bool raw
) {

	u8 retval = 0;

	if (packet && socket) {
		(void) pthread_mutex_lock (socket->write_mutex);

		// a socket without its connection waits the default send timeout
		retval = socket_send_buffer (
			socket,
			raw ? packet->data : packet->packet,
			raw ? packet->data_size : packet->packet_size,
			flags, (time_t) CERVER_DEFAULT_SEND_TIMEOUT,
			total_sent, NULL
		);

		(void) pthread_mutex_unlock (socket->write_mutex);
	}

	return retval;

}

// sends a packet of selected types without any data
// returns 0 on success, 1 on error
u8 packet_send_request (
	const PacketType packet_type,
	const u32 request_type,
	Cerver *cerver,
	Client *client, Connection *connection,
	Lobby *lobby
) {

	u8 retval = 1;

	Packet request = {
		.cerver = cerver,
		.client = client,
		.connection = connection,
		.lobby = lobby,

		.packet_type = packet_type,
		.req_type = request_type,

		.data_size = 0,
		.data = NULL,
		.data_ptr = NULL,
		.data_end = NULL,
		.data_ref = false,

		.header = (PacketHeader) {
			.packet_type = packet_type,
			.packet_size = sizeof (PacketHeader),

			.handler_id = 0,

			.request_type = request_type,

			.sock_fd = 0,

			.version = packet_version_local ()
		},

		.version = packet_version_local (),

		.packet_size = sizeof (PacketHeader),
		.packet = (void *) &request.header,
		.packet_ref = true,

		.release = NULL,
		.release_args = NULL
	};

	// the header is sent from the packet itself, the literal does not zero its padding
	packet_header_init (&request.header, packet_type, sizeof (PacketHeader), request_type);

	size_t sent = 0;
	if (!packet_send (&request, 0, &sent, false)) {
		if (sent == sizeof (PacketHeader)) {
			retval = 0;
		}
	}

	return retval;

}

// sends a ping packet (PACKET_TYPE_TEST)
// returns 0 on success, 1 on error
u8 packet_send_ping (
	Cerver *cerver,
	Client *client, Connection *connection,
	Lobby *lobby
) {

	return packet_send_request (
		PACKET_TYPE_TEST, 0,
		cerver,
		client, connection,
		lobby
	);

}

static inline u8 packet_route_between_connections_receive (
	int from_fd, int pipefd, int buff_size,
	ssize_t *received
) {

	u8 retval = 1;

	*received = splice (
		from_fd, NULL,
		pipefd, NULL,
		buff_size,
		SPLICE_F_MOVE | SPLICE_F_MORE
	);

	switch (*received) {
		case -1: {
			#ifdef PACKETS_DEBUG
			perror (
				"packet_route_between_connections_receive () - "
				"splice () = -1"
			);
			#endif
		} break;

		case 0: {
			#ifdef PACKETS_DEBUG
			perror (
				"packet_route_between_connections_receive () - "
				"splice () = 0"
			);
			#endif
		} break;

		default: {
			#ifdef PACKETS_DEBUG
			cerver_log_debug (
				"packet_route_between_connections_receive () - "
				"spliced %ld bytes", *received
			);
			#endif

			retval = 0;
		} break;
	}

	return retval;

}


static inline u8 packet_route_between_connections_move (
	int pipefd, int to_fd, int buff_size,
	ssize_t *moved
) {

	u8 retval = 1;

	*moved = splice (
		pipefd, NULL,
		to_fd, NULL,
		buff_size,
		SPLICE_F_MOVE | SPLICE_F_MORE
	);

	switch (*moved) {
		case -1: {
			#ifdef PACKETS_DEBUG
			perror (
				"packet_route_between_connections_move () - "
				"splice () = -1"
			);
			#endif
		} break;

		case 0: {
			#ifdef PACKETS_DEBUG
			perror (
				"packet_route_between_connections_move () - "
				"splice () = 0"
			);
			#endif
		} break;

		default: {
			#ifdef PACKETS_DEBUG
			cerver_log_debug (
				"packet_route_between_connections_move () - "
				"spliced %ld bytes", *moved
			);
			#endif

			retval = 0;
		} break;
	}

	return retval;

}

// moves the pending bytes from the pipe to the destination,
// splice () can move less bytes than the ones in the pipe
static u8 packet_route_between_connections_move_all (
	int pipefd, int to_fd, size_t pending, size_t *sent
) {

	u8 retval = 0;

	ssize_t moved = 0;
	while (pending > 0) {
		if (packet_route_between_connections_move (pipefd, to_fd, (int) pending, &moved)) {
			retval = 1;
			break;
		}

		if (sent) *sent += (size_t) moved;
		pending -= (size_t) moved;
	}

	return retval;

}

// routes a packet from one connection's sock fd to another connection's sock fd
// the header is sent first and then the packet's body (if any) is handled directly between fds
// by calling the splice method using a pipe as the middleman
// this method is thread safe, since it will block the socket until the entire packet has been routed
// returns 0 on success, 1 on error
u8 packet_route_between_connections (
	Connection *from, Connection *to,
	PacketHeader *header, size_t *sent
) {

	u8 retval = 1;

	if (from && to && header) {
		(void) pthread_mutex_lock (to->socket->write_mutex);

		// first send the header
		ssize_t s = send (to->socket->sock_fd, header, sizeof (PacketHeader), 0);
		if (s > 0) {
			if (sent) *sent = (size_t) s;

			size_t left = header->packet_size - sizeof (PacketHeader);
			if (left) {
				int pipefds[2] = { 0 };
				if (!pipe (pipefds)) {
					ssize_t received = 0;
					size_t buff_size = PACKET_ROUTE_BUFFER_SIZE;
					while (left > 0) {
						if (buff_size > left) buff_size = left;

						if (packet_route_between_connections_receive (
							from->socket->sock_fd, pipefds[1], buff_size, &received
						)) break;

						// everything that entered the pipe leaves it in this round
						if (packet_route_between_connections_move_all (
							pipefds[0], to->socket->sock_fd, (size_t) received, sent
						)) break;

						left -= (size_t) received;
					}

					// we are done!
					if (left <= 0) retval = 0;

					(void) close (pipefds[0]);
					(void) close (pipefds[1]);
				}

				else {
					#ifdef PACKETS_DEBUG
					cerver_log_error (
						"packet_route_between_connections () - "
						"pipe () failed!"
					);
					perror ("Error");
					#endif
				}
			}

			else {
				// we are done
				if (sent) *sent = s;
				retval = 0;
			}
		}

		(void) pthread_mutex_unlock (to->socket->write_mutex);
	}

	return retval;

}

// check if packet has a compatible protocol id and a version
// returns false on a bad packet
bool packet_check (const Packet *packet) {

	bool retval = false;

	if (packet) {
		if (packet->version.protocol_id == protocol_id) {
			// the same major, and a minor that is not newer than the local one
			if (
				(packet->version.protocol_version.major == protocol_version.major)
				&& (packet->version.protocol_version.minor <= protocol_version.minor)
			) {
				retval = true;
			}

			else {
				#ifdef PACKETS_DEBUG
				cerver_log (
					LOG_TYPE_WARNING, LOG_TYPE_PACKET,
					"Packet with incompatible version"
				);
				#endif
			}
		}

		else {
			#ifdef PACKETS_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_PACKET,
				"Packet with unknown protocol ID"
			);
			#endif
		}
	}

	return retval;

}

#pragma endregion