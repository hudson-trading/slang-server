# A WCP viewer that greets the server and then disconnects, used by WcpTests.cpp.
import json
import socket
import sys

sock = socket.create_connection(("127.0.0.1", int(sys.argv[1])))
buf = b""
while b"\0" not in buf:
    data = sock.recv(4096)
    if not data:
        sys.exit(0)
    buf += data
greeting = {
    "type": "greeting",
    "version": "0",
    "commands": ["add_items", "get_item_list", "get_item_info", "focus_item", "load"],
}
sock.sendall(json.dumps(greeting).encode() + b"\0")
sock.close()
