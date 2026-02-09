import socket
import struct

def parse_tm(frame: bytes):
    if len(frame) != 10:
        return None

    if frame[0:2] != b"TM":
        return None

    if checksum(frame[:-1]) != frame[-1]:
        print("Bad checksum")
        return None

    status = frame[2]
    data = struct.unpack("!H", frame[3:5])[0]
    ts = struct.unpack("!I", frame[5:9])[0]

    return status, data, ts

def checksum(data: bytes) -> int:
    cs = 0
    for b in data:
        cs ^= b
    return cs

s = socket.socket()
s.connect(("192.168.4.1", 4242))

while True:
    cmd  = int(input("Command ID: "))
    p1   = int(input("Param 1: "))
    p2   = int(input("Param 2: "))

    frame = struct.pack(
        "!2sBBB",
        b"TC",
        cmd & 0xFF,
        p1 & 0xFF,
        p2 & 0xFF
    )

    cs = checksum(frame)
    s.sendall(frame + struct.pack("!B", cs))

    reply = s.recv(1024)
    print("RX:", reply)
