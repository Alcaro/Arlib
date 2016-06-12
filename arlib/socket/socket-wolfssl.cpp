#include "socket.h"

#ifdef ARLIB_SSL_WOLFSSL
#include <wolfssl/ssl.h>
#ifdef __unix__
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#endif

static WOLFSSL_CTX* ctx;

class socketssl_impl : public socketssl {
public:
	socket* sock;
	WOLFSSL* ssl;
	bool nonblock;
	
	socketssl_impl(socket* parent, const char * domain, bool permissive)
	{
		sock = parent;
		fd = get_fd(parent);
		ssl = wolfSSL_new(ctx);
		
		wolfSSL_SetIOReadCtx(ssl, this);
		wolfSSL_SetIOWriteCtx(ssl, this);
	}
	
	/*private*/ static int recv_raw(WOLFSSL* ssl, char* buf, int sz, void* ctx)
	{
		socketssl_impl* this_ = (socketssl_impl*)ctx;
		int ret = this_->sock->recv((uint8_t*)buf, sz);
		if (ret==0) return WOLFSSL_CBIO_ERR_WANT_READ;
		if (ret<0) return WOLFSSL_CBIO_ERR_GENERAL;
		return ret;
	}
	
	/*private*/ static int send_raw(WOLFSSL* ssl, char* buf, int sz, void* ctx)
	{
		socketssl_impl* this_ = (socketssl_impl*)ctx;
		int ret;
		if (this_->nonblock) ret = this_->sock->send0((uint8_t*)buf, sz);
		else                 ret = this_->sock->send1((uint8_t*)buf, sz);
		if (ret==0) return WOLFSSL_CBIO_ERR_WANT_WRITE;
		if (ret<0) return WOLFSSL_CBIO_ERR_GENERAL;
		return ret;
	}
	
	/*private*/ int fixret(int ret)
	{
		if (ret > 0) return ret;
		
		int err = wolfSSL_get_error(ssl, ret);
		if (err==SSL_ERROR_WANT_READ || err==SSL_ERROR_WANT_WRITE) return 0;
		return e_broken;
	}
	
	int recv(uint8_t* data, int len)
	{
		nonblock = false;
		return fixret(wolfSSL_read(ssl, data, len));
	}
	
	int send0(const uint8_t* data, int len)
	{
		nonblock = true;
		return fixret(wolfSSL_write(ssl, data, len));
	}
	
	int send1(const uint8_t* data, int len)
	{
		nonblock = false;
		return fixret(wolfSSL_write(ssl, data, len));
	}
	
	virtual size_t serialize(uint8_t* data, size_t len) { return 0; }
	
	~socketssl_impl()
	{
		wolfSSL_free(ssl);
		delete sock;
	}
};

static void initialize()
{
	static bool initialized = false;
	if (initialized) return;
	initialized = true;
	
	wolfSSL_Init();
	
	ctx = wolfSSL_CTX_new(wolfTLSv1_2_client_method());
	if (!ctx) return;
	
	wolfSSL_SetIORecv(ctx, socketssl_impl::recv_raw);
	wolfSSL_SetIOSend(ctx, socketssl_impl::send_raw);
	
#ifdef __unix__
	//mostly copypasta from wolfSSL_CTX_load_verify_locations,
	//minus the abort-on-first-error thingy
	struct dirent* entry;
	DIR* dir = opendir("/etc/ssl/certs/");
	
	if (dir)
	{
		while (true)
		{
			entry = readdir(dir);
			if (!entry) break;
			char name[256];
			snprintf(name, sizeof(name), "%s%s", "/etc/ssl/certs/", entry->d_name);
			
			struct stat s;
			if (stat(name, &s) == 0 && (s.st_mode & S_IFREG))
			{
				wolfSSL_CTX_load_verify_locations(ctx, name, NULL);
			}
		}
		closedir(dir);
	}
#else
#error unsupported
#endif
}

socketssl* socketssl::create(socket* parent, const char * domain, bool permissive)
{
	initialize();
	if (!ctx) return NULL;
	
	return new socketssl_impl(parent, domain, permissive);
}

socketssl* socketssl::unserialize(const uint8_t* data, size_t len)
{
	initialize();
	if (!ctx) return NULL;
	
	return NULL;
}
#endif
