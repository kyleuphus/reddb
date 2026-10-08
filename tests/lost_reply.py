# Needs a 10 MB value first:
#   head -c 10000000 /dev/zero | tr '\0' x | redis-cli -p 6380 -x SET big
import socket, time

s = socket.create_connection(("127.0.0.1", 6380))
s.sendall(b"*2\r\n$3\r\nGET\r\n$3\r\nbig\r\n" + b"oops\r\n")
time.sleep(0.5)  # let the server hit the bad bytes before we read anything
got = b""
while True:
    try:
        chunk = s.recv(1 << 16)
    except ConnectionResetError:
        print("connection reset")
        break
    if not chunk:
        break
    got += chunk
print(
    "received",
    len(got),
    "bytes; expected",
    len(b"$10000000\r\n") + 10000000 + 2 + len(b"-ERR could not be parsed\r\n"),
)
print("ends with -ERR:", got.endswith(b"-ERR could not be parsed\r\n"))
