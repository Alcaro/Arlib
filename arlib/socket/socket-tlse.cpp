#include "socket.h"

#ifdef ARLIB_SSL_TLSE
extern "C" {
#include "tlse.h"
}

class socketssl_impl : public socketssl {
public:
	socket* sock;
	TLSContext* ssl;
	
	const uint8_t * unsent;
	uint32_t unsent_len;
	
	void process(bool block)
	{
		if (!ssl) return;
		
		unsigned int outlen = 0;
		const uint8_t * out = tls_get_write_buffer(ssl, &outlen);
		if (out && outlen)
		{
			if (sock->send(out, outlen) < 0) { error(); return; }
			tls_buffer_clear(ssl);
		}
		
		uint8_t in[0x2000];
		int inlen = sock->recv(in, sizeof(in));
		if (inlen<0) { error(); return; }
		tls_consume_stream(ssl, in, inlen, NULL);
	}
	
	void error()
	{
		ssl=NULL;
	}
	
	static socketssl_impl* create(socket* parent, const char * domain, bool permissive)
	{
		if (!parent) return NULL;
		
		socketssl_impl* ret = new socketssl_impl();
		ret->sock = parent;
		ret->fd = get_fd(parent);
		ret->ssl = tls_create_context(false, TLS_V12);
		
		tls_client_connect(ret->ssl);
		
		while (!tls_established(ret->ssl))
		{
			ret->process(true);
		}
		
		return ret;
	}
	
	int recv(uint8_t* data, int len)
	{
		if (!ssl) return -1;
		
		process(false);
		return tls_read(ssl, data, len);
	}
	
	int send0(const uint8_t* data, int len)
	{
		if (!ssl) return -1;
		
		tls_write(ssl, (uint8_t*)data, len);
		process(false);
		return len;
	}
	
	int send1(const uint8_t* data, int len)
	{
		if (!ssl) return -1;
		
		tls_write(ssl, (uint8_t*)data, len);
		process(true);
		return len;
	}
	
	~socketssl_impl()
	{
		
	}
};

socketssl* socketssl::create(socket* parent, const char * domain, bool permissive)
{
	return socketssl_impl::create(parent, domain, permissive);
}

socketssl* socketssl::unserialize(socket* inner, const uint8_t* data, size_t len)
{
	return NULL;
}
#endif
