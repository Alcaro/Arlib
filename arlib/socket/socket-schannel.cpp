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
	
	if (SSPI->AcquireCredentialsHandleA(NULL, (char*)UNISP_NAME_A, SECPKG_CRED_OUTBOUND,
	                                    NULL, &SchannelCred, NULL, NULL, &cred, NULL) != SEC_E_OK)
	{
		SSPI = NULL;
	}
}

class socketssl_impl : public socketssl {
public:
	socket* sock;
	CtxtHandle ssl;
	SecPkgContext_StreamSizes bufsizes;
	
	BYTE* recv_buf;
	size_t recv_buf_len;
	BYTE* ret_buf;
	size_t ret_buf_len;
	
	bool in_handshake;
	
	void fetch_realloc(int bytes)
	{
		if (bytes < 0)
		{
			delete sock;
			sock = NULL;
		}
		if (bytes > 0)
		{
			recv_buf_len += bytes;
			if (recv_buf_len > 1024)
			{
				recv_buf = realloc(recv_buf, recv_buf_len + 1024);
			}
		}
	}
	
	void fetch()
	{
		fetch_realloc(sock->recv(recv_buf+recv_buf_len, 1024));
	}
	
	void fetchnb()
	{
		fetch_realloc(sock->recvnb(recv_buf+recv_buf_len, 1024));
	}
	
	
	void ret_realloc(int bytes)
	{
		if (bytes > 0)
		{
			ret_buf_len += bytes;
			if (ret_buf_len > 1024)
			{
				ret_buf = realloc(ret_buf, ret_buf_len + 1024);
			}
		}
	}
	
	
	void error()
	{
		SSPI->DeleteSecurityContext(&ssl);
		delete sock;
		sock = NULL;
	}
	
	void handshake(bool block)
	{
		if (!in_handshake) return;
		
		if (block) fetch();
		else fetchnb();
		
		SecBuffer       InBuffers[2] = { { recv_buf_len, SECBUFFER_TOKEN, recv_buf}, { 0, SECBUFFER_EMPTY, NULL } };
		SecBuffer       OutBuffer = { 0, SECBUFFER_TOKEN, NULL };
		SecBufferDesc   InBufferDesc = { SECBUFFER_VERSION, 2, InBuffers };
		SecBufferDesc   OutBufferDesc = { SECBUFFER_VERSION, 1, &OutBuffer };
		DWORD           dwSSPIOutFlags;
		SECURITY_STATUS scRet;
		
		scRet = SSPI->InitializeSecurityContextA(&cred, &ssl, NULL, SSPIFlags, 0, SECURITY_NATIVE_DREP,
		                                         &InBufferDesc, 0, NULL, &OutBufferDesc, &dwSSPIOutFlags, NULL);
		
		// If InitializeSecurityContext was successful (or if the error was
		// one of the special extended ones), send the contends of the output
		// buffer to the server.
		if (scRet == SEC_E_OK || scRet == SEC_I_CONTINUE_NEEDED ||
		   (FAILED(scRet) && (dwSSPIOutFlags & ISC_RET_EXTENDED_ERROR)))
		{
			if (OutBuffer.cbBuffer != 0 && OutBuffer.pvBuffer != NULL)
			{
				if (sock->send((BYTE*)OutBuffer.pvBuffer, OutBuffer.cbBuffer) < 0)
				{
					SSPI->FreeContextBuffer(OutBuffer.pvBuffer);
					error();
					return;
				}
				//printf("%d bytes of handshake data sent\n", cbData);
				
				// Free output buffer.
				SSPI->FreeContextBuffer(OutBuffer.pvBuffer);
				OutBuffer.pvBuffer = NULL;
			}
		}
		
		// If InitializeSecurityContext returned SEC_E_INCOMPLETE_MESSAGE,
		// then we need to read more data from the server and try again.
		if (scRet == SEC_E_INCOMPLETE_MESSAGE) return;
		
		// If InitializeSecurityContext returned SEC_E_OK, then the
		// handshake completed successfully.
		if (scRet == SEC_E_OK)
		{
			printf("Handshake was successful\n");
			in_handshake = false;
		}
		
		// Check for fatal error.
		if(FAILED(scRet)) { printf("**** Error 0x%lx returned by InitializeSecurityContext (2)\n", scRet); error(); }
		
		// If InitializeSecurityContext returned SEC_I_INCOMPLETE_CREDENTIALS,
		// then the server just requested client authentication.
		if (scRet == SEC_I_INCOMPLETE_CREDENTIALS)
		{
			return;
		}
		
		// Copy any leftover data from the "extra" buffer, and go around again.
		if (InBuffers[1].BufferType == SECBUFFER_EXTRA)
		{
			memmove(recv_buf, recv_buf + (recv_buf_len - InBuffers[1].cbBuffer), InBuffers[1].cbBuffer);
			recv_buf_len = InBuffers[1].cbBuffer;
		}
		else
			recv_buf_len = 0;
	}
	
	bool handshake_first(const char * domain)
	{
		SecBuffer OutBuffer = { 0, SECBUFFER_TOKEN, NULL };
		SecBufferDesc OutBufferDesc = { SECBUFFER_VERSION, 1, &OutBuffer };
		
		DWORD ignore;
		if (SSPI->InitializeSecurityContextA(&cred, NULL, (char*)domain, SSPIFlags, 0, SECURITY_NATIVE_DREP,
		                                     NULL, 0, &ssl, &OutBufferDesc, &ignore, NULL)
		    != SEC_I_CONTINUE_NEEDED)
		{
			return false;
		}
		
		// Send response to server if there is one.
		if (OutBuffer.cbBuffer != 0 && OutBuffer.pvBuffer != NULL)
		{
			if (sock->send((BYTE*)OutBuffer.pvBuffer, OutBuffer.cbBuffer) < 0)
			{
				SSPI->FreeContextBuffer(OutBuffer.pvBuffer);
				SSPI->DeleteSecurityContext(&ssl);
				return false;
			}
			SSPI->FreeContextBuffer(OutBuffer.pvBuffer); // Free output buffer.
		}
		
		in_handshake = true;
		while (in_handshake) handshake(true);
		return true;
	}
	
	bool init(socket* parent, const char * domain, bool permissive)
	{
		if (!parent) return false;
		
		sock = parent;
		fd = parent->get_fd();
		recv_buf = malloc(2048);
		recv_buf_len = 0;
		ret_buf = malloc(2048);
		ret_buf_len = 0;
		
		if (!handshake_first(domain)) return false;
		SSPI->QueryContextAttributes(&ssl, SECPKG_ATTR_STREAM_SIZES, &bufsizes);
		
		return true;
	}
	
	void process()
	{
		handshake(false);
		
		SECURITY_STATUS scRet = SEC_E_INCOMPLETE_MESSAGE;
		SecBufferDesc   Message;
		SecBuffer       Buffers[4];
		
		DWORD           length;
		int i;
		
		bool again = true;
		
		while (again)
		{
			again = false;
			if (scRet == SEC_E_INCOMPLETE_MESSAGE) // get the data
			{
				fetch();
			}


			// Decrypt the received data.
			Buffers[0].pvBuffer     = recv_buf;
			Buffers[0].cbBuffer     = recv_buf_len;
			Buffers[0].BufferType   = SECBUFFER_DATA;  // Initial Type of the buffer 1
			Buffers[1].BufferType   = SECBUFFER_EMPTY; // Initial Type of the buffer 2
			Buffers[2].BufferType   = SECBUFFER_EMPTY; // Initial Type of the buffer 3
			Buffers[3].BufferType   = SECBUFFER_EMPTY; // Initial Type of the buffer 4

			Message.ulVersion       = SECBUFFER_VERSION;    // Version number
			Message.cBuffers        = 4;                                    // Number of buffers - must contain four SecBuffer structures.
			Message.pBuffers        = Buffers;                        // Pointer to array of buffers
			
			scRet = SSPI->DecryptMessage(&ssl, &Message, 0, NULL);
			if( scRet == SEC_I_CONTEXT_EXPIRED ) { error(); return; }
//      if( scRet == SEC_E_INCOMPLETE_MESSAGE - Input buffer has partial encrypted record, read more
			if( scRet != SEC_E_OK &&
					scRet != SEC_I_RENEGOTIATE &&
					scRet != SEC_I_CONTEXT_EXPIRED )
											{ printf("**** DecryptMessage "); error();
													return; }



			// Locate data and (optional) extra buffers.
			SecBuffer* pDataBuffer  = NULL;
			SecBuffer* pExtraBuffer = NULL;
			for(i = 0; i < 4; i++)
			{
					if( pDataBuffer  == NULL && Buffers[i].BufferType == SECBUFFER_DATA  ) pDataBuffer  = &Buffers[i];
					if( pExtraBuffer == NULL && Buffers[i].BufferType == SECBUFFER_EXTRA ) pExtraBuffer = &Buffers[i];
			}


			// Display the decrypted data.
			if(pDataBuffer && pDataBuffer->cbBuffer)
			{
									length = pDataBuffer->cbBuffer;
									if( length ) // check if last two chars are CR LF
									{
									//TODO: ret_buf
											printf("Decrypted data: %ld bytes %.*s\n", pDataBuffer->cbBuffer, (int)pDataBuffer->cbBuffer, (char*)pDataBuffer->pvBuffer);
											memcpy(ret_buf+ret_buf_len, pDataBuffer->pvBuffer, pDataBuffer->cbBuffer);
											ret_realloc(pDataBuffer->cbBuffer);
											//buff = (BYTE*)pDataBuffer->pvBuffer; // printf( "n-2= %d, n-1= %d \n", buff[length-2], buff[length-1] );
											again = true;
									}
			}

			// Move any "extra" data to the input buffer.
			if (pExtraBuffer)
			{
					memmove(recv_buf, pExtraBuffer->pvBuffer, pExtraBuffer->cbBuffer);
					recv_buf_len = pExtraBuffer->cbBuffer; // printf("cbIoBuffer= %d  \n", cbIoBuffer);
			}
			else
				recv_buf_len = 0;

							// The server wants to perform another handshake sequence.
			if(scRet == SEC_I_RENEGOTIATE)
			{
					in_handshake = true;
					printf("Server requested renegotiate!\n");
			}
    } // Loop till CRLF is found at the end of the data
	}
	
	int recvnb(uint8_t* data, int len)
	{
		if (!sock) return -1;
		
		fetchnb();
		process();
		
		if (!ret_buf_len) return 0;
		
		unsigned ulen = len;
		int ret = (ulen < ret_buf_len ? ulen : ret_buf_len);
		memcpy(data, ret_buf, ret);
		memmove(ret_buf, ret_buf+ret, ret_buf_len-ret);
		ret_buf_len -= ret;
		return ret;
	}
	
	int recv(uint8_t* data, int len)
	{
		if (!sock) return -1;
		
		fetch();
		process();
		
		if (!ret_buf_len) return 0;
		
		unsigned ulen = len;
		int ret = (ulen < ret_buf_len ? ulen : ret_buf_len);
		memcpy(data, ret_buf, ret);
		memmove(ret_buf, ret_buf+ret, ret_buf_len-ret);
		ret_buf_len -= ret;
		return ret;
	}
	
	int send0(const uint8_t* data, int len)
	{
		if (!sock) return -1;
		
		handshake(false);
		return send1(data, len);
	}
	
	int send1(const uint8_t* data, int len)
	{
		if (!sock) return -1;
		
		handshake(false);
		//static DWORD EncryptSend( SOCKET Socket, CtxtHandle * ssl, PBYTE pbIoBuffer, SecPkgContext_StreamSizes Sizes )
// http://msdn.microsoft.com/en-us/library/aa375378(VS.85).aspx
// The encrypted message is encrypted in place, overwriting the original contents of its buffer.
    SECURITY_STATUS    scRet;            // unsigned long cbBuffer;    // Size of the buffer, in bytes
    SecBufferDesc        Message;        // unsigned long BufferType;  // Type of the buffer (below)
    SecBuffer                Buffers[4];    // void    SEC_FAR * pvBuffer;   // Pointer to the buffer
    DWORD                        cbMessage, cbData;
    PBYTE                        pbMessage;
    BYTE pbIoBuffer[0x1000]; // not static, for threading purposes


    pbMessage = pbIoBuffer + bufsizes.cbHeader; // Offset by "header size"
    cbMessage = len;
    printf("Sending %lu bytes of plaintext: %.*s", cbMessage, (int)cbMessage, data);


    // Encrypt the HTTP request.
    Buffers[0].pvBuffer     = pbIoBuffer;                                // Pointer to buffer 1
    Buffers[0].cbBuffer     = bufsizes.cbHeader;                        // length of header
    Buffers[0].BufferType   = SECBUFFER_STREAM_HEADER;    // Type of the buffer

    Buffers[1].pvBuffer     = pbMessage;                                // Pointer to buffer 2
    memcpy(pbMessage, data, cbMessage);
    Buffers[1].cbBuffer     = cbMessage;                                // length of the message
    Buffers[1].BufferType   = SECBUFFER_DATA;                        // Type of the buffer
                                                                                            
    Buffers[2].pvBuffer     = pbMessage + cbMessage;        // Pointer to buffer 3
    Buffers[2].cbBuffer     = bufsizes.cbTrailer;                    // length of the trailor
    Buffers[2].BufferType   = SECBUFFER_STREAM_TRAILER;    // Type of the buffer

    Buffers[3].pvBuffer     = SECBUFFER_EMPTY;                    // Pointer to buffer 4
    Buffers[3].cbBuffer     = SECBUFFER_EMPTY;                    // length of buffer 4
    Buffers[3].BufferType   = SECBUFFER_EMPTY;                    // Type of the buffer 4

    Message.ulVersion       = SECBUFFER_VERSION;    // Version number
    Message.cBuffers        = 4;                                    // Number of buffers - must contain four SecBuffer structures.
    Message.pBuffers        = Buffers;                        // Pointer to array of buffers
    scRet = SSPI->EncryptMessage(&ssl, 0, &Message, 0); // must contain four SecBuffer structures.
    if(FAILED(scRet)) { printf("**** Error 0x%lx returned by EncryptMessage\n", scRet); return scRet; }

    // Send the encrypted data to the server.                                            len                                                                         flags
    cbData = sock->send(pbIoBuffer,    Buffers[0].cbBuffer + Buffers[1].cbBuffer + Buffers[2].cbBuffer);

        printf("%ld bytes of encrypted data sent\n", cbData);

    return cbData; // send( Socket, pbIoBuffer,    Sizes.cbHeader + strlen(pbMessage) + Sizes.cbTrailer,  0 );
	}
	
	~socketssl_impl()
	{
		error();
		free(recv_buf);
		free(ret_buf);
	}
};

socketssl* socketssl::create(socket* parent, const char * domain, bool permissive)
{
	initialize();
	socketssl_impl* ret = new socketssl_impl();
	if (!ret->init(parent, domain, permissive)) { delete ret; return NULL; }
	else return ret;
}
#endif
