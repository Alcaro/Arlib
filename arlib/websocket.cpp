#ifdef ARLIB_SOCKET
#include "websocket.h"
#include "http.h"
#include "endian.h"
#include "stringconv.h"

bool websocket::connect(cstring target, arrayview<string> headers)
{
	http::location loc;
	if (!http::parseUrl(target, false, loc)) return false;
	if (loc.proto == "wss") sock = socketssl::create(loc.domain, loc.port ? loc.port : 443);
	if (loc.proto == "ws")  sock =    socket::create(loc.domain, loc.port ? loc.port : 80);
	if (!sock) return false;
	
	sock->send("GET "+loc.loc+" HTTP/1.1\r\n"
						 "Host: "+loc.domain+"\r\n"
						 "Connection: upgrade\r\n"
						 "Upgrade: websocket\r\n"
						 //"Origin: "+loc.domain+"\r\n"
						 "Sec-WebSocket-Version: 13\r\n"
						 "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n" // TODO: de-hardcode this
						 );
	
	for (cstring s : headers)
	{
		sock->send(s+"\r\n");
	}
	sock->send("\r\n");
	
	msg.resize(4096);
	int bytesdone = 0;
	while (true)
	{
		int bytes = sock->recv(msg.skip(bytesdone), true);
		if (bytes<0) return false;
		
	again: ;
		size_t n = msg.slice(bytesdone, bytes).find('\n');
		if (n != (size_t)-1)
		{
			n += bytesdone;
			bytesdone += bytes;
			
			string line = msg.slice(0, n);
			if (line.endswith("\r")) line = line.substr(0, ~1);
			msg = msg.slice(n+1, msg.size()-n-1);
			msg.resize(4096);
			bytesdone -= n+1;
			if (line.startswith("HTTP") && !line.startswith("HTTP/1.1 101 ")) return false;
			if (line == "")
			{
				msg.resize(bytesdone);
				return true;
			}
			bytes = bytesdone;
			bytesdone = 0;
			goto again;
		}
		else bytesdone += bytes;
	}
	return true;
}

void websocket::fetch(bool block)
{
	uint8_t bytes[4096];
	int nbyte = sock->recv(bytes, block);
	if (nbyte < 0)
	{
		sock = NULL;
	}
	if (nbyte > 0)
	{
		msg += arrayview<byte>(bytes, nbyte);
	}
}

bool websocket::ready(bool block, size_t* start, size_t* size, uint8_t key[4])
{
	if (!sock) return NULL;
	
	if (msg.size() < 2) { fetch(block); block=false; }
	if (msg.size() < 2) return false;
	
	uint8_t headsizespec = msg[1]&0x7F;
	uint8_t headsize = 2;
	if (msg[1] & 0x80) headsize += 4;
	if (headsizespec == 126) headsize += 2;
	if (headsizespec == 127) headsize += 8;
	
	if (msg.size() < headsize) { fetch(block); block=false; }
	if (msg.size() < headsize) return false;
	
	size_t msgsize;
	if (headsizespec <= 125) msgsize = headsize + headsizespec;
	if (headsizespec == 126) msgsize = headsize + bigend<uint16_t>(msg.slice(2, 2));
	if (headsizespec == 127) msgsize = headsize + bigend<uint64_t>(msg.slice(2, 8));
	
	if (msg.size() < msgsize) { fetch(block); block=false; }
	if (msg.size() < msgsize) return false;
	
	if (start)
	{
		*start = headsize;
		*size = msgsize-headsize;
		
		key[0] = 0;
		key[1] = 0;
		key[2] = 0;
		key[3] = 0;
		
		if (msg[1] & 0x80)
		{
			key[0] = msg[headsize-4+0];
			key[1] = msg[headsize-4+1];
			key[2] = msg[headsize-4+2];
			key[3] = msg[headsize-4+3];
		}
	}
	return true;
}

array<byte> websocket::recv(bool block)
{
	if (block) await();
	size_t start;
	size_t size;
	uint8_t key[4];
	ready(false, &start, &size, key);
	
	array<byte> ret = msg.slice(start, size);
	msg = msg.skip(start+size);
	
	if (key[0] || key[1] || key[2] || key[3])
	{
		for (size_t i=0;i<size;i++)
		{
			ret[i] ^= key[i&3];
		}
	}
	
	return ret;
}

void websocket::send(arrayview<byte> message)
{
	array<byte> header;
	header.append(0x80 | 0x02); // FIN, no masking, opcode 2 Binary
	if (message.size() <= 125)
	{
		header.append(message.size());
	}
	else if (message.size() <= 65535)
	{
		header.append(126);
		header += bigend<uint16_t>(message.size()).bytes();
	}
	else
	{
		header.append(127);
		header += bigend<uint64_t>(message.size()).bytes();
	}
	sock->send(header);
	sock->send(message);
}

#include "test.h"
#ifdef ARLIB_TEST
test()
{
	test_skip("kinda slow");
	
	websocket ws;
	assert(ws.connect("ws://echo.websocket.org")); // neither OpenSSL nor BearSSL can successfully connect to echo.websocket.org:443
	ws.send("hello");
	assert_eq((string)ws.recv(true), "hello");
	ws.send("hello");
	assert_eq((string)ws.recv(true), "hello");
	ws.send("hello");
	ws.send("hello");
	assert_eq((string)ws.recv(true), "hello");
	assert_eq((string)ws.recv(true), "hello");
}
#endif
#endif
