#pragma once
#include "socket.h"

class socks5 {
	string m_host;
	uint16_t m_port;
	
public:
	static async<autoptr<socket2>> create(cstring proxy_host, uint16_t proxy_port, cstring host, uint16_t port);
	
	// If host is empty, this connects to the target directly.
	void configure(cstring host, uint16_t port = 1080) { m_host = host; m_port = port; }
	
	// These functions act identically to their counterparts in socket2::.
	inline async<autoptr<socket2>> create(cstring domain, uint16_t port)
	{
		if (!m_host)
			return socket2::create(domain, port);
		return create(m_host, m_port, domain, port);
	}
#ifdef ARLIB_SSL
	inline async<autoptr<socket2>> create_ssl(cstring domain, uint16_t port)
	{
		co_return co_await socket2::wrap_ssl(co_await create(domain, port), domain);
	}
#endif
	inline async<autoptr<socket2>> create_sslmaybe(bool ssl, cstring domain, uint16_t port)
	{
		co_return co_await socket2::wrap_sslmaybe(ssl, co_await create(domain, port), domain);
	}
	
	void auto_configure();
};
