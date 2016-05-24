#include <stdbool.h>
#include <stdint.h>

class socket {
protected:
	socket(){}
public:
	//Returns NULL on connection failure.
	static socket* create(const char * domain, int port);
	//Always succeeds. If the server can't be contacted, 
	static socket* create_async(const char * domain, int port);
	static socket* create_udp(const char * domain, int port);
	
	//Negative means error; close the socket and create a new one.
	//Positive is number of bytes handled. Zero means try again, and can be treated as byte count.
	virtual int read(uint8_t* data, int len) = 0;
	virtual int write(const uint8_t* data, int len) = 0;
	
	int writeb(const uint8_t* data, int len);
	
	//Returns an index to the sockets array, or negative if timeout expires.
	//Negative timeouts mean wait forever.
	static int select(socket* * sockets, int count, int timeout_ms);
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
