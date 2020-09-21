from ctypes import *

lib = CDLL ("libcerver.so", RTLD_GLOBAL)

# types
ACTION = CFUNCTYPE (c_void_p, c_void_p)

# structures
class PacketHeader (Structure):
	_fields_ = [("packet_type", c_int),
				("packet_size", c_size_t),
				("handler_id", c_uint8),
				("packet_type", c_uint32),]

class Packet (Structure):
	_fields_ = [("cerver", c_void_p),
				("client", c_void_p),
				("connection", c_void_p),
				("lobby", c_void_p),

				("packet_type", c_int),
				("req_type", c_uint32),

				("data_size", c_size_t),
				("data", c_void_p),
				("data_ptr", c_char_p),
				("data_end", c_char_p),
				("data_ref", c_bool),

				("header", POINTER (PacketHeader)),
				("version", c_void_p),
				("packet_size", c_size_t),
				("packet", c_void_p),
				("packet_ref", c_bool)]

# cerver
cerver_create = lib.cerver_create
cerver_create.argtypes = [
	c_int, c_char_p,
	c_int, c_int, c_bool,
	c_int, c_int
]
cerver_create.restype = c_void_p

cerver_set_app_packet_handler = lib.cerver_set_app_packet_handler
cerver_set_app_packet_handler.argtypes = [c_void_p, ACTION, c_bool]

cerver_start = lib.cerver_start
cerver_start.argtypes = [c_void_p]
cerver_start.restype = c_uint8