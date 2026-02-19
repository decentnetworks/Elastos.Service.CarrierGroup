from ctypes import POINTER, c_char, c_char_p, c_int, cdll, create_string_buffer, string_at
from pathlib import Path

LIB_PATH = Path(__file__).resolve().with_name("libcarrierManager.so")
carrierManager = cdll.LoadLibrary(str(LIB_PATH))
carrierManager.start.argtypes = [c_char_p, c_int, c_char_p]
carrierManager.start.restype = None
carrierManager.createGroup.argtypes = []
carrierManager.createGroup.restype = None
carrierManager.list.argtypes = [POINTER(c_char)]
carrierManager.list.restype = None

def start(ip, port, data_dir):
    hostname = str(ip).encode("utf-8")
    data_dir_bytes = str(data_dir).encode("utf-8")
    manager_port = int(port)
    print(hostname, manager_port, data_dir_bytes)
    carrierManager.start(hostname, manager_port, data_dir_bytes)

def createGroup():
    print("createGroup in");
    carrierManager.createGroup()
    print("createGroup out");

def list():
    data_out = create_string_buffer(1024*512)
    carrierManager.list(data_out)
    data = string_at(data_out, -1).decode("utf-8")
    print("list:", data)
    return data

