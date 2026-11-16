/**
 * @file cerver/admin/cerver.h
 * @brief The admin cerver: its struct, its setters, its broadcasts and the registry of its admins.
 */

#ifndef _CERVER_ADMIN_CERVER_H_
#define _CERVER_ADMIN_CERVER_H_

#include "cerver/config.h"

#include "cerver/admin/admin.h"
#include "cerver/admin/stats.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup admin_main Admin cerver
 * @ingroup admin
 * @brief The struct of the admin cerver and its setters.
 *
 * The setters ignore a NULL admin cerver.
 * @{
 */

/**
 * @brief The admin cerver of a cerver.
 */
struct _AdminCerver {

	/** @brief The cerver that the admin cerver belongs to. */
	struct _Cerver *cerver;

	/** @brief The connected admins, with its own lock. */
	DoubleList *admins;

	/** @brief The method that authenticates the admins, it returns 0 when the authentication succeeds. */
	delegate authenticate;

	/** @brief The max admins at the same time, 0 is no limit: admin_cerver_register_admin () rejects one more. */
	u8 max_admins;
	/**
	 * @brief The max connections of each admin, 0 is no limit: admin_cerver_register_admin () rejects an admin whose
	 * client has more.
	 */
	u8 max_admin_connections;

	/** @brief The bad packets of an admin connection before it is dropped. */
	u32 n_bad_packets_limit;

	/** @brief The size in bytes of the buffer of the admin poll. */
	size_t receive_buffer_size;

	/** @brief The array of the admin poll. */
	struct pollfd *fds;
	/** @brief The size of the array of the admin poll. */
	u32 max_n_fds;
	/** @brief The active fds in the array of the admin poll. */
	u16 current_n_fds;
	/** @brief The timeout in milliseconds of the admin poll. */
	u32 poll_timeout;
	/** @brief Protects the array of the admin poll. */
	pthread_mutex_t *poll_lock;

	/** @brief The handler of the PACKET_TYPE_APP packets of the authenticated admins. */
	struct _Handler *app_packet_handler;
	/** @brief The handler of the PACKET_TYPE_APP_ERROR packets of the authenticated admins. */
	struct _Handler *app_error_packet_handler;
	/** @brief The handler of the PACKET_TYPE_CUSTOM packets of the authenticated admins. */
	struct _Handler *custom_packet_handler;

	/** @brief The handlers that are alive (ready to handle packets). */
	unsigned int num_handlers_alive;
	/** @brief The handlers that are handling a packet, counted with __atomic. */
	unsigned int num_handlers_working;
	/** @brief Protects #num_handlers_alive, created when the admin cerver starts. */
	pthread_mutex_t *handlers_lock;

	/** @brief The admin cerver deletes the PACKET_TYPE_APP packets after their handler. */
	bool app_packet_handler_delete_packet;
	/** @brief The admin cerver deletes the PACKET_TYPE_APP_ERROR packets after their handler. */
	bool app_error_packet_handler_delete_packet;
	/** @brief The admin cerver deletes the PACKET_TYPE_CUSTOM packets after their handler. */
	bool custom_packet_handler_delete_packet;

	/** @brief The packets of the admins are checked with packet_check () by the admin handler. */
	bool check_packets;

	/** @brief The thread of the update method. */
	pthread_t update_thread_id;
	/** @brief The method that runs every tick, it gets a CerverUpdate with the cerver. */
	Action update;
	/** @brief The arguments of the update method. */
	void *update_args;
	/** @brief Releases the arguments of the update method when the cerver is torn down. */
	void (*delete_update_args)(void *);
	/** @brief The ticks per second of the update method. */
	u8 update_ticks;

	/** @brief The thread of the update interval method. */
	pthread_t update_interval_thread_id;
	/** @brief The method that runs every #update_interval_secs, it gets a CerverUpdate with the cerver. */
	Action update_interval;
	/** @brief The arguments of the update interval method. */
	void *update_interval_args;
	/** @brief Releases the arguments of the update interval method when the cerver is torn down. */
	void (*delete_update_interval_args)(void *);
	/** @brief The seconds between two calls of the update interval method. */
	u32 update_interval_secs;

	/** @brief The stats of the admin cerver. */
	struct _AdminCerverStats *stats;

};

/** @brief The admin cerver of a cerver, see struct _AdminCerver. */
typedef struct _AdminCerver AdminCerver;

/**
 * @brief Allocates an admin cerver with the default values.
 *
 * @return the new admin cerver, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE AdminCerver *admin_cerver_new (void);

/**
 * @brief Releases an admin cerver: its admins, its poll, its handlers and its stats.
 *
 * @param[in] admin_cerver the admin cerver, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void admin_cerver_delete (AdminCerver *admin_cerver);

/**
 * @brief Creates an admin cerver with its list of admins and its stats.
 *
 * @return the new admin cerver, NULL if it could not be allocated
 * @note For the internal methods of cerver: an application calls cerver_set_admin_enable ().
 */
CERVER_PRIVATE AdminCerver *admin_cerver_create (void);

/**
 * @brief Sets the method that authenticates the credentials of the admins.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] authenticate the method, it gets an AuthMethod and returns 0 on success and 1 on error
 */
CERVER_EXPORT void admin_cerver_set_authenticate (
	AdminCerver *admin_cerver, delegate authenticate
);

/**
 * @brief Sets the max admins at the same time.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] max_admins the admins, the default is #ADMIN_CERVER_DEFAULT_MAX_ADMINS, 0 is no limit
 * @note An admin that authenticates when the limit is reached is rejected with an error.
 */
CERVER_EXPORT void admin_cerver_set_max_admins (
	AdminCerver *admin_cerver, u8 max_admins
);

/**
 * @brief Sets the max connections of each admin.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] max_admin_connections the connections, the default is #ADMIN_CERVER_DEFAULT_MAX_ADMIN_CONNECTIONS, 0
 * is no limit
 * @note An admin whose client has more connections when it authenticates is rejected with an error.
 */
CERVER_EXPORT void admin_cerver_set_max_admin_connections (
	AdminCerver *admin_cerver, u8 max_admin_connections
);

/**
 * @brief Sets the bad packets that an admin connection can send before it is dropped.
 *
 * A bad packet fails packet_check () or has an unknown type, and its sender gets a CERVER_ERROR_PACKET_ERROR.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] n_bad_packets_limit the packets, 0 or less uses #ADMIN_CERVER_DEFAULT_MAX_BAD_PACKETS
 */
CERVER_EXPORT void admin_cerver_set_bad_packets_limit (
	AdminCerver *admin_cerver, i32 n_bad_packets_limit
);

/**
 * @brief Sets the size of the buffer of the admin poll.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] buffer_size the size in bytes, the default is #ADMIN_CERVER_DEFAULT_RECEIVE_BUFFER_SIZE
 * @note Call it before cerver_start (): the buffer is allocated when the admin poll starts.
 */
CERVER_EXPORT void admin_cerver_set_receive_buffer_size (
	AdminCerver *admin_cerver, const size_t buffer_size
);

/**
 * @brief Sets the size of the array of the admin poll.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] max_n_fds the size, the default is #ADMIN_CERVER_DEFAULT_POLL_FDS; 0 is ignored
 * @note Call it before cerver_start (): a call after the array was created is ignored.
 */
CERVER_EXPORT void admin_cerver_set_max_fds (
	AdminCerver *admin_cerver, u32 max_n_fds
);

/**
 * @brief Sets the timeout of the admin poll.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] poll_timeout the milliseconds, the default is #ADMIN_CERVER_DEFAULT_POLL_TIMEOUT
 */
CERVER_EXPORT void admin_cerver_set_poll_timeout (
	AdminCerver *admin_cerver, u32 poll_timeout
);

/**
 * @brief Sets the handlers of the PACKET_TYPE_APP and PACKET_TYPE_APP_ERROR packets of the admins.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] app_handler the handler of the PACKET_TYPE_APP packets, the admin cerver owns it from now on; it can be
 * NULL
 * @param[in] app_error_handler the handler of the PACKET_TYPE_APP_ERROR packets, the admin cerver owns it from now
 * on; it can be NULL
 * @note A different handler deletes the one that was already set; the same handler again is kept. Set them
 * before cerver_start ().
 */
CERVER_EXPORT void admin_cerver_set_app_handlers (
	AdminCerver *admin_cerver,
	struct _Handler *app_handler, struct _Handler *app_error_handler
);

/**
 * @brief Sets whether the admin cerver deletes the PACKET_TYPE_APP packets after their handler.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] delete_packet false so the handler deletes them; by default the admin cerver does it
 */
CERVER_EXPORT void admin_cerver_set_app_handler_delete (
	AdminCerver *admin_cerver, bool delete_packet
);

/**
 * @brief Sets whether the admin cerver deletes the PACKET_TYPE_APP_ERROR packets after their handler.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] delete_packet false so the handler deletes them; by default the admin cerver does it
 */
CERVER_EXPORT void admin_cerver_set_app_error_handler_delete (
	AdminCerver *admin_cerver, bool delete_packet
);

/**
 * @brief Sets the handler of the PACKET_TYPE_CUSTOM packets of the admins.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] custom_handler the handler, the admin cerver owns it from now on; it can be NULL
 * @note A different handler deletes the one that was already set; the same handler again is kept. Set them
 * before cerver_start ().
 */
CERVER_EXPORT void admin_cerver_set_custom_handler (
	AdminCerver *admin_cerver, struct _Handler *custom_handler
);

/**
 * @brief Sets whether the admin cerver deletes the PACKET_TYPE_CUSTOM packets after their handler.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] delete_packet false so the handler deletes them; by default the admin cerver does it
 */
CERVER_EXPORT void admin_cerver_set_custom_handler_delete (
	AdminCerver *admin_cerver, bool delete_packet
);

/**
 * @brief Returns the number of handlers of the admin cerver that are alive (ready to handle packets).
 *
 * @param[in] admin_cerver the admin cerver
 * @return the handlers, 0 for a NULL admin cerver or one that has not started
 * @threadsafety Safe from any thread, it reads the count with the lock of the handlers.
 */
CERVER_EXPORT unsigned int admin_cerver_get_n_handlers_alive (
	AdminCerver *admin_cerver
);

/**
 * @brief Returns the number of handlers of the admin cerver that are handling a packet.
 *
 * @param[in] admin_cerver the admin cerver
 * @return the handlers, 0 for a NULL admin cerver
 * @threadsafety Safe from any thread, the handlers count themselves with __atomic.
 */
CERVER_EXPORT unsigned int admin_cerver_get_n_handlers_working (
	AdminCerver *admin_cerver
);

/**
 * @brief Sets whether the packets of the admins are checked with packet_check ().
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] check_packets true to check them, the default is #ADMIN_CERVER_DEFAULT_CHECK_PACKETS
 * @note It is independent of the check_packets of the cerver (cerver_set_check_packets ()), which was used before.
 */
CERVER_EXPORT void admin_cerver_set_check_packets (
	AdminCerver *admin_cerver, bool check_packets
);

/**
 * @brief Sets a method that runs every tick in its own thread, started with the admin cerver.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] update the method, it gets a CerverUpdate with the cerver and the arguments
 * @param[in] update_args the arguments of the method
 * @param[in] delete_update_args releases the arguments in the teardown, NULL to keep them
 * @param[in] fps the ticks per second, 0 runs it once per second
 */
CERVER_EXPORT void admin_cerver_set_update (
	AdminCerver *admin_cerver,
	Action update, void *update_args, void (*delete_update_args)(void *),
	const u8 fps
);

/**
 * @brief Sets a method that runs every some seconds in its own thread, started with the admin cerver.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] update the method, it gets a CerverUpdate with the cerver and the arguments
 * @param[in] update_args the arguments of the method
 * @param[in] delete_update_args releases the arguments in the teardown, NULL to keep them
 * @param[in] interval the seconds between two calls
 */
CERVER_EXPORT void admin_cerver_set_update_interval (
	AdminCerver *admin_cerver,
	Action update, void *update_args, void (*delete_update_args)(void *),
	const u32 interval
);

/**
 * @brief Returns the number of connected admins.
 *
 * @param[in] admin_cerver the admin cerver
 * @return the admins, 0 for a NULL admin cerver
 */
CERVER_EXPORT u8 admin_cerver_get_current_admins (
	AdminCerver *admin_cerver
);

/**
 * @brief Sends a packet to the first connection of every connected admin, with admin_send_packet ().
 *
 * @param[in] admin_cerver the admin cerver
 * @param[in,out] packet the packet
 * @retval 0 the packet was sent to every admin
 * @retval 1 a NULL argument, or the packet was not sent to some admin
 * @threadsafety It takes the lock of the list of the admins.
 */
CERVER_EXPORT u8 admin_cerver_broadcast_to_admins (
	AdminCerver *admin_cerver, struct _Packet *packet
);

/**
 * @brief Sends a packet to every connected admin with admin_send_packet_split ().
 *
 * @param[in] admin_cerver the admin cerver
 * @param[in] packet the packet
 * @retval 0 the packet was sent to every admin
 * @retval 1 a NULL argument, or the packet was not sent to some admin
 * @threadsafety It takes the lock of the list of the admins.
 */
CERVER_EXPORT u8 admin_cerver_broadcast_to_admins_split (
	AdminCerver *admin_cerver, struct _Packet *packet
);

/**
 * @brief Sends a packet in pieces to every connected admin with admin_send_packet_pieces ().
 *
 * @param[in] admin_cerver the admin cerver
 * @param[in,out] packet the packet
 * @param[in] pieces the pieces
 * @param[in] sizes the size in bytes of each piece
 * @param[in] n_pieces the number of pieces
 * @retval 0 the packet was sent to every admin
 * @retval 1 a NULL argument, or the packet was not sent to some admin
 * @threadsafety It takes the lock of the list of the admins.
 */
CERVER_EXPORT u8 admin_cerver_broadcast_to_admins_pieces (
	AdminCerver *admin_cerver, struct _Packet *packet,
	void **pieces, size_t *sizes, u32 n_pieces
);

/**
 * @brief Registers a new admin: its first connection goes to the admin poll and the admin to the list, so the
 * admin cerver handles its packets.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in,out] admin the admin, with at least one connection
 * @retval 0 the admin was registered
 * @retval 1 a NULL argument, the admin cerver has #AdminCerver::max_admins, the client of the admin has more than
 * #AdminCerver::max_admin_connections, or the admin poll is full
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 admin_cerver_register_admin (
	AdminCerver *admin_cerver, Admin *admin
);

/**
 * @brief Unregisters an admin: it leaves the list and its connections leave the admin poll; it is not deleted.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in,out] admin the admin
 * @retval 0 the admin was unregistered
 * @retval 1 a NULL argument, or the admin was not in the list
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 admin_cerver_unregister_admin (
	AdminCerver *admin_cerver, Admin *admin
);

/**
 * @brief Unregisters an admin and deletes it.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] admin the admin, it is deleted even when it was not registered
 * @retval 0 the admin was unregistered and deleted
 * @retval 1 a NULL argument, or the admin was not in the list
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 admin_cerver_drop_admin (
	AdminCerver *admin_cerver, Admin *admin
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
