#include "socket.h"

//based on http://wayback.archive.org/web/20100528130307/http://www.coastrd.com/c-schannel-smtp
//but heavily rewritten for stability and compactness

#ifdef ARLIB_SSL_SCHANNEL
#ifndef _WIN32
#error SChannel only exists on Windows
#endif

#define SECURITY_WIN32
#undef bind
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <winsock.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <schannel.h>
#include <security.h>
#include <sspi.h>

namespace {

static SecurityFunctionTable* SSPI;
static CredHandle cred;

#define SSPIFlags \
	(ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | \
	 ISC_RET_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM)

static void initialize()
{
	if (SSPI) return;
	SSPI = InitSecurityInterfaceA();
	
	SCHANNEL_CRED SchannelCred = {};
	SchannelCred.dwVersion = SCHANNEL_CRED_VERSION;
	SchannelCred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS;
	
	SSPI->AcquireCredentialsHandleA(NULL, (char*)UNISP_NAME_A, SECPKG_CRED_OUTBOUND,
	                                NULL, &SchannelCred, NULL, NULL, &cred, NULL);
}

class socketssl_impl : public socketssl {
public:
	socket* this_sock;
	CtxtHandle this_ssl;
	SecPkgContext_StreamSizes this_bufsizes;
	
	BYTE* this_recv_buf;
	size_t this_recv_buf_len;
	BYTE* this_ret_buf;
	size_t this_ret_buf_len;
	
	bool in_handshake;
	
	void fetch(bool block)
	{
		int bytes = this_sock->recv(this_recv_buf+this_recv_buf_len, 1024, block);
		if (bytes < 0)
		{
			delete this_sock;
			this_sock = NULL;
		}
		if (bytes > 0)
		{
			this_recv_buf_len += bytes;
			if (this_recv_buf_len > 1024)
			{
				this_recv_buf = realloc(this_recv_buf, this_recv_buf_len + 1024);
			}
		}
	}
	
	void fetch() { fetch(true); }
	void fetchnb() { fetch(false); }
	
	
	void ret_realloc(int bytes)
	{
		if (bytes > 0)
		{
			this_ret_buf_len += bytes;
			if (this_ret_buf_len > 1024)
			{
				this_ret_buf = realloc(this_ret_buf, this_ret_buf_len + 1024);
			}
		}
	}
	
	
	BYTE* tmpptr()
	{
		return this_recv_buf + this_recv_buf_len;
	}
	
	
	void error()
	{
		SSPI->DeleteSecurityContext(&this_ssl);
		delete this_sock;
		this_sock = NULL;
	}
	
	void handshake()
	{
		if (!in_handshake) return;
		
		SecBuffer       InBuffers[2] = { { this_recv_buf_len, SECBUFFER_TOKEN, this_recv_buf }, { 0, SECBUFFER_EMPTY, NULL } };
		SecBuffer       OutBuffer = { 0, SECBUFFER_TOKEN, NULL };
		SecBufferDesc   InBufferDesc = { SECBUFFER_VERSION, 2, InBuffers };
		SecBufferDesc   OutBufferDesc = { SECBUFFER_VERSION, 1, &OutBuffer };
		
		DWORD ignore;
		SECURITY_STATUS scRet;
		scRet = SSPI->InitializeSecurityContextA(&cred, &this_ssl, NULL, SSPIFlags, 0, SECURITY_NATIVE_DREP,
		                                         &InBufferDesc, 0, NULL, &OutBufferDesc, &ignore, NULL);
		
		// according to the original program, extended errors are success
		// but they also hit the error handler below, so I guess it just sends an error to the server?
		// either way, ignore
		if (scRet == SEC_E_OK || scRet == SEC_I_CONTINUE_NEEDED)
		{
			if (OutBuffer.cbBuffer != 0 && OutBuffer.pvBuffer != NULL)
			{
				if (this_sock->send((BYTE*)OutBuffer.pvBuffer, OutBuffer.cbBuffer) < 0)
				{
					SSPI->FreeContextBuffer(OutBuffer.pvBuffer);
					error();
					return;
				}
				SSPI->FreeContextBuffer(OutBuffer.pvBuffer);
			}
		}
		
		if (scRet == SEC_E_INCOMPLETE_MESSAGE) return;
		
		if (scRet == SEC_E_OK)
		{
			in_handshake = false;
		}
		
		if (FAILED(scRet))
		{
			error();
			return;
		}
		
		// SEC_I_INCOMPLETE_CREDENTIALS is possible and means server requested client authentication
		// we don't support that, just ignore it
		
		if (InBuffers[1].BufferType == SECBUFFER_EXTRA)
		{
			memmove(this_recv_buf, this_recv_buf + (this_recv_buf_len - InBuffers[1].cbBuffer), InBuffers[1].cbBuffer);
			this_recv_buf_len = InBuffers[1].cbBuffer;
		}
		else this_recv_buf_len = 0;
	}
	
	bool handshake_first(const char * domain)
	{
		SecBuffer OutBuffer = { 0, SECBUFFER_TOKEN, NULL };
		SecBufferDesc OutBufferDesc = { SECBUFFER_VERSION, 1, &OutBuffer };
		
		DWORD ignore;
		if (SSPI->InitializeSecurityContextA(&cred, NULL, (char*)domain, SSPIFlags, 0, SECURITY_NATIVE_DREP,
		                                     NULL, 0, &this_ssl, &OutBufferDesc, &ignore, NULL)
		    != SEC_I_CONTINUE_NEEDED)
		{
			return false;
		}
		
		if (OutBuffer.cbBuffer!=0)
		{
			if (this_sock->send((BYTE*)OutBuffer.pvBuffer, OutBuffer.cbBuffer) < 0)
			{
				SSPI->FreeContextBuffer(OutBuffer.pvBuffer);
				error();
				return false;
			}
			SSPI->FreeContextBuffer(OutBuffer.pvBuffer); // Free output buffer.
		}
		
		in_handshake = true;
		while (in_handshake) { fetch(); handshake(); }
		return true;
	}
	
	bool init(socket* parent, const char * domain, bool permissive)
	{
		if (!parent) return false;
		
		this_sock = parent;
		fd = parent->get_fd();
		this_recv_buf = malloc(2048);
		this_recv_buf_len = 0;
		this_ret_buf = malloc(2048);
		this_ret_buf_len = 0;
		
		if (!handshake_first(domain)) return false;
		SSPI->QueryContextAttributes(&this_ssl, SECPKG_ATTR_STREAM_SIZES, &this_bufsizes);
		
		return (this_sock);
	}
	
	void process()
	{
		handshake();
		
		bool again = true;
		
		while (again)
		{
			again = false;
			
			SecBuffer       Buffers[4] = {
				{ this_recv_buf_len, SECBUFFER_DATA, this_recv_buf },
				{ 0, SECBUFFER_EMPTY, NULL },
				{ 0, SECBUFFER_EMPTY, NULL },
				{ 0, SECBUFFER_EMPTY, NULL },
			};
			SecBufferDesc   Message = { SECBUFFER_VERSION, 4, Buffers };
			
			SECURITY_STATUS scRet = SSPI->DecryptMessage(&this_ssl, &Message, 0, NULL);
			if (scRet == SEC_E_INCOMPLETE_MESSAGE) return;
			else if (scRet == SEC_I_RENEGOTIATE)
			{
				in_handshake = true;
			}
			else if (scRet != SEC_E_OK)
			{
				error();
				return;
			}
			
			this_recv_buf_len = 0;
			
			// Locate data and (optional) extra buffers.
			for (int i=0;i<4;i++)
			{
				if (Buffers[i].BufferType == SECBUFFER_DATA)
				{
					memcpy(this_ret_buf+this_ret_buf_len, Buffers[i].pvBuffer, Buffers[i].cbBuffer);
					ret_realloc(Buffers[i].cbBuffer);
					again = true;
				}
				if (Buffers[i].BufferType == SECBUFFER_EXTRA)
				{
					memmove(this_recv_buf, Buffers[i].pvBuffer, Buffers[i].cbBuffer);
					this_recv_buf_len = Buffers[i].cbBuffer;
				}
			}
		}
	}
	
	int recv(uint8_t* data, unsigned int len, bool block = false)
	{
		if (!this_sock) return -1;
		fetch(block);
		process();
		
		if (!this_ret_buf_len) return 0;
		
		unsigned ulen = len;
		int ret = (ulen < this_ret_buf_len ? ulen : this_ret_buf_len);
		memcpy(data, this_ret_buf, ret);
		memmove(this_ret_buf, this_ret_buf+ret, this_ret_buf_len-ret);
		this_ret_buf_len -= ret;
		return ret;
	}
	
	int sendp(const uint8_t* data, unsigned int len, bool block = true)
	{
		if (!this_sock) return -1;
		
		fetchnb();
		process();
		
		BYTE* sendbuf = tmpptr(); // let's reuse this
		
		unsigned int maxmsglen = 0x1000 - this_bufsizes.cbHeader - this_bufsizes.cbTrailer;
		if (len > maxmsglen) len = maxmsglen;
		
		memcpy(sendbuf+this_bufsizes.cbHeader, data, len);
		SecBuffer                Buffers[4] = {
			{ this_bufsizes.cbHeader,  SECBUFFER_STREAM_HEADER,  sendbuf },
			{ len,                     SECBUFFER_DATA,           sendbuf+this_bufsizes.cbHeader },
			{ this_bufsizes.cbTrailer, SECBUFFER_STREAM_TRAILER, sendbuf+this_bufsizes.cbHeader+len },
			{ 0,                       SECBUFFER_EMPTY,          NULL },
		};
		SecBufferDesc        Message = { SECBUFFER_VERSION, 4, Buffers };
		if (FAILED(SSPI->EncryptMessage(&this_ssl, 0, &Message, 0))) { error(); return -1; }
		
		if (this_sock->send(sendbuf, Buffers[0].cbBuffer + Buffers[1].cbBuffer + Buffers[2].cbBuffer) < 0) error();
		
		return len;
	}
	
	~socketssl_impl()
	{
		error();
		free(this_recv_buf);
		free(this_ret_buf);
	}
};

}

socketssl* socketssl::create(socket* parent, const char * domain, bool permissive)
{
	initialize();
	socketssl_impl* ret = new socketssl_impl();
	if (!ret->init(parent, domain, permissive)) { delete ret; return NULL; }
	else return ret;
}
#endif
