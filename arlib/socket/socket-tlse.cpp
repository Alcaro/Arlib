#include "socket.h"

#ifdef ARLIB_SSL_TLSE
extern "C" {
#include "tlse.h"
}
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

// separate context here to ensure they're not loaded multiple times, saves memory and time
static TLSContext* rootcerts;

static void initialize()
{
	if (rootcerts) return;
	
	rootcerts = tls_create_context(false, TLS_V12);
	
#ifdef __unix__
	DIR* dir = opendir("/etc/ssl/certs/");
	uint8_t* cert = NULL;
	off_t cert_buf_len = 0;
	
	if (dir)
	{
		while (true)
		{
			struct dirent* entry = readdir(dir);
			if (!entry) break;
			char name[256];
			snprintf(name, sizeof(name), "%s%s", "/etc/ssl/certs/", entry->d_name);
			
			if (!strstr(name, "DST_Root")) continue;
			
			struct stat s;
			if (stat(name, &s) == 0 && (s.st_mode & S_IFREG))
			{
				if (s.st_size > cert_buf_len)
				{
					free(cert);
					cert_buf_len = s.st_size;
					cert = (uint8_t*)malloc(cert_buf_len);
				}
				
				int fd = open(name, O_RDONLY);
				if (fd >= 0)
				{
					read(fd, cert, s.st_size);
					tls_load_root_certificates(rootcerts, cert, s.st_size);
				}
			}
		}
		closedir(dir);
	}
	free(cert);
#else
#error unsupported
#endif
}

class socketssl_impl : public socketssl {
public:
	socket* sock;
	TLSContext* ssl;
	
	static int verify(struct TLSContext *context, struct TLSCertificate **certificate_chain, int len) {
    int i;
    int err;
    if (certificate_chain) {
        for (i = 0; i < len; i++) {
            struct TLSCertificate *certificate = certificate_chain[i];
            // check validity date
            err = tls_certificate_is_valid(certificate);
printf("chaine=%i\n",err);
            if (err)
                return err;
            // check certificate in certificate->bytes of length certificate->len
            // the certificate is in ASN.1 DER format
        }
    }
    // check if chain is valid
    err = tls_certificate_chain_is_valid(certificate_chain, len);
printf("chainvalid=%i\n",err);
    if (err)
        return err;

    const char *sni = tls_sni(context);
    if ((len > 0) && (sni)) {
        err = tls_certificate_valid_subject(certificate_chain[0], sni);
printf("sni=%s %i\n",sni,err);
        if (err)
            return err;
    }

    // Perform certificate validation agains ROOT CA
    err = tls_certificate_chain_is_valid_root(rootcerts, certificate_chain, len);
printf("rooted=%i\n",err);
    if (err)
        return err;

    //return certificate_expired;
    //return certificate_revoked;
    //return certificate_unknown;
    return no_error;
}
	
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
		tls_consume_stream(ssl, in, inlen, verify);
	}
	
	void error()
	{
		//tls_close_notify(ssl);
		//process(false);
		tls_destroy_context(ssl);
		delete sock;
		ssl = NULL;
	}
	
	static socketssl_impl* create(socket* parent, const char * domain, bool permissive)
	{
		if (!parent) return NULL;
		
		socketssl_impl* ret = new socketssl_impl();
		ret->sock = parent;
		ret->fd = get_fd(parent);
		ret->ssl = tls_create_context(false, TLS_V12);
		
		tls_client_connect(ret->ssl);
		tls_sni_set(ret->ssl, domain);
		
		while (!tls_established(ret->ssl))
		{
			ret->process(true);
		}
		
		return ret;
	}
	
	int recv(uint8_t* data, int len)
	{
		if (!ssl) return -1;
		
		process(true);
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
		tls_close_notify(ssl);
		process(false);
		error();
	}
};

socketssl* socketssl::create(socket* parent, const char * domain, bool permissive)
{
	initialize();
	return socketssl_impl::create(parent, domain, permissive);
}

socketssl* socketssl::unserialize(socket* inner, const uint8_t* data, size_t len)
{
	initialize();
	return NULL;
}
#endif
