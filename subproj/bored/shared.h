// BoredomFS - because I didn't have anything better to do than learn the FUSE API.
#pragma once
#include "arlib.h"
#include "libsodium-1.0.18-stable/src/libsodium/include/sodium.h"

static uint8_t the_key[] = {
#include "the_key.h"
};
static_assert(sizeof(the_key) == crypto_secretstream_xchacha20poly1305_KEYBYTES);

static bool init_key(string key_text)
{
	if (!key_text)
	{
		return true;
	}
	else if (key_text.length() == sizeof(the_key)*2 && fromstringhex(key_text, the_key))
	{
		return true;
	}
	else if (key_text.length() <= sizeof(the_key))
	{
		memset(the_key, 0, sizeof(the_key));
		memcpy(the_key, key_text.bytes().ptr(), key_text.length());
		return true;
	}
	else
	{
		return false;
	}
}

class receiver {
	size_t pos = 0;
	bytearray buf;
	
	bool cryptrecv_inited = false;
	crypto_secretstream_xchacha20poly1305_state crypt_recv;
	crypto_secretstream_xchacha20poly1305_state crypt_send;
	
	autoptr<socket> sock;
	function<void(bytearray)> cb;
	function<void()> error;
	
	void do_error()
	{
		stop();
		error();
	}
	
	void handle()
	{
		if (!buf) buf.resize(4);
		
		// libsodium doesn't support splitting concatenated variable-width input, have to prefix unencrypted length
		if (pos < 4)
		{
			int n = sock->recv(buf.skip(pos));
			if (n < 0) return do_error();
			
			pos += n;
			if (pos == 4)
			{
				uint32_t len = readu_le32(buf.ptr());
				if (len > 16*1024*1024) return do_error();
				if (len < crypto_secretstream_xchacha20poly1305_ABYTES) return do_error();
				buf.resize(len);
			}
		}
		else
		{
			int n = sock->recv(buf.skip(pos-4));
			if (n < 0) return do_error();
			pos += n;
			
			if (pos-4 == buf.size())
			{
				pos = 0;
				
				if (!cryptrecv_inited)
				{
					if (buf.size() != crypto_secretstream_xchacha20poly1305_HEADERBYTES) return do_error();
					crypto_secretstream_xchacha20poly1305_init_pull(&crypt_recv, buf.ptr(), the_key); // oddly enough, this can't fail
					
					cryptrecv_inited = true;
					cb(bytearray());
				}
				else
				{
					// weird how this one doesn't let me decrypt into the input buffer, forces me to make extra pointless allocations
					// but BoredomFS isn't exactly well designed anyways, it's good enough
					bytearray plain;
					plain.resize(buf.size() - crypto_secretstream_xchacha20poly1305_ABYTES);
					if (crypto_secretstream_xchacha20poly1305_pull(
							&crypt_recv, plain.ptr(), NULL, NULL, buf.ptr(), buf.size(), NULL, 0) != 0)
						return do_error();
					cb(std::move(plain));
				}
				buf.reset();
			}
		}
	}
	
public:
	receiver() {}
	
	void init(autoptr<socket> sock, function<void(bytearray)> cb, function<void()> error)
	{
		this->sock = std::move(sock);
		this->cb = cb;
		this->error = error;
		
		this->sock->callback(bind_this(&receiver::handle));
		
		uint8_t header[sizeof(uint32_t) + crypto_secretstream_xchacha20poly1305_HEADERBYTES];
		crypto_secretstream_xchacha20poly1305_init_push(&crypt_send, header+sizeof(uint32_t), the_key);
		writeu_le32(header, crypto_secretstream_xchacha20poly1305_HEADERBYTES);
		this->sock->send(header);
	}
	void consume(receiver& prev)
	{
		cryptrecv_inited = prev.cryptrecv_inited;
		crypt_recv = prev.crypt_recv;
		crypt_send = prev.crypt_send;
		
		pos = prev.pos;
		buf = std::move(prev.buf);
		sock = prev.sock.release();
		sock->callback(bind_this(&receiver::handle));
	}
	void callback(function<void(bytearray)> cb, function<void()> error) { this->cb = cb; this->error = error; }
	
	void stop() { sock = NULL; }
	bool alive() { return sock; }
	
	void send(bytesr by)
	{
		bytearray crypt;
		crypt.resize(sizeof(uint32_t) + crypto_secretstream_xchacha20poly1305_ABYTES + by.size());
		writeu_le32(crypt.ptr(), crypto_secretstream_xchacha20poly1305_ABYTES + by.size());
		
		// always successful
		crypto_secretstream_xchacha20poly1305_push(&crypt_send, crypt.ptr()+sizeof(uint32_t), NULL, by.ptr(), by.size(), NULL, 0, 0);
		sock->send(crypt);
	}
};

// wire format: everything is in chunks
// chunk format: u32 ciphertext length, then libsodium crypto_secretstream_xchacha20poly1305 data; key agreement is a separate mechanism
// first chunk must be the header (24 bytes), anything subsequent is body data (min 17 bytes)
// applies in both directions
// all pathnames must start with a /; readdir return value is filename only, no path
// the tag byte is not used (other than libsodium detecting rekey requests)

// chunk contents:
// - u32 type (one of the below)
// - u8[] body (size and interpretation depends on what the request is)
// response:
// - u8[] body
// all integers are little endian
// all strings are utf8
// bad type or otherwise invalid request yields undefined output

#define REQ_STAT 1
// request:
// - strnul pathname
// response:
// - u8 type (0 - not found or inaccessible, 1 - dir, 2 - file, 3 - executable file)
// - u64 size
// - u32+u32 access (sec+nsec)
// - u32+u32 modify (sec+nsec)

#define REQ_READDIR 2
// request:
// - strnul pathname
// response:
// - u8 zero (if success; on failure, response is empty)
// - array (can be empty, does not contain . or .. components):
//   - same as REQ_STAT
//   - strnul filename

#define REQ_OPEN 3
// request:
// - strnul pathname
// - u32 flags
//   0x00000003 - mode (0 - read only; 1 - rw, existing only; 2 - rw, create only; 3 - rw always)
//   0x00000004 - truncate
//   0x00000008 - append
//   0x00000010 - don't set access time
// response:
// - u32 file identifier (blank on failure)

#define REQ_CLOSE 4
// request:
// - u32 file identifier
// response:
// - empty

#define REQ_READ 5
// request:
// - u32 file identifier
// - u64 offset
// - u32 size
// response:
// - u8[] data (blank on error)

#define REQ_WRITE 6
// request:
// - u32 file identifier
// - u64 offset (ignored if append mode set in REQ_OPEN)
// - u8[] data
// response:
// - u32 bytes written (blank on error)

#define REQ_RENAME 7
// request:
// - strnul source
// - strnul target
// response:
// - u8 success (always 1, or blank on failure)

#define REQ_DELETE 8
// request:
// - strnul path
// response:
// - u8 success (always 1, or blank on failure)

#define REQ_MKDIR 9
// request:
// - strnul path
// response:
// - u8 success (always 1, or blank on failure)

#define REQ_RMDIR 10
// request:
// - strnul path
// response:
// - u8 success (always 1, or blank on failure)


#define REQ_PING 100
// request:
// - u32 response amount, can be zero
// - u8[] data (ignored by recipient)
// response:
// - u8[] data (size equal to the u32, contents are arbitrary bytes)
// can be used to test transfer speed, or ask if the nodes are correctly connected


#define REQ_EXEC 101
// request:
// - strnul path
// - strnul[] argv (excluding argv[0] path)
// response:
// - u8 success (always 1, or blank on failure)
// on success, the protocol is switched
// the framing scheme remains the same, but the new protocol is asynchronous
// both sides will send message types, and no message gets a response

// the set of commands available to the client is
#define REQ_EXEC_STDIN 110
// - u8[] contents
#define REQ_EXEC_STDIN_CLOSE 111
// - blank
#define REQ_EXEC_RELEASE 112
// - blank
// (socket terminates after this)

// and the server may send
#define REQ_EXEC_STDOUT 120
// - u8[] contents
#define REQ_EXEC_EXIT 121
// - u32 exit status
// (socket terminates after this)

// if the socket terminates, the child process is terminated as well, if not terminated with REQ_EXEC_RELEASE
