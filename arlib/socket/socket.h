#include "../global.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define socket socket_t
class socket : nocopy {
protected:
	socket(){}
	int fd; // Used by select().
	
	//deallocates the socket, returning its fd, while letting the fd remain valid
	static int decompose(socket* sock) { int ret = sock->fd; sock->fd=-1; delete sock; return ret; }
	
public:
	//Returns NULL on connection failure.
	static socket* create(const char * domain, int port);
	//Always succeeds. If the server can't be contacted, returns failure on first write or read.
	static socket* create_async(const char * domain, int port);
	static socket* create_udp(const char * domain, int port);
	
	enum {
		e_closed = -1, // Remote host set the TCP EOF flag.
		e_broken = -2, // Connection was forcibly torn down.
		e_udp_too_big = -3, // Attempted to process an unacceptably large UDP packet.
		e_ssl_failure = -4, // Certificate validation failed, no algorithms in common, or other SSL error.
	};
	
	//Negative means error, see above.
	//Positive is number of bytes handled. Zero means try again, and can be treated as byte count.
	//send() sends all bytes before returning. send1() waits until it can send at least one byte.
	//send0() can send zero, but is fully nonblocking.
	//For UDP sockets, partial reads or writes aren't possible; you always get one or zero packets.
	//recv() corresponds to send1(); recvnb() is send0(). There is no counterpart to send(); use socketbuffer if you need that.
	virtual int recv(uint8_t* data, unsigned int len, bool block = false) = 0;
	virtual int sendp(const uint8_t* data, unsigned int len, bool block = true) = 0;
	int send(const uint8_t* data, unsigned int len)
	{
		unsigned int sent = 0;
		while (sent < len)
		{
			int here = sendp(data+sent, len-sent);
			if (here<0) return here;
			sent += here;
		}
		return len;
	}
	
	//Convenience functions for handling textual data.
	int recv(char* data, unsigned int len, bool block = false)
	{
		int ret = recv((uint8_t*)data, len-1, block);
		if (ret >= 0) data[ret]='\0';
		else data[0]='\0';
		return ret;
	}
	int sendp(const char * data, bool block = true) { return sendp((uint8_t*)data, strlen(data), block); }
	int send (const char * data) { return send((uint8_t*)data, strlen(data)); }
	
	//Returns an index to the sockets array, or negative if timeout expires.
	//Negative timeouts mean wait forever.
	//It's possible that an active socket returns zero bytes.
	//However, this is guaranteed to happen rarely enough that repeatedly select()ing will leave the CPU mostly idle.
	//(It may be caused by packets with wrong checksum, SSL renegotiation, or whatever.)
	static int select(socket* * socks, unsigned int nsocks, int timeout_ms = -1);
	
	virtual ~socket() {}
	
	//Can be used to keep a socket alive across exec(). Don't use for an SSL socket.
	static socket* create_from_fd(int fd);
	int get_fd() { return fd; }
};

class socketssl : public socket {
protected:
	socketssl(){}
public:
	//If 'permissive' is true, expired and self-signed server certificates will be accepted.
	//Other invalid certs, such as ones for a different domain, may or may not be accepted.
	static socketssl* create(const char * domain, int port, bool permissive=false)
	{
		return socketssl::create(socket::create(domain, port), domain, permissive);
	}
	//On entry, this takes ownership of the socket. Even if connection fails, the socket may not be used anymore.
	//The socket must be a normal TCP socket. UDP and nested SSL is not supported.
	static socketssl* create(socket* parent, const char * domain, bool permissive=false);
	
	
	virtual void q(){}
	
	//Can be used to keep a socket alive across exec().
	//If successful, serialize() returns the the file descriptor needed to unserialize, and the socket is deleted.
	//If failure, negative return and nothing happens.
	virtual size_t serialize_size() { return 0; }
	virtual int serialize(uint8_t* data, size_t len) { return -1; }
	static socketssl* unserialize(int fd, const uint8_t* data, size_t len);
};
