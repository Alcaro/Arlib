#pragma once
#include "socket.h"
#include "bytestream.h"

struct socks5_par {
	runloop* loop;
	socket* to_proxy;
	string target;
	uint16_t port;
};
socket* wrap_socks5(const socks5_par& param);
