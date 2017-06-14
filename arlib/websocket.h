#ifdef ARLIB_SOCKET
#pragma once
#include "socket.h"

class websocket : nocopy {
	autoptr<socket> sock;
	array<byte> msg;
	
public:
	bool connect(cstring target, arrayview<string> headers = NULL);
	
private:
	void fetch(bool block);
	bool ready(bool block, size_t* start, size_t* size, uint8_t key[4]);
public:
	bool ready()
	{
		return ready(false, NULL, NULL, NULL);
	}
	void await()
	{
		while (!ready(true, NULL, NULL, NULL)) {}
	}
	array<byte> recv(bool block = false);
	string recvstr(bool block = false)
	{
		return (string)recv(block);
	}
	
	void send(arrayview<byte> message);
	void send(cstring message)
	{
		send(message.bytes());
	}
	
	bool isOpen()
	{
		return sock;
	}
	void close()
	{
		sock = NULL;
	}
	
	//If this key is returned, call .recv(). May not necessarily return anything.
	void monitor(socket::monitor& mon, void* key) { mon.add(sock, key, true, false); }
};


#endif
