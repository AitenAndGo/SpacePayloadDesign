import socket

s = socket.socket()
s.connect(("192.168.4.1", 4242))

while True:
    cmd = input("> ")
    s.sendall((cmd + "\n").encode())
    print(s.recv(1024).decode())