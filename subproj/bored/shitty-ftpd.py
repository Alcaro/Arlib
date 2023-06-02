#!/usr/bin/env python3

# super primitive ftp server, can be used to bootstrap BoredomFS onto another computer
# usage: explorer.exe ftp://192.168.122.1:2121
# (to use another server address, change the above, and also the 'pasv' response below)
# (the windows command line ftp client is not supported, since it uses active mode and does not support passive)

# if you accidentally transfer wrong file, you'll need to change the filename
# explorer.exe is extremely aggressively about caching data retrieved over ftp

import socket
import selectors

returned_file = open("bored.exe","rb").read()

ctrl_serv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
ctrl_serv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
ctrl_serv.bind(('', 2121))
ctrl_serv.listen(5)

data_serv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
data_serv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
data_serv.bind(('', 2122))  # must match the pasv response; the last two of six octets are the bytes of the port number, base 10
data_serv.listen(5)

select = selectors.DefaultSelector()
select.register(ctrl_serv, selectors.EVENT_READ)
select.register(data_serv, selectors.EVENT_READ)

while True:
	print("accepting")
	for key,events in select.select():
		if key.fileobj is data_serv:
			print("unexpected data connection")
			# we don't expect any data connections at this point
			data_sock, _ = data_serv.accept()
			data_sock.close()
		if key.fileobj is ctrl_serv:
			break
	else:
		continue
	ctrl_sock, _ = ctrl_serv.accept()
	def reply(line):
		ctrl_sock.send(line.encode("UTF-8")+b"\r\n")
	def send_data_sock(by):
		data_sock, _ = data_serv.accept()
		data_sock.send(by)
		data_sock.close()
	reply("220 Hello")
	while True:
		chunk = ctrl_sock.recv(4096).decode("UTF-8")
		if not chunk:
			break
		for line in chunk.splitlines():
			print(line)
			words = line.split()
			cmd = words[0].lower()
			if cmd == "user":
				reply("230 Welcome")
			elif line == "opts utf8 on":
				reply("202 Always using utf8")
			elif cmd == "syst":
				reply("215 UNIX Type: L8")
			elif cmd == "site":
				reply("500 No custom features")
			elif cmd == "pwd":
				reply("257 in \"/\"")
			elif cmd == "cwd":
				reply("250 now in \"/\"")
			elif cmd == "type":
				reply("200 Type set to: Binary.")
			elif cmd == "pasv":
				reply("227 Entering passive mode (192,168,122,1,8,74)")
			elif cmd == "noop":
				reply("200 Okay")
			elif cmd == "list":
				reply("125 Transfer starting")
				size_str = ("%8s" % (len(returned_file),)).encode("UTF-8")
				line = b"-rw-r--r--   1 root    root    "+size_str+b" Jan 01  1970 bored.exe\r\n"
				send_data_sock(line)
				reply("200 Transfer complete")
			elif cmd == "size":
				reply("213 "+str(len(returned_file)))
			elif cmd == "retr":
				reply("125 Transfer starting")
				send_data_sock(returned_file)
				reply("200 Transfer complete")
			else:
				reply("500 Unknown command "+cmd)
