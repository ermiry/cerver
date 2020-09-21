import ctypes
import cerver

def app_handler (p_ptr):
    p = ctypes.cast (p_ptr, ctypes.POINTER (cerver.Packet))
    print (p.packet_ref)

my_cerver = cerver.cerver_create (1, b"my-cerver", 7000, 6, False, 2, 2000);

handler_func = cerver.ACTION (app_handler);
cerver.cerver_set_app_packet_handler (my_cerver, handler_func, True)

cerver.cerver_start (my_cerver)