/**
 * @file cerver/core/cerver.h
 * @brief The Cerver: its defaults, its struct, its setters and how it is created.
 */

#ifndef _CERVER_CORE_CERVER_H_
#define _CERVER_CORE_CERVER_H_

#include <stdlib.h>

#include <poll.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/htab.h"
#include "cerver/collections/pool.h"

#include "cerver/config.h"

#include "cerver/admin/admin.h"
#include "cerver/core/data.h"
#include "cerver/core/info.h"
#include "cerver/core/types.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/events.h"
#include "cerver/handler/handler.h"
#include "cerver/network/network.h"
#include "cerver/packets/packets.h"
#include "cerver/packets/serializer.h"

#include "cerver/game/game.h"

#include "cerver/threads/thpool.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/histogram.h"

/**
 * @defgroup core Core
 * @brief The Cerver: a server that accepts connections, receives and handles packets and drains its connections
 * when it is torn down.
 *
 * A cerver is created with cerver_create (), configured with its setters, started with cerver_start (), that blocks
 * while it accepts connections, stopped with cerver_shutdown () and released with cerver_teardown ().
 */

/**
 * @defgroup core_defaults Defaults
 * @ingroup core
 * @brief The values that a new cerver starts with, and the limits of its setters.
 * @{
 */

/** @brief The default alias of a cerver. */
#define CERVER_DEFAULT_ALIAS						"my-cerver"

/** @brief The default port of a cerver. */
#define CERVER_DEFAULT_PORT							7000

/** @brief The default protocol of a cerver. */
#define CERVER_DEFAULT_PROTOCOL						PROTOCOL_TCP

/** @brief A new cerver uses IPv4 by default. */
#define CERVER_DEFAULT_USE_IPV6						false

/** @brief An IPv6 cerver also accepts IPv4 connections by default (dual stack). */
#define CERVER_DEFAULT_IPV6_ONLY					false

/** @brief The default backlog of listen (). */
#define CERVER_DEFAULT_CONNECTION_QUEUE				SOMAXCONN

/** @brief The size of a literal IPv4 or IPv6 address with its zone, without the port. */
#define CERVER_BIND_ADDRESS_SIZE					NETWORK_ADDRESS_STRING_SIZE

/** @brief The default size in bytes of the buffer of each recv (). */
#define CERVER_DEFAULT_RECEIVE_BUFFER_SIZE			4096

/** @brief The default size in bytes of the biggest packet that a cerver handles. */
#define CERVER_DEFAULT_MAX_RECEIVED_PACKET_SIZE		MAX_UDP_PACKET_SIZE

/** @brief The socket of a new cerver does not use SO_REUSEADDR by default. */
#define CERVER_DEFAULT_REUSABLE_FLAGS				false

/** @brief The socket of a new cerver does not use SO_REUSEPORT by default. */
#define CERVER_DEFAULT_REUSABLE_PORT				false

/** @brief cerver_start () ignores SIGPIPE by default when the process has not set a handler for it. */
#define CERVER_DEFAULT_IGNORE_SIGPIPE				true

/** @brief The default number of threads of the thpool. */
#define CERVER_DEFAULT_POOL_THREADS					4

/** @brief The default size of the queue of the thpool: 0 uses the one of the thpool (threads * THPOOL_DEFAULT_QUEUE_SIZE_FACTOR). */
#define CERVER_DEFAULT_THPOOL_QUEUE_SIZE			0

/** @brief The default overload policy. */
#define CERVER_DEFAULT_OVERLOAD_POLICY				CERVER_OVERLOAD_POLICY_WAIT

/**
 * @brief The milliseconds that the accept loop waits for space in a full thpool before it checks if the cerver is
 * still running; a free slot wakes it up right away.
 */
#define CERVER_OVERLOAD_WAIT_TIMEOUT				1024

/**
 * @brief The default seconds that a connection of a cerver with CERVER_HANDLER_TYPE_EVENTS waits for a place in a
 * full thpool with CERVER_OVERLOAD_POLICY_WAIT.
 *
 * @since 2.1b-15
 */
#define CERVER_DEFAULT_OVERLOAD_WAIT_TIMEOUT		(1 << 5)

/**
 * @brief The default I/O threads of a cerver with CERVER_HANDLER_TYPE_EVENTS.
 *
 * @since 2.1b-15
 */
#define CERVER_DEFAULT_IO_THREADS					1

/**
 * @brief The default limit of connections of a cerver with CERVER_HANDLER_TYPE_EVENTS.
 *
 * @since 2.1b-15
 */
#define CERVER_DEFAULT_MAX_CONNECTIONS				(1 << 14)

/** @brief The default drain policy. */
#define CERVER_DEFAULT_DRAIN_POLICY					CERVER_DRAIN_POLICY_RUN

/** @brief The default seconds that a drain waits for the connections in progress. */
#define CERVER_DEFAULT_DRAIN_TIMEOUT				32

/** @brief The extra seconds that a drain that timed out waits before it reports what is still alive. */
#define CERVER_DRAIN_GRACE_TIMEOUT					4U

/** @brief The default SO_SNDTIMEO in seconds of the accepted connections, 0 disables it. */
#define CERVER_DEFAULT_SEND_TIMEOUT					32

/**
 * @brief The initial sockets of the pool that was removed.
 *
 * @deprecated The sockets pool was removed and cerver does not use this value anymore.
 */
#define CERVER_DEFAULT_SOCKETS_INIT					10

/** @brief The default initial size of the array of the main poll, that grows when it is full. */
#define CERVER_DEFAULT_POLL_FDS						128

/** @brief The default timeout in milliseconds of the main poll. */
#define CERVER_DEFAULT_POLL_TIMEOUT					2000

/** @brief The default max connections of the main poll: 0 is no limit. */
#define CERVER_DEFAULT_POLL_MAX_CONNECTIONS			0

/** @brief The smallest main poll: the socket of the cerver and at least one connection. */
#define CERVER_POLL_MIN_FDS							2

/** @brief The default seconds that a client can be inactive before it is dropped. */
#define CERVER_DEFAULT_MAX_INACTIVE_TIME			60

/** @brief The default seconds between two checks of the inactive clients. */
#define CERVER_DEFAULT_CHECK_INACTIVE_INTERVAL		30

/** @brief A new cerver does not require authentication by default. */
#define CERVER_DEFAULT_AUTH_REQUIRED				false

/** @brief The default failed authentications that a new connection can have before it is dropped. */
#define CERVER_DEFAULT_MAX_AUTH_TRIES				2

/** @brief The default connections that the on hold poll handles at the same time. */
#define CERVER_DEFAULT_ON_HOLD_POLL_FDS				64

/** @brief The default timeout in milliseconds of the on hold poll. */
#define CERVER_DEFAULT_ON_HOLD_TIMEOUT				2000

/** @brief The default bad packets that an on hold connection can send before it is dropped. */
#define CERVER_DEFAULT_ON_HOLD_MAX_BAD_PACKETS		4

/** @brief The packets of the on hold connections are not checked with packet_check () by default. */
#define CERVER_DEFAULT_ON_HOLD_CHECK_PACKETS		false

/** @brief The default size in bytes of the buffer that receives from the on hold connections. */
#define CERVER_DEFAULT_ON_HOLD_RECEIVE_BUFFER_SIZE	4096

/** @brief The default seconds that a new connection has to authenticate. */
#define CERVER_DEFAULT_ON_HOLD_SECONDS				16

/** @brief The default bad packets that an authenticated connection can send before it is dropped. */
#define CERVER_DEFAULT_MAX_BAD_PACKETS				16

/** @brief A new cerver does not use sessions by default. */
#define CERVER_DEFAULT_USE_SESSIONS					false

/** @brief A new cerver has a single app handler by default. */
#define CERVER_DEFAULT_MULTIPLE_HANDLERS			false

/** @brief The packets are not checked with packet_check () by default. */
#define CERVER_DEFAULT_CHECK_PACKETS				false

/** @brief The default ticks per second of the update method. */
#define CERVER_DEFAULT_UPDATE_TICKS					30

/** @brief The default seconds between two calls of the update interval method. */
#define CERVER_DEFAULT_UPDATE_INTERVAL_SECONDS		1

/** @brief The SO_RCVTIMEO in seconds of a connection that is handled in its own thread. */
#define CERVER_DEFAULT_SOCKET_RECEIVE_TIMEOUT		5

/** @} */

#ifdef __cplusplus
extern "C" {
#endif

struct _Balancer;
struct _Cerver;
struct _AdminCerver;
struct _Client;
struct _Connection;
struct _Packet;
struct _PacketsPerType;
struct _Handler;

/**
 * @defgroup core_main Cerver
 * @ingroup core
 * @brief The struct of a cerver and its setters.
 *
 * The setters take effect before cerver_start () unless they say otherwise, and they ignore a NULL cerver.
 * @{
 */

/**
 * @brief A cerver: the generic struct that every type of server uses.
 *
 * The fields are read by the parts of cerver; an application sets them with the setters and reads them with the
 * getters.
 */
struct _Cerver {

	/** @brief The type of the cerver. */
	CerverType type;

	/** @brief The socket of the cerver, -1 before it starts. */
	i32 sock;
	/** @brief The address that the socket is bound to. */
	struct sockaddr_storage address;

	/** @brief The port of the cerver. */
	u16 port;
	/** @brief The protocol of the cerver: TCP or UDP. */
	Protocol protocol;
	/** @brief The cerver uses IPv6. */
	bool use_ipv6;
	/** @brief The backlog of listen (). */
	u16 connection_queue;

	/** @brief The address that the cerver binds, empty for every address of its family. */
	char bind_address[CERVER_BIND_ADDRESS_SIZE];
	/** @brief An IPv6 cerver accepts IPv4 connections when it is false (dual stack). */
	bool ipv6_only;

	/** @brief The cerver is accepting connections; read and written with __atomic, use cerver_is_running (). */
	bool running;
	/** @brief A stop was requested; read and written with __atomic, a stop requested before cerver_start () cancels it. */
	bool stop_requested;
	/** @brief The socket of the cerver is blocking. */
	bool blocking;
	/** @brief The socket of the cerver uses SO_REUSEADDR. */
	bool reusable;
	/** @brief The socket of the cerver uses SO_REUSEPORT. */
	bool reusable_port;
	/** @brief cerver_start () ignores SIGPIPE when the process has not set a handler for it. */
	bool ignore_sigpipe;

	/** @brief The data of the type of the cerver, or the one of the application. */
	void *cerver_data;
	/** @brief Releases the cerver data, free () when it is NULL. */
	Action delete_cerver_data;

	/** @brief The number of threads of the thpool. */
	u16 n_thpool_threads;
	/** @brief The connections that can wait in the queue of the thpool, 0 for the default of the thpool. */
	unsigned int thpool_queue_size;
	/** @brief The thpool, created by cerver_start () when the cerver uses it. */
	Thpool *thpool;

	/** @brief What happens with a new connection when the queue of the thpool is full. */
	CerverOverloadPolicy overload_policy;

	/** @brief What happens with the queued connections when the cerver is drained. */
	CerverDrainPolicy drain_policy;
	/** @brief The seconds that the drain waits for the connections, 0 waits until all of them end. */
	u32 drain_timeout;
	/** @brief Every connection finished before the drain timeout. */
	bool drained;

	/** @brief The SO_SNDTIMEO in seconds of the accepted connections, 0 disables it. */
	u32 send_timeout;

	/**
	 * @brief The pipe that wakes up the threads of the cerver that sleep when the cerver stops: the stop writes a
	 * byte in its write end, which is async-signal-safe, and the sleeps poll its read end; -1 if it could not be
	 * created (the sleeps then wait their whole time). With CERVER_HANDLER_TYPE_EVENTS, cerver_start () waits on
	 * its read end too.
	 * @since 1.9b-2
	 */
	i32 stop_pipe[2];

	/** @brief The connected clients. */
	AVLTree *clients;
	/** @brief The clients indexed by the socket of their connections. */
	Htab *client_sock_fd_map;

	/** @brief The clients that have not sent or received a packet in #max_inactive_time are dropped. */
	bool inactive_clients;
	/** @brief The seconds that a client can be inactive. */
	u32 max_inactive_time;
	/** @brief The seconds between two checks of the inactive clients. */
	u32 check_inactive_interval;
	/** @brief The thread that checks the inactive clients. */
	pthread_t inactive_thread_id;

	/** @brief How the cerver handles its TCP connections. */
	CerverHandlerType handler_type;

	/**
	 * @brief With CERVER_HANDLER_TYPE_THREADS, each connection is handled in a new detachable thread instead of the
	 * thpool.
	 *
	 * When it is false the thpool is used, and the overload policy applies when it is full; a cerver without a
	 * thpool creates a detachable thread anyway. Web cervers always use the thpool.
	 */
	bool handle_detachable_threads;

	/** @brief The detachable connection threads, the teardown waits for them while it drains. */
	ThreadGroup connection_threads;

	/** @brief The threads of the events and errors actions, the teardown waits for them before the cerver is released. */
	ThreadGroup event_threads;

	/** @brief The peers of an UDP cerver (cerver/network/udp.h), created when it starts. */
	struct CerverUdp *udp;
	/** @brief The seconds that an UDP peer can be inactive. */
	u32 udp_peer_timeout;
	/** @brief The max UDP peers of the cerver. */
	u32 udp_max_peers;
	/** @brief The current UDP peers, read with __atomic by cerver_udp_peers_count (). */
	u32 udp_peers_count;

	/** @brief The array of the main poll. */
	struct pollfd *fds;
	/** @brief The current size of the array of the main poll. */
	u32 max_n_fds;
	/** @brief The active fds in the array of the main poll. */
	u32 current_n_fds;
	/** @brief The connections that the main poll handles, 0 is no limit. */
	u32 poll_max_connections;
	/** @brief The timeout in milliseconds of the main poll. */
	u32 poll_timeout;
	/** @brief Protects the array of the main poll. */
	pthread_mutex_t *poll_lock;

	/**
	 * @brief The cerver requires its clients to authenticate: the info of the cerver says so, and a connection
	 * without auth data stays on hold until its timeout.
	 */
	bool auth_required;
	/** @brief The failed authentications that a new connection can have before it is dropped. */
	u8 max_auth_tries;
	/** @brief The method of the application that authenticates a connection. */
	delegate authenticate;

	/**
	 * @brief The schema of the credentials, the authenticate method gets the decoded object; NULL takes the bytes
	 * of the credentials as they come.
	 */
	const SerializerSchema *auth_schema;

	/** @brief The connections that have not authenticated yet. */
	AVLTree *on_hold_connections;
	/** @brief The on hold connections indexed by their socket. */
	Htab *on_hold_connection_sock_fd_map;
	/** @brief The array of the on hold poll. */
	struct pollfd *hold_fds;
	/** @brief The timeout in milliseconds of the on hold poll. */
	u32 on_hold_poll_timeout;
	/** @brief The connections that the on hold poll handles at the same time. */
	u32 max_on_hold_connections;
	/** @brief The active fds in the array of the on hold poll. */
	u16 current_on_hold_nfds;
	/** @brief The thread of the on hold poll. */
	pthread_t on_hold_poll_thread_id;
	/** @brief Protects the array of the on hold poll. */
	pthread_mutex_t *on_hold_poll_lock;
	/** @brief The bad packets that an on hold connection can send before it is dropped. */
	u8 on_hold_max_bad_packets;
	/** @brief The packets of the on hold connections are checked with packet_check (). */
	bool on_hold_check_packets;
	/** @brief The size in bytes of the buffer that receives from the on hold connections. */
	size_t on_hold_receive_buffer_size;
	/** @brief The seconds that a new connection has to authenticate before it is dropped, 0 disables it. */
	u32 on_hold_timeout;

	/** @brief The clients use sessions: a client can have many connections. */
	bool use_sessions;
	/** @brief Generates the id of a session from a SessionData, a string allocated with malloc (). */
	void *(*session_id_generator) (const void *);

	/** @brief Handles the buffer that each recv () received; the default one reads the cerver packets. */
	Action handle_received_buffer;

	/** @brief The size in bytes of the buffer of each recv (). */
	size_t receive_buffer_size;
	/** @brief The size in bytes of the biggest packet that the cerver handles. */
	size_t max_received_packet_size;

	/** @brief The handler of the PACKET_TYPE_APP packets. */
	struct _Handler *app_packet_handler;
	/** @brief The handler of the PACKET_TYPE_APP_ERROR packets. */
	struct _Handler *app_error_packet_handler;
	/** @brief The handler of the PACKET_TYPE_CUSTOM packets. */
	struct _Handler *custom_packet_handler;

	/** @brief The cerver deletes the PACKET_TYPE_APP packets after their handler. */
	bool app_packet_handler_delete_packet;
	/** @brief The cerver deletes the PACKET_TYPE_APP_ERROR packets after their handler. */
	bool app_error_packet_handler_delete_packet;
	/** @brief The cerver deletes the PACKET_TYPE_CUSTOM packets after their handler. */
	bool custom_packet_handler_delete_packet;

	/** @brief The PACKET_TYPE_APP packets go to the handler of the handler id of their header. */
	bool multiple_handlers;
	/** @brief The app handlers, indexed by their id. */
	struct _Handler **handlers;
	/** @brief The size of #handlers. */
	unsigned int n_handlers;

	/** @brief The handlers of each request type, [packet type][request type], from cerver_set_request_handler (). */
	struct _Handler **request_handlers[CERVER_REQUEST_PACKET_TYPES];
	/** @brief The size of each array of #request_handlers. */
	u32 request_handlers_size[CERVER_REQUEST_PACKET_TYPES];

	/** @brief The handlers that are alive (ready to handle packets). */
	unsigned int num_handlers_alive;
	/** @brief The handlers that are handling a packet, counted with __atomic. */
	unsigned int num_handlers_working;
	/** @brief Protects #num_handlers_alive. */
	pthread_mutex_t *handlers_lock;

	/** @brief Signaled with the handlers lock when a handler marks itself alive. */
	pthread_cond_t *handlers_alive_cond;

	/** @brief The packets are checked with packet_check (). */
	bool check_packets;
	/** @brief The bad packets that an authenticated connection can send before it is dropped, 0 disables it. */
	u8 max_bad_packets;

	/** @brief The thread of the update method. */
	pthread_t update_thread_id;
	/** @brief The method that runs every tick, it gets a CerverUpdate. */
	Action update;
	/** @brief The arguments of the update method. */
	void *update_args;
	/** @brief Releases the arguments of the update method when the cerver is torn down. */
	void (*delete_update_args)(void *);
	/** @brief The ticks per second of the update method. */
	u8 update_ticks;

	/** @brief The thread of the update interval method. */
	pthread_t update_interval_thread_id;
	/** @brief The method that runs every #update_interval_secs, it gets a CerverUpdate. */
	Action update_interval;
	/** @brief The arguments of the update interval method. */
	void *update_interval_args;
	/** @brief Releases the arguments of the update interval method when the cerver is torn down. */
	void (*delete_update_interval_args)(void *);
	/** @brief The seconds between two calls of the update interval method. */
	u32 update_interval_secs;

	/** @brief The admin cerver, NULL until cerver_set_admin_enable (). */
	struct _AdminCerver *admin;
	/** @brief The thread of the admin cerver. */
	pthread_t admin_thread_id;

	/** @brief The events that the application registered, by type. */
	CerverEvent *events[CERVER_MAX_EVENTS];
	/** @brief The errors that the application registered, by type. */
	CerverErrorEvent *errors[CERVER_MAX_ERRORS];

	/** @brief The name, the alias, the welcome message and the uptime of the cerver. */
	CerverInfo *info;
	/** @brief The stats of the cerver. */
	CerverStats *stats;

	/** @brief The stats of the request types, [packet type][request type], see cerver_get_request_stats (). */
	RequestStats (*request_stats)[CERVER_REQUEST_STATS_ENTRIES];

	/** @brief The load balancer that the cerver belongs to, NULL if it does not belong to one. */
	struct _Balancer *balancer;

	/**
	 * @brief The TLS of the cerver, NULL without it (see cerver_set_tls ()).
	 *
	 * @since 2.1b-22
	 */
	struct _CerverTls *tls;

	/**
	 * @brief The I/O threads of a cerver with CERVER_HANDLER_TYPE_EVENTS.
	 *
	 * @since 2.1b-15
	 */
	unsigned int n_io_threads;

	/**
	 * @brief The connections that a cerver with CERVER_HANDLER_TYPE_EVENTS has at most at the same time.
	 *
	 * @since 2.1b-15
	 */
	unsigned int max_connections;

	/**
	 * @brief The seconds that a connection of a cerver with CERVER_HANDLER_TYPE_EVENTS waits for a place in the
	 * thpool with CERVER_OVERLOAD_POLICY_WAIT before it is answered with 503.
	 *
	 * @since 2.1b-15
	 */
	u32 overload_wait_timeout;

	/**
	 * @brief The event loops of a cerver with CERVER_HANDLER_TYPE_EVENTS while it runs.
	 *
	 * @since 2.1b-15
	 */
	struct EventLoopGroup *event_loops;

};

/** @brief A cerver, see struct _Cerver. */
typedef struct _Cerver Cerver;

/**
 * @brief Allocates a cerver with the default values.
 *
 * @return the new cerver, NULL if it could not be allocated
 * @note For the internal methods of cerver: an application creates a cerver with cerver_create ().
 */
CERVER_PRIVATE Cerver *cerver_new (void);

/**
 * @brief Releases a cerver and everything that it owns: its data, its handlers, its events and its stats.
 *
 * @param[in] ptr the cerver, it can be NULL
 * @note For the internal methods of cerver: an application releases a cerver with cerver_teardown ().
 */
CERVER_PRIVATE void cerver_delete (void *ptr);

/**
 * @brief Sets the main network values of a cerver.
 *
 * @param[in,out] cerver the cerver
 * @param[in] port the port
 * @param[in] protocol TCP or UDP
 * @param[in] use_ipv6 true to use IPv6
 * @param[in] connection_queue the backlog of listen ()
 */
CERVER_EXPORT void cerver_set_network_values (
	Cerver *cerver,
	const u16 port, const Protocol protocol,
	bool use_ipv6, const u16 connection_queue
);

/**
 * @brief Sets how many connections listen () queues for accept ().
 *
 * @param[in,out] cerver the cerver
 * @param[in] connection_queue the backlog of listen ()
 */
CERVER_EXPORT void cerver_set_connection_queue (
	Cerver *cerver, const u16 connection_queue
);

/**
 * @brief Sets the size of the buffer of each recv ().
 *
 * @param[in,out] cerver the cerver
 * @param[in] size the size in bytes, 0 keeps the current one
 */
CERVER_EXPORT void cerver_set_receive_buffer_size (
	Cerver *cerver, const size_t size
);

/**
 * @brief Sets whether the socket of the cerver uses SO_REUSEADDR.
 *
 * It avoids failing to bind an address that a previous process just used.
 *
 * @param[in,out] cerver the cerver
 * @param[in] value true to use SO_REUSEADDR, the default is #CERVER_DEFAULT_REUSABLE_FLAGS
 */
CERVER_EXPORT void cerver_set_reusable_address_flags (
	Cerver *cerver, bool value
);

/**
 * @brief Sets whether the socket of the cerver shares its port with other sockets (SO_REUSEPORT).
 *
 * @param[in,out] cerver the cerver
 * @param[in] value true to use SO_REUSEPORT, the default is #CERVER_DEFAULT_REUSABLE_PORT
 * @warning Only enable it when more than one process must listen in the same port: with it, a second cerver can
 * bind the same port without any error.
 */
CERVER_EXPORT void cerver_set_reusable_port (
	Cerver *cerver, bool value
);

/**
 * @brief Sets the literal IPv4 or IPv6 address that the cerver binds, like "127.0.0.1" or "::1".
 *
 * The family of the address sets whether the cerver uses IPv6.
 *
 * @param[in,out] cerver the cerver
 * @param[in] address the literal address, NULL binds every address again
 * @retval 0 the address was set
 * @retval 1 the cerver is NULL or the address is not a valid ip
 * @note Call it before cerver_start ().
 */
CERVER_EXPORT unsigned int cerver_set_bind_address (
	Cerver *cerver, const char *address
);

/**
 * @brief Sets IPV6_V6ONLY in the socket of an IPv6 cerver.
 *
 * @param[in,out] cerver the cerver
 * @param[in] ipv6_only false (the default, #CERVER_DEFAULT_IPV6_ONLY) also accepts IPv4 connections, as
 * IPv4-mapped IPv6 addresses
 */
CERVER_EXPORT void cerver_set_ipv6_only (
	Cerver *cerver, bool ipv6_only
);

/**
 * @brief Returns whether the cerver is accepting connections.
 *
 * @param[in] cerver the cerver
 * @return true while the cerver is running, false for a NULL cerver
 * @threadsafety Safe from any thread, it reads the flag with __atomic.
 */
CERVER_EXPORT bool cerver_is_running (const Cerver *cerver);

/**
 * @brief Sets whether cerver_start () ignores SIGPIPE when the process has not set a handler for it.
 *
 * Writing to a connection that the client reset raises SIGPIPE, that by default kills the process.
 *
 * @param[in,out] cerver the cerver
 * @param[in] value true to ignore it, the default is #CERVER_DEFAULT_IGNORE_SIGPIPE
 */
CERVER_EXPORT void cerver_set_ignore_sigpipe (
	Cerver *cerver, bool value
);

/**
 * @brief Sets the data of the cerver and how to release it.
 *
 * @param[in,out] cerver the cerver
 * @param[in] data the data, the cerver owns it from now on
 * @param[in] delete_data releases the data in the teardown, NULL to release it with free ()
 * @warning It replaces the data of the type of the cerver (a game, the files cerver), without releasing it.
 */
CERVER_EXPORT void cerver_set_cerver_data (
	Cerver *cerver, void *data, Action delete_data
);

/**
 * @brief Sets the number of threads of the thpool.
 *
 * Only a cerver with CERVER_HANDLER_TYPE_THREADS creates the thpool: each connection takes a thread while it is
 * connected, so the cerver handles at most this number of connections at the same time, and the others wait in the
 * listen backlog (CERVER_OVERLOAD_POLICY_WAIT) or are rejected.
 *
 * @param[in,out] cerver the cerver
 * @param[in] n_threads the threads, the default is #CERVER_DEFAULT_POOL_THREADS
 * @threadsafety The data of the handlers must be thread safe, as it is accessed from many threads.
 */
CERVER_EXPORT void cerver_set_thpool_n_threads (
	Cerver *cerver, u16 n_threads
);

/**
 * @brief Sets how many accepted connections can wait in the queue of the thpool.
 *
 * @param[in,out] cerver the cerver
 * @param[in] queue_size the size, rounded up to a power of 2 by the thpool; 0 uses the default of the thpool (the
 * number of threads * THPOOL_DEFAULT_QUEUE_SIZE_FACTOR)
 */
CERVER_EXPORT void cerver_set_thpool_queue_size (
	Cerver *cerver, unsigned int queue_size
);

/**
 * @brief Sets what happens with a new connection when the queue of the thpool is full.
 *
 * CERVER_OVERLOAD_POLICY_WAIT stops accepting until there is space, and the new connections wait in the listen
 * backlog; CERVER_OVERLOAD_POLICY_REJECT closes the new connection right away (web cervers respond with 503 and a
 * Retry-After header).
 *
 * @param[in,out] cerver the cerver
 * @param[in] policy the policy, a value out of the table keeps the current one; the default is
 * #CERVER_DEFAULT_OVERLOAD_POLICY
 */
CERVER_EXPORT void cerver_set_overload_policy (
	Cerver *cerver, CerverOverloadPolicy policy
);

/**
 * @brief Sets how long a connection of a cerver with CERVER_HANDLER_TYPE_EVENTS waits for a place in a full
 * thpool with CERVER_OVERLOAD_POLICY_WAIT.
 *
 * The connection waits in its event loop without using a thread; when the time ends it is answered with 503 and a
 * Retry-After header.
 *
 * @param[in,out] cerver the cerver
 * @param[in] seconds the seconds, 0 answers 503 as soon as the thpool has no place; the default is
 * #CERVER_DEFAULT_OVERLOAD_WAIT_TIMEOUT
 * @note The other handler types ignore it: with them the cerver stops accepting connections until the thpool has
 * space, without a limit.
 * @since 2.1b-15
 */
CERVER_EXPORT void cerver_set_overload_wait_timeout (
	Cerver *cerver, u32 seconds
);

/**
 * @brief Sets the I/O threads of a cerver with CERVER_HANDLER_TYPE_EVENTS: each one is an event loop that accepts
 * connections and reads their requests.
 *
 * With more than one, each loop listens in its own socket with SO_REUSEPORT and the kernel shares the new
 * connections among them.
 *
 * @param[in,out] cerver the cerver
 * @param[in] n_threads the threads, 0 keeps the current value; the default is #CERVER_DEFAULT_IO_THREADS
 * @note Call it before cerver_start (). The other handler types ignore it.
 * @since 2.1b-15
 */
CERVER_EXPORT void cerver_set_io_threads (
	Cerver *cerver, unsigned int n_threads
);

/**
 * @brief Sets the connections that a cerver with CERVER_HANDLER_TYPE_EVENTS has at most at the same time.
 *
 * At the limit the cerver stops accepting, and the new connections wait in the listen backlog until one ends.
 *
 * @param[in,out] cerver the cerver
 * @param[in] max_connections the limit, 0 keeps the current value; the default is
 * #CERVER_DEFAULT_MAX_CONNECTIONS
 * @note Call it before cerver_start (). The other handler types ignore it.
 * @since 2.1b-15
 */
CERVER_EXPORT void cerver_set_max_connections (
	Cerver *cerver, unsigned int max_connections
);

/**
 * @brief Sets what happens with the queued connections when the cerver is drained.
 *
 * In cerver_teardown (), the cerver stops accepting connections and waits the drain timeout
 * (cerver_set_drain_timeout ()) for the connections in progress: CERVER_DRAIN_POLICY_RUN also handles the queued
 * ones, CERVER_DRAIN_POLICY_REJECT rejects them right away (503 in web cervers).
 *
 * @param[in,out] cerver the cerver
 * @param[in] policy the policy, a value out of the table keeps the current one; the default is
 * #CERVER_DEFAULT_DRAIN_POLICY
 */
CERVER_EXPORT void cerver_set_drain_policy (
	Cerver *cerver, CerverDrainPolicy policy
);

/**
 * @brief Sets how many seconds the cerver waits for its connections when it is drained.
 *
 * The connections that are still queued after it are rejected.
 *
 * @param[in,out] cerver the cerver
 * @param[in] drain_timeout the seconds, 0 waits until every connection has finished; the default is
 * #CERVER_DEFAULT_DRAIN_TIMEOUT
 */
CERVER_EXPORT void cerver_set_drain_timeout (
	Cerver *cerver, u32 drain_timeout
);

/**
 * @brief Sets the SO_SNDTIMEO of every accepted connection, so a client that never reads can not block a thread
 * forever.
 *
 * @param[in,out] cerver the cerver
 * @param[in] send_timeout the seconds, 0 disables it; the default is #CERVER_DEFAULT_SEND_TIMEOUT
 */
CERVER_EXPORT void cerver_set_send_timeout (
	Cerver *cerver, u32 send_timeout
);

/**
 * @brief Does nothing.
 *
 * @param[in] cerver ignored
 * @param[in] n_sockets ignored
 * @deprecated The sockets pool was removed, every connection owns its socket.
 */
DEPRECATED (CERVER_EXPORT void cerver_set_sockets_pool_init (
	Cerver *cerver, unsigned int n_sockets
));

/**
 * @brief Enables the check of the inactive clients: the clients that have not sent or received a packet in some
 * time are dropped.
 *
 * The check runs in its own thread, started by cerver_start ().
 *
 * @param[in,out] cerver the cerver
 * @param[in] max_inactive_time the seconds that a client can be inactive, 0 for #CERVER_DEFAULT_MAX_INACTIVE_TIME
 * @param[in] check_inactive_interval the seconds between two checks, 0 for
 * #CERVER_DEFAULT_CHECK_INACTIVE_INTERVAL
 */
CERVER_EXPORT void cerver_set_inactive_clients (
	Cerver *cerver,
	u32 max_inactive_time, u32 check_inactive_interval
);

/**
 * @brief Sets how the cerver handles its TCP connections.
 *
 * CERVER_HANDLER_TYPE_POLL accepts, receives and runs the direct handlers in a single thread with poll (), and the
 * accepted sockets block with #CERVER_DEFAULT_SEND_TIMEOUT, so a client that does not read, or a slow direct
 * handler, delays every connection; handlers with their own queues avoid it. CERVER_HANDLER_TYPE_THREADS receives
 * each connection in its own thread, a thread of the thpool or a detachable one
 * (cerver_set_handle_detachable_threads ()).
 *
 * @param[in,out] cerver the cerver
 * @param[in] handler_type the handler type, the default is CERVER_HANDLER_TYPE_NONE
 * @note An UDP cerver ignores the handler type: a single thread receives every datagram (cerver/network/udp.h).
 */
CERVER_EXPORT void cerver_set_handler_type (
	Cerver *cerver, CerverHandlerType handler_type
);

/**
 * @brief Sets whether a cerver with CERVER_HANDLER_TYPE_THREADS handles each new connection in a new detachable
 * thread instead of the thpool.
 *
 * @param[in,out] cerver the cerver
 * @param[in] active true for the detachable threads; the default is false, that uses the thpool
 * @note A cerver of type CERVER_TYPE_WEB uses the thpool more often, as its connections have a shorter life.
 */
CERVER_EXPORT void cerver_set_handle_detachable_threads (
	Cerver *cerver, bool active
);

/**
 * @brief Sets the timeout of the main poll.
 *
 * @param[in,out] cerver the cerver
 * @param[in] poll_timeout the milliseconds, the default is #CERVER_DEFAULT_POLL_TIMEOUT
 */
CERVER_EXPORT void cerver_set_poll_time_out (
	Cerver *cerver, const u32 poll_timeout
);

/**
 * @brief Sets the initial size of the array of the main poll and the max connections that it handles.
 *
 * The array grows when it is full; a new connection beyond the max is closed and counted in
 * total_rejected_connections.
 *
 * @param[in,out] cerver the cerver
 * @param[in] initial the initial size, at least #CERVER_POLL_MIN_FDS; the default is #CERVER_DEFAULT_POLL_FDS
 * @param[in] max_connections the max connections, 0 (the default) is no limit
 * @retval 0 the values were set
 * @retval 1 the cerver is NULL, its poll already exists or the initial size is too small
 * @note Call it before cerver_start ().
 */
CERVER_EXPORT u8 cerver_set_poll_fds (
	Cerver *cerver, const u32 initial, const u32 max_connections
);

/**
 * @brief Enables the authentication of cerver: every new connection has to authenticate before it is a client.
 *
 * @param[in,out] cerver the cerver
 * @param[in] max_auth_tries the failed authentications that each new connection can have
 * @param[in] authenticate the method of the application, it gets an AuthMethod that it must not free and returns 0
 * when the authentication succeeds and 1 on any error
 * @retval 0 the authentication was enabled
 * @retval 1 the cerver is NULL
 */
CERVER_EXPORT u8 cerver_set_auth (
	Cerver *cerver,
	u8 max_auth_tries, delegate authenticate
);

/**
 * @brief Sets the failed authentications that a new connection can have before it is dropped.
 *
 * @param[in,out] cerver the cerver
 * @param[in] max_auth_tries the tries, the default is #CERVER_DEFAULT_MAX_AUTH_TRIES
 */
CERVER_EXPORT void cerver_set_auth_max_tries (
	Cerver *cerver, u8 max_auth_tries
);

/**
 * @brief Sets the method that authenticates the clients.
 *
 * @param[in,out] cerver the cerver
 * @param[in] authenticate the method, it returns 0 when the authentication succeeds; NULL keeps the current one
 */
CERVER_EXPORT void cerver_set_auth_method (
	Cerver *cerver, delegate authenticate
);

/**
 * @brief Sets the schema of the credentials of the clients.
 *
 * The credentials are decoded and validated with it before the authenticate method gets them, and its auth data
 * is the object; a packet that is not an object of the schema fails the authentication.
 *
 * @param[in,out] cerver the cerver
 * @param[in] schema the schema, it must live as long as the cerver; NULL takes the bytes of the credentials as they
 * come
 * @warning The fields of the schema can not take memory from an arena.
 */
CERVER_EXPORT void cerver_set_auth_schema (
	Cerver *cerver, const SerializerSchema *schema
);

/**
 * @brief Sets the timeout of the on hold poll.
 *
 * @param[in,out] cerver the cerver
 * @param[in] on_hold_poll_timeout the milliseconds, the default is #CERVER_DEFAULT_ON_HOLD_TIMEOUT
 */
CERVER_EXPORT void cerver_set_on_hold_poll_timeout (
	Cerver *cerver, u32 on_hold_poll_timeout
);

/**
 * @brief Sets the connections that the on hold poll handles at the same time; a new connection when it is full is
 * closed.
 *
 * @param[in,out] cerver the cerver
 * @param[in] max_connections the connections, the default is #CERVER_DEFAULT_ON_HOLD_POLL_FDS
 * @retval 0 the value was set
 * @retval 1 the cerver is NULL, its on hold poll already exists or max_connections is 0
 * @note Call it before cerver_start ().
 */
CERVER_EXPORT u8 cerver_set_on_hold_poll_fds (
	Cerver *cerver, const u32 max_connections
);

/**
 * @brief Sets the seconds that a new connection has to authenticate; after them it is dropped
 * (CERVER_EVENT_ON_HOLD_DROPPED).
 *
 * It is checked every on hold poll timeout.
 *
 * @param[in,out] cerver the cerver
 * @param[in] on_hold_timeout the seconds, 0 disables it; the default is #CERVER_DEFAULT_ON_HOLD_SECONDS
 */
CERVER_EXPORT void cerver_set_on_hold_timeout (
	Cerver *cerver, u32 on_hold_timeout
);

/**
 * @brief Sets the bad packets that an on hold connection can send before it is dropped.
 *
 * A bad packet is one that the cerver can not handle, because it has no handler or because it failed
 * packet_check ().
 *
 * @param[in,out] cerver the cerver
 * @param[in] on_hold_max_bad_packets the packets, the default is #CERVER_DEFAULT_ON_HOLD_MAX_BAD_PACKETS
 */
CERVER_EXPORT void cerver_set_on_hold_max_bad_packets (
	Cerver *cerver, u8 on_hold_max_bad_packets
);

/**
 * @brief Sets whether the on hold handler checks the packets with packet_check (); a packet that fails is a bad
 * packet.
 *
 * @param[in,out] cerver the cerver
 * @param[in] check true to check them, the default is #CERVER_DEFAULT_ON_HOLD_CHECK_PACKETS
 */
CERVER_EXPORT void cerver_set_on_hold_check_packets (
	Cerver *cerver, bool check
);

/**
 * @brief Sets the size of the buffer that receives the packets of the on hold connections.
 *
 * @param[in,out] cerver the cerver
 * @param[in] on_hold_receive_buffer_size the size in bytes, 0 keeps the current one; the default is
 * #CERVER_DEFAULT_ON_HOLD_RECEIVE_BUFFER_SIZE
 */
CERVER_EXPORT void cerver_set_on_hold_receive_buffer_size (
	Cerver *cerver, const size_t on_hold_receive_buffer_size
);

/**
 * @brief Configures the cerver to use client sessions.
 *
 * A client can then have many connections, like the ones of different devices with the same token.
 *
 * @param[in,out] cerver the cerver
 * @param[in] session_id_generator takes a SessionData and returns the session id (the token) as a string allocated
 * with malloc (), that cerver copies and releases with free (); NULL uses the default one, 32 random bytes of the
 * kernel in hex characters
 * @retval 0 the sessions were enabled
 * @retval 1 the cerver is NULL
 */
CERVER_EXPORT u8 cerver_set_sessions (
	Cerver *cerver, void *(*session_id_generator) (const void *)
);

/**
 * @brief Sets the method that handles the buffer of each recv (), for a custom protocol.
 *
 * @param[in,out] cerver the cerver
 * @param[in] handle_received_buffer the method, it gets a ReceiveHandle; cerver_create () sets the one that reads
 * the cerver packets
 */
CERVER_EXPORT void cerver_set_handle_recieved_buffer (
	Cerver *cerver, Action handle_received_buffer
);

/**
 * @brief Sets the biggest packet that the cerver handles; a bigger one is a bad packet.
 *
 * @param[in,out] cerver the cerver
 * @param[in] max_received_packet_size the size in bytes, 0 keeps the current one; the default is
 * #CERVER_DEFAULT_MAX_RECEIVED_PACKET_SIZE
 */
CERVER_EXPORT void cerver_set_max_received_packet_size (
	Cerver *cerver, size_t max_received_packet_size
);

/**
 * @brief Sets the handlers of the PACKET_TYPE_APP and PACKET_TYPE_APP_ERROR packets.
 *
 * @param[in,out] cerver the cerver
 * @param[in] app_handler the handler of the PACKET_TYPE_APP packets, the cerver owns it from now on; it can be NULL
 * @param[in] app_error_handler the handler of the PACKET_TYPE_APP_ERROR packets, the cerver owns it from now on; it
 * can be NULL
 * @note A different handler deletes the one that was already set; the same handler again is kept. Set them
 * before cerver_start ().
 */
CERVER_EXPORT void cerver_set_app_handlers (
	Cerver *cerver,
	struct _Handler *app_handler,
	struct _Handler *app_error_handler
);

/**
 * @brief Sets whether the cerver deletes the PACKET_TYPE_APP packets after their handler.
 *
 * @param[in,out] cerver the cerver
 * @param[in] delete_packet false so the handler deletes them; by default the cerver does it
 */
CERVER_EXPORT void cerver_set_app_handler_delete (
	Cerver *cerver, bool delete_packet
);

/**
 * @brief Sets whether the cerver deletes the PACKET_TYPE_APP_ERROR packets after their handler.
 *
 * @param[in,out] cerver the cerver
 * @param[in] delete_packet false so the handler deletes them; by default the cerver does it
 */
CERVER_EXPORT void cerver_set_app_error_handler_delete (
	Cerver *cerver, bool delete_packet
);

/**
 * @brief Sets the handler of the PACKET_TYPE_CUSTOM packets.
 *
 * @param[in,out] cerver the cerver
 * @param[in] custom_handler the handler, the cerver owns it from now on; it can be NULL
 * @note A different handler deletes the one that was already set; the same handler again is kept. Set them
 * before cerver_start ().
 */
CERVER_EXPORT void cerver_set_custom_handler (
	Cerver *cerver, struct _Handler *custom_handler
);

/**
 * @brief Sets whether the cerver deletes the PACKET_TYPE_CUSTOM packets after their handler.
 *
 * @param[in,out] cerver the cerver
 * @param[in] delete_packet false so the handler deletes them; by default the cerver does it
 */
CERVER_EXPORT void cerver_set_custom_handler_delete (
	Cerver *cerver, bool delete_packet
);

/**
 * @brief Enables the multiple app handlers: the PACKET_TYPE_APP packets go to the handler of the handler id of
 * their header.
 *
 * The handlers are added with cerver_handlers_add ().
 *
 * @param[in,out] cerver the cerver
 * @param[in] n_handlers the number of handlers, their ids go from 0 to n_handlers - 1
 * @retval 0 the multiple handlers were enabled
 * @retval 1 the cerver is NULL, n_handlers is 0, it is a second call, or the array could not be allocated
 */
CERVER_EXPORT int cerver_set_multiple_handlers (
	Cerver *cerver, unsigned int n_handlers
);

/**
 * @brief Sets the handler of the packets of a request type of PACKET_TYPE_APP, PACKET_TYPE_APP_ERROR or
 * PACKET_TYPE_CUSTOM.
 *
 * The packets of the request types without one go to the handler of their packet type, and they are released like
 * the ones of that handler.
 *
 * @param[in,out] cerver the cerver
 * @param[in] packet_type PACKET_TYPE_APP, PACKET_TYPE_APP_ERROR or PACKET_TYPE_CUSTOM
 * @param[in] request_type the request type, below #CERVER_REQUEST_HANDLERS_MAX
 * @param[in] handler the handler, the cerver owns it and deletes it in its teardown
 * @retval 0 the handler was set
 * @retval 1 a NULL argument, the cerver is running, a packet type or a request type out of range, the request type
 * already has a handler, or the array could not grow; the handler still belongs to the caller
 * @note Call it before cerver_start ().
 */
CERVER_EXPORT u8 cerver_set_request_handler (
	Cerver *cerver,
	const PacketType packet_type, const u32 request_type,
	struct _Handler *handler
);

/**
 * @brief Returns the handler of a request type of a packet type.
 *
 * @param[in] cerver the cerver
 * @param[in] packet_type the packet type
 * @param[in] request_type the request type
 * @return the handler, NULL if the request type does not have one
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE struct _Handler *cerver_get_request_handler (
	const Cerver *cerver,
	const PacketType packet_type, const u32 request_type
);

/**
 * @brief Sets whether the cerver checks the protocol id and the version of the header of each packet
 * (packet_check ()); a packet that fails is a bad packet.
 *
 * The size of the packets is checked by the handlers.
 *
 * @param[in,out] cerver the cerver
 * @param[in] check_packets true to check them, the default is #CERVER_DEFAULT_CHECK_PACKETS
 */
CERVER_EXPORT void cerver_set_check_packets (
	Cerver *cerver, bool check_packets
);

/**
 * @brief Sets how many bad packets an authenticated connection can send before it is dropped.
 *
 * A bad packet fails packet_check () or has an unknown type, and its sender gets a CERVER_ERROR_PACKET_ERROR; a
 * packet bigger than the max received packet size closes the connection right away.
 *
 * @param[in,out] cerver the cerver
 * @param[in] max_bad_packets the packets, 0 disables the limit; the default is #CERVER_DEFAULT_MAX_BAD_PACKETS
 */
CERVER_EXPORT void cerver_set_max_bad_packets (
	Cerver *cerver, u8 max_bad_packets
);

/**
 * @brief Sets a method that runs every tick in its own thread, started by cerver_start ().
 *
 * @param[in,out] cerver the cerver
 * @param[in] update the method, it gets a CerverUpdate with the cerver and the arguments
 * @param[in] update_args the arguments of the method
 * @param[in] delete_update_args releases the arguments in the teardown, NULL to keep them
 * @param[in] fps the ticks per second, 0 runs it once per second
 */
CERVER_EXPORT void cerver_set_update (
	Cerver *cerver,
	Action update,
	void *update_args, void (*delete_update_args)(void *),
	const u8 fps
);

/**
 * @brief Sets a method that runs every some seconds in its own thread, started by cerver_start ().
 *
 * @param[in,out] cerver the cerver
 * @param[in] update the method, it gets a CerverUpdate with the cerver and the arguments
 * @param[in] update_args the arguments of the method
 * @param[in] delete_update_args releases the arguments in the teardown, NULL to keep them
 * @param[in] interval the seconds between two calls
 */
CERVER_EXPORT void cerver_set_update_interval (
	Cerver *cerver,
	Action update,
	void *update_args, void (*delete_update_args)(void *),
	const u32 interval
);

/**
 * @brief Enables the admin connections of the cerver, with its admin cerver.
 *
 * @param[in,out] cerver the cerver
 * @retval 0 the admins were enabled, or the cerver already had its admin cerver
 * @retval 1 the cerver is NULL or the admin cerver could not be created
 */
CERVER_EXPORT u8 cerver_set_admin_enable (Cerver *cerver);

/** @} */

/**
 * @defgroup core_create Create
 * @ingroup core
 * @brief How a cerver is created.
 * @{
 */

/**
 * @brief Creates a cerver with its network values.
 *
 * It attaches the data of its type (a game for CERVER_TYPE_GAME, an HTTP cerver for CERVER_TYPE_WEB, a files cerver
 * for CERVER_TYPE_FILES), the default receive handler, its info and its stats.
 *
 * @param[in] type the type
 * @param[in] name the name, required
 * @param[in] port the port
 * @param[in] protocol TCP or UDP
 * @param[in] use_ipv6 true to use IPv6
 * @param[in] connection_queue the backlog of listen ()
 * @return the new cerver, released with cerver_teardown (); NULL without a name or if it could not be allocated
 */
CERVER_EXPORT Cerver *cerver_create (
	const CerverType type, const char *name,
	const u16 port, const Protocol protocol, bool use_ipv6,
	const u16 connection_queue
);

/**
 * @brief Creates a cerver of type CERVER_TYPE_WEB, with TCP and IPv4.
 *
 * @param[in] name the name, required
 * @param[in] port the port
 * @param[in] connection_queue the backlog of listen ()
 * @return the new cerver, released with cerver_teardown (); NULL without a name or if it could not be allocated
 */
CERVER_EXPORT Cerver *cerver_create_web (
	const char *name, const u16 port, const u16 connection_queue
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
