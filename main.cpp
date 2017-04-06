#include "arlib.h"

static array<byte> recvall(socket* sock, unsigned int len)
{
	array<byte> ret;
	ret.resize(len);
	
	size_t pos = 0;
	while (pos < len)
	{
		int part = sock->recv(ret.slice(pos, (pos==0)?2:1), true); // funny slicing to ensure partial reads are processed sensibly
		if (part<0) return NULL;
		pos += part;
	}
	return ret;
}
int ymain()
{
	//socket* s = socket::create("google.com", 80);
	socketssl* s = socketssl::create("google.com", 443);
	//s->sendp("GET / HTTP/1.1\nHost: google.com\nConnection: close\n\n", false);
	s->send("GET / HTTP/1.1\nHost: google.com\nConnection: close\n\n");
	puts(string(recvall(s, 200)));
	return 0;
}
