#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define socket socket_t
class socket {
protected:
	socket(){}
	int fd; // Used by select().
	
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
	//send() sends all bytes before returning. send1() tries to send at least one byte. send0() can send zero, but is fully nonblocking.
	//For UDP sockets, partial reads or writes aren't possible; you always get one or zero packets.
	virtual int recv(uint8_t* data, int len) = 0;
	virtual int send0(const uint8_t* data, int len) = 0;
	virtual int send1(const uint8_t* data, int len) = 0;
	int send(const uint8_t* data, int len)
	{
		int sent = 0;
		while (sent < len)
		{
			int here = send1(data+sent, len-sent);
			if (here<0) return here;
			sent += here;
		}
		return len;
	}
	
	//Convenience functions for handling textual data.
	int recv(char* data, int len) { int ret = recv((uint8_t*)data, len-1); if (ret>=0) data[ret]='\0'; else data[0]='\0'; return ret; }
	int send0(const char * data) { return send0((uint8_t*)data, strlen(data)); }
	int send1(const char * data) { return send1((uint8_t*)data, strlen(data)); }
	int send (const char * data) { return send ((uint8_t*)data, strlen(data)); }
	
	//Returns an index to the sockets array, or negative if timeout expires.
	//Negative timeouts mean wait forever.
	//It's possible that an active socket returns zero bytes.
	//However, this is guaranteed to happen rarely enough that repeatedly select()ing will leave the CPU mostly idle.
	static int select(socket* * socks, int nsocks, int timeout_ms = -1);
	
	virtual ~socket() {}
	
	//Use only if you're up to no good.
	//Remember to serialize the SSL socket if this is used.
#ifdef __linux__
	static socket* create_from_fd(int fd);
	int get_fd() { return fd; }
#endif
};

class sslsocket : public socket {
protected:
	sslsocket();
public:
	//If 'permissive' is true, the server certificate won't be verified.
	static sslsocket* create(const char * domain, int port, bool permissive=false)
	{
		return sslsocket::create(socket::create(domain, port), domain, permissive);
	}
	static sslsocket* create(socket* parent, const char * domain, bool permissive=false);
};
