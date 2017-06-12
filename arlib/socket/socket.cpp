#include "socket.h"
#include <stdio.h>

#undef socket
#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <mstcpip.h>
	#define MSG_NOSIGNAL 0
	#define MSG_DONTWAIT 0
	#define SOCK_CLOEXEC 0
	#define close closesocket
	#ifdef _MSC_VER
		#pragma comment(lib, "ws2_32.lib")
	#endif
#else
	#include <netdb.h>
	#include <errno.h>
	#include <unistd.h>
	#include <fcntl.h>
	
	#include <netinet/tcp.h>
#endif

//wrapper because 'socket' is a type in this code, so socket(2) needs another name
static int mksocket(int domain, int type, int protocol) { return socket(domain, type|SOCK_CLOEXEC, protocol); }
#define socket socket_t

namespace {

static void initialize()
{
#ifdef _WIN32 // lol
	static bool initialized = false;
	if (initialized) return;
	initialized = true;
	
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

static int setsockopt(int socket, int level, int option_name, const void * option_value, socklen_t option_len)
{
	return ::setsockopt(socket, level, option_name, (char*)/*lol windows*/option_value, option_len);
}

static int setsockopt(int socket, int level, int option_name, int option_value)
{
	return setsockopt(socket, level, option_name, &option_value, sizeof(option_value));
}

static int connect(const char * domain, int port)
{
	initialize();
	
	char portstr[16];
	sprintf(portstr, "%i", port);
	
	addrinfo hints;
	memset(&hints, 0, sizeof(addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	
	addrinfo * addr = NULL;
	getaddrinfo(domain, portstr, &hints, &addr);
	if (!addr) return -1;
	
	int fd = mksocket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
#ifndef _WIN32
	//because 30 second pauses are unequivocally detestable
	timeval timeout;
	timeout.tv_sec = 4;
	timeout.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
#endif
	if (connect(fd, addr->ai_addr, addr->ai_addrlen) != 0)
	{
		freeaddrinfo(addr);
		close(fd);
		return -1;
	}
	
#ifndef _WIN32
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, 1); // enable
	setsockopt(fd, SOL_TCP, TCP_KEEPCNT, 3); // ping count before the kernel gives up
	setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, 30); // seconds idle until it starts pinging
	setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, 10); // seconds per ping once the pings start
#else
	struct tcp_keepalive keepalive = {
		1,       // SO_KEEPALIVE
		30*1000, // TCP_KEEPIDLE in milliseconds
		3*1000,  // TCP_KEEPINTVL
		//On Windows Vista and later, the number of keep-alive probes (data retransmissions) is set to 10 and cannot be changed.
		//https://msdn.microsoft.com/en-us/library/windows/desktop/dd877220(v=vs.85).aspx
		//so no TCP_KEEPCNT; I'll reduce INTVL instead. And a polite server will RST anyways.
	};
	u_long ignore;
	WSAIoctl(fd, SIO_KEEPALIVE_VALS, &keepalive, sizeof(keepalive), NULL, 0, &ignore, NULL, NULL);
#endif
	
	freeaddrinfo(addr);
	return fd;
}

} // close namespace

//MSG_DONTWAIT is usually better, but accept() and connect() don't take that argument
void socket::setblock(int fd, bool newblock)
{
#ifdef _WIN32
	u_long nonblock = !newblock;
	ioctlsocket(fd, FIONBIO, &nonblock);
#else
	int flags = fcntl(fd, F_GETFL, 0);
	flags &= ~O_NONBLOCK;
	if (!newblock) flags |= O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);
#endif
}

namespace {

class socket_impl : public socket {
public:
	socket_impl(int fd) { this->fd = fd; }
	
	/*private*/ int fixret(int ret)
	{
		if (ret > 0) return ret;
		if (ret == 0) return e_closed;
#ifdef __unix__
		if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
#endif
#ifdef _WIN32
		if (ret < 0 && WSAGetLastError() == WSAEWOULDBLOCK) return 0;
#endif
		return e_broken;
	}
	
	int recv(arrayvieww<byte> data, bool block = false)
	{
#ifdef _WIN32 // for Linux, MSG_DONTWAIT is enough
		socket::setblock(this->fd, block);
#endif
		return fixret(::recv(this->fd, (char*)data.ptr(), data.size(), MSG_NOSIGNAL | (block ? 0 : MSG_DONTWAIT)));
	}
	
	int sendp(arrayview<byte> data, bool block = true)
	{
#ifdef _WIN32
		socket::setblock(this->fd, block);
#endif
		return fixret(::send(fd, (char*)data.ptr(), data.size(), MSG_NOSIGNAL | (block ? 0 : MSG_DONTWAIT)));
	}
	
	~socket_impl()
	{
		if (fd>=0) close(fd);
	}
};

static socket* socket_wrap(int fd)
{
	if (fd<0) return NULL;
	return new socket_impl(fd);
}

}

socket* socket::create_from_fd(int fd)
{
	return socket_wrap(fd);
}

socket* socket::create(cstring domain, int port)
{
	return socket_wrap(connect(domain, port));
}

//static socket* socket::create_udp(const char * domain, int port);

#ifdef __unix__
size_t socket::select(arrayview<socket*> socks, bool* can_read, bool* can_write, int timeout_ms)
{
	array<int> fds;
	fds.resize(socks.size());
	for (size_t i=0;i<socks.size();i++) fds[i] = socks[i]->fd;
	return fd_monitor_oneshot(fds, can_read, can_write, timeout_ms);
}
#endif


#if defined(__unix__) && defined(ARLIB_THREAD)
#include "../thread.h"
#include "../file.h"

class sockwrap : public socket {
	socket* inner;
	mutex mut;
	array<byte> tosend;
	
public:
	/*private*/ void process()
	{
		if (!tosend) return;
		
		int bytes = inner->sendp(tosend, false);
		if (bytes > 0)
		{
			tosend = tosend.skip(bytes);
		}
		if (bytes < 0)
		{
			delete inner;
			inner = NULL;
			tosend.reset();
		}
		if (tosend.size()) fd_mon_thread(fd, NULL, bind_this(&sockwrap::activity));
		else fd_mon_thread(fd, NULL, NULL);
	}
	/*private*/ void activity(int fd)
	{
		synchronized(mut)
		{
			process();
		}
	}
	
	int recv(arrayvieww<byte> data, bool block)
	{
		synchronized(mut)
		{
			if (!inner) return -1;
			process();
			if (!inner) return -1;
			return inner->recv(data, block);
		}
		return -1; // unreachable
	}
	int sendp(arrayview<byte> data, bool block)
	{
		synchronized(mut)
		{
			if (!inner) return -1;
			int bytes = 0;
			if (!tosend)
			{
				bytes = inner->sendp(data, false);
				if ((size_t)bytes == data.size()) return data.size();
				if (bytes < 0) bytes = 0;
			}
			
			tosend += data.skip(bytes);
			process();
			if (inner) return data.size();
			else return -1;
		}
		return -1; // unreachable
	}
	~sockwrap()
	{
		synchronized(mut)
		{
			fd_mon_thread(fd, NULL, NULL);
			delete inner;
		}
	}
	sockwrap(socket* inner) : socket(inner->get_fd()), inner(inner) {}
};

socket* socket::bufwrap(socket* inner)
{
	if (!inner) return NULL;
	return new sockwrap(inner);
}
#endif


static MAYBE_UNUSED int socketlisten_create_ip4(int port)
{
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	
	int fd = mksocket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) goto fail;
	
	if (bind(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) goto fail;
	if (listen(fd, 10) < 0) goto fail;
	return fd;
	
fail:
	close(fd);
	return -1;
}

static int socketlisten_create_ip6(int port)
{
	struct sockaddr_in6 sa; // IN6ADDR_ANY_INIT should work, but doesn't.
	memset(&sa, 0, sizeof(sa));
	sa.sin6_family = AF_INET6;
	sa.sin6_addr = in6addr_any;
	sa.sin6_port = htons(port);
	
	int fd = mksocket(AF_INET6, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	
	if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, false) < 0) goto fail;
	if (bind(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) goto fail;
	if (listen(fd, 10) < 0) goto fail;
	return fd;
	
fail:
	close(fd);
	return -1;
}

socketlisten* socketlisten::create(int port)
{
	initialize();
	
	int fd = -1;
	if (fd<0) fd = socketlisten_create_ip6(port);
#if defined(_WIN32) && _WIN32_WINNT < 0x0600
	//Windows XP can't dualstack the v6 addresses, so let's keep the fallback
	if (fd<0) fd = socketlisten_create_ip4(port);
#endif
	if (fd<0) return NULL;
	
	setblock(fd, false);
	return new socketlisten(fd);
}

socket* socketlisten::accept()
{
	return socket_wrap(::accept(this->fd, NULL,NULL));
}

socketlisten::~socketlisten() { close(this->fd); }
