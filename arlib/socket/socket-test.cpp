#include "socket.h"
#include "../test.h"

//TODO:
//- fetch howsmyssl, ensure the only failure is the session cache

#ifdef ARLIB_TEST
//not in socket.h because this shouldn't really be used for anything, blocking is evil
static array<byte> recvall(socket* sock, unsigned int len)
{
	array<byte> ret;
	ret.resize(len);
	
	size_t pos = 0;
	while (pos < len)
	{
		int part = sock->recv(ret.slice(pos, (pos==0)?2:1), true); // funny slicing to ensure partial reads are processed sensibly
		assert_ret(part >= 0, NULL);
		assert_ret(part > 0, NULL); // this is a blocking recv, returning zero is forbidden
		pos += part;
	}
	return ret;
}

static void clienttest(socket* rs)
{
	//returns whether the socket peer speaks HTTP
	//discards the actual response, and since the Host: header is silly, it's most likely some variant of 404 not found
	//also closes the socket
	
	autoptr<socket> s = rs;
	assert(s);
	
	//in HTTP, client talks first, ensure this doesn't return anything
	byte discard[1];
	assert(s->recv(discard) == 0);
	
	const char http_get[] =
		"GET / HTTP/1.1\r\n"
		"Host: example.com\r\n" // wrong host, but we don't care, all we care about is server returning a HTTP response
		"Connection: close\r\n"
		"\r\n";
	assert_eq(s->send(http_get), (int)strlen(http_get));
	
	array<byte> ret = recvall(s, 4);
	assert(ret.size() == 4);
	assert(!memcmp(ret.ptr(), "HTTP", 4));
}

test("plaintext TCP client") { test_skip("too slow"); clienttest(socket::create("google.com", 80)); }
test("SSL client") { test_skip("too slow"); clienttest(socketssl::create("google.com", 443)); }
test("SSL SNI") { test_skip("too slow"); clienttest(socketssl::create("git.io", 443)); }
test("SSL permissiveness (bad root)")
{
	test_skip("too slow");
	autoptr<socket> s;
	assert(!(s=socketssl::create("superfish.badssl.com", 443)));
	assert( (s=socketssl::create("superfish.badssl.com", 443, true)));
}
test("SSL permissiveness (bad name)")
{
	test_skip("too slow");
	autoptr<socket> s;
	assert(!(s=socketssl::create("wrong.host.badssl.com", 443)));
	assert( (s=socketssl::create("wrong.host.badssl.com", 443, true)));
}
test("SSL permissiveness (expired)")
{
	test_skip("too slow");
	autoptr<socket> s;
	assert(!(s=socketssl::create("expired.badssl.com", 443)));
	assert( (s=socketssl::create("expired.badssl.com", 443, true)));
}

#ifdef ARLIB_SSL_BEARSSL
static void ser_test(autoptr<socketssl>& s)
{
//int fd;
//s->serialize(&fd);
	
	socketssl* sp = s.release();
	assert(sp);
	assert(!s);
	int fd;
	array<byte> data = sp->serialize(&fd);
	assert(data);
	s = socketssl::deserialize(fd, data);
	assert(s);
}
test("SSL serialization")
{
	test_skip("too slow");
	autoptr<socketssl> s = socketssl::create("google.com", 443);
	testcall(ser_test(s));
	s->send("GET / HTTP/1.1\n");
	testcall(ser_test(s));
	s->send("Host: google.com\nConnection: close\n\n");
	testcall(ser_test(s));
	array<byte> bytes = recvall(s, 4);
	assert_eq(string(bytes), "HTTP");
	testcall(ser_test(s));
	bytes = recvall(s, 4);
	assert_eq(string(bytes), "/1.1");
}
#endif

static void listentest(const char * localhost, int port)
{
#ifdef __linux__
	test_skip("spurious failures due to TIME_WAIT (add SO_REUSEADDR?)");
#endif
	
	autoptr<socketlisten> l = socketlisten::create(port);
	assert(l);
	autoptr<socket> c1 = socket::create(localhost, port);
	assert(c1);
	
#ifdef _WIN32
	//apparently the connection takes a while to make it through the kernel, at least on windows
	//socket* lr = l; // can't select &l because autoptr<socketlisten>* isn't socket**
	//assert(socket::select(&lr, 1, 100) == 0); // TODO: enable select()
	Sleep(50);
#endif
	autoptr<socket> c2 = l->accept();
	assert(c2);
	
	l = NULL;
	
	c1->send("foo");
	c2->send("bar");
	
	array<byte> ret;
	ret = recvall(c1, 3);
	assert(ret.size() == 3);
	assert(!memcmp(ret.ptr(), "bar", 3));
	
	ret = recvall(c2, 3);
	assert(ret.size() == 3);
	assert(!memcmp(ret.ptr(), "foo", 3));
}

test("listen on localhost") { listentest("localhost", 7777); }
test("listen on 127.0.0.1") { listentest("127.0.0.1", 7778); }
test("listen on ::1")       { listentest("::1", 7779); }

#include "../runloop.h"
static void test_nonblock(cstring target, int port, bool ssl)
{
	test_skip("too slow");
	
	autoptr<runloop> loop = runloop_blocktest_create();
	
	//autoptr<socket> sock = socket::create(target, port);
	
	autoptr<socket> sock = socket::create_async(target, port);
	assert(sock);
	
	//ugly, but the alternative is nesting lambdas forever or busywait. I need a way to break it anyways
	function<void(socket*)> break_runloop = bind_lambda([&](socket*) { loop->exit(); });
	
	const char * http_get =
		"GET / HTTP/1.1\r\n"
		"Host: example.com\r\n"
		"Connection: close\r\n"
		"\r\n";
	
	sock->callback(loop, NULL, break_runloop);
	
	while (*http_get)
	{
		testcall(loop->enter());
		int bytes = sock->sendp(arrayview<byte>((uint8_t*)http_get, strlen(http_get)), false);
		assert(bytes >= 0);
		http_get += bytes;
	}
	
	sock->callback(loop, break_runloop, NULL);
	
	uint8_t buf[4];
	size_t n_buf = 0;
	while (n_buf < 4)
	{
		testcall(loop->enter());
		int bytes = sock->recv(arrayvieww<byte>(buf).skip(n_buf));
		assert(bytes >= 0);
		n_buf += bytes;
	}
	
	assert_eq(string(arrayview<byte>(buf)), "HTTP");
}

test("nonblocking TCP") { test_nonblock("192.41.192.145", 80, false); } // www.nic.ad.jp
//test("nonblocking DNS") { test_nonblock("www.nic.ad.jp", 80, false); } // both lookup and ping time for that one are 300ms
//test("nonblocking SSL") { test_nonblock("www.nic.ad.jp", 443, true); }
#endif
