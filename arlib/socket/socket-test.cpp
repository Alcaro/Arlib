#include "../arlib.h"
#include "../test.h"

//TODO:
//- fetch howsmyssl, ensure the only failure is the session cache
//- ensure Subject Name is verified: fetch https://172.217.18.142/ (IP of google.com)
//- ensure bad roots are rejected: fetch https://badfish.filippo.io/
//- ensure bad certs are accepted with verification off

#ifdef ARLIB_TEST
//not in socket.h because this shouldn't really be used for anything, blocking is evil
static array<byte> recvall(socket* sock, unsigned int len)
{
	array<byte> ret;
	while (ret.size() < len)
	{
		maybe<array<byte>> part = sock->recv(true);
		assert_ret(part, NULL);
		assert_ret(part.value.size() != 0, NULL);
		ret += part.value;
	}
	return ret;
}

static void clienttest(socket* rs)
{
	const char http_get[] = "GET / HTTP/1.1\nHost: example.com\nConnection: close\n\n"; // wrong vhost - all we want to know is whether server speaks HTTP
	
	autoptr<socket> s = rs;
	assert(s);
	assert(s->send(http_get) == (int)strlen(http_get));
	
	//no need for sophisticated tests, just 'can it connect to google' is good enough
	array<byte> ret = recvall(s, 4);
	assert(ret.size() >= 4);
	assert(!memcmp(ret.data(), "HTTP", 4));
}

test("plain connection") { clienttest(socket::create("google.com", 80)); }
test("SSL client") { clienttest(socketssl::create("google.com", 443)); }
test("SSL permissiveness")
{
	autoptr<socket> s;
	assert(!(s=socketssl::create("172.217.18.142", 443))); // invalid subject name (this is Google)
	assert( (s=socketssl::create("172.217.18.142", 443, true)));
	assert(!(s=socketssl::create("badfish.filippo.io", 443))); // invalid cert root
	assert( (s=socketssl::create("badfish.filippo.io", 443, true)));
}

void listentest(const char * localhost)
{
	autoptr<socketlisten> l = socketlisten::create(7777);
	assert(l);
	autoptr<socket> c1 = socket::create(localhost, 7777);
	assert(c1);
	//socket* lr = l; // can't select &l because autoptr<socketlisten>* isn't socket**
	//assert(socket::select(&lr, 1, 100) == 0); // apparently the connection takes a while to make it through the kernel, at least on Windows
#ifdef _WIN32
	Sleep(50);
#endif
	autoptr<socket> c2 = l->accept();
	assert(c2);
	
	delete l.release();
	
	c1->send("foo");
	c2->send("bar");
	
	array<byte> ret;
	ret = recvall(c1, 3);
	assert(ret.size() == 3);
	assert(!memcmp(ret.data(), "bar", 3));
	
	ret = recvall(c2, 3);
	assert(ret.size() == 3);
	assert(!memcmp(ret.data(), "foo", 3));
}

test("listen on localhost") { listentest("localhost"); }
test("listen on 127.0.0.1") { listentest("127.0.0.1"); }
test("listen on ::1")       { listentest("::1"); }
#endif
