#include "socket.h"
#include <stdio.h>

#undef socket
#if defined(_WIN32)
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#define MSG_NOSIGNAL 0
	#define MSG_DONTWAIT 0
	#define close closesocket
	#define usleep(n) Sleep(n/1000)
	#ifdef _MSC_VER
		#pragma comment(lib, "ws2_32.lib")
	#endif
#else
	#include <netdb.h>
	#include <errno.h>
	#include <unistd.h>
	#include <fcntl.h>
#endif

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
	
	int fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
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
	//let read0 block on windows
//#ifdef _WIN32
//	u_long yes = 1;
//	ioctlsocket(fd, FIONBIO, &yes);
//#else
//	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0)|O_NONBLOCK);
//#endif
	
	freeaddrinfo(addr);
	return fd;
}

#define socket socket_t
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
	
	int recv(uint8_t* data, int len)
	{
		return fixret(::recv(fd, (char*)data, len, MSG_NOSIGNAL|MSG_DONTWAIT));
	}
	
	int send0(const uint8_t* data, int len)
	{
printf("snd=%i\n",len);
		return fixret(::send(fd, (char*)data, len, MSG_NOSIGNAL|MSG_DONTWAIT));
	}
	
	int send1(const uint8_t* data, int len)
	{
printf("snd=%i\n",len);
		return fixret(::send(fd, (char*)data, len, MSG_NOSIGNAL));
	}
	
	~socket_impl()
	{
		close(fd);
	}
};

static socket* socket_wrap(int fd)
{
	if (fd<0) return NULL;
	return new socket_impl(fd);
}

#ifdef __unix__
socket* socket::create_from_fd(int fd)
{
	return socket_wrap(fd);
}
#endif

socket* socket::create(const char * domain, int port)
{
	return socket_wrap(connect(domain, port));
}

//static socket* create_async(const char * domain, int port);
//static socket* create_udp(const char * domain, int port);

//int socket::select(socket* * socks, int nsocks, int timeout_ms)
//{
//	return -1;
//}
