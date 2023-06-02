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
	socketbuf sock;
	
	crypto_secretstream_xchacha20poly1305_state crypt_recv;
	crypto_secretstream_xchacha20poly1305_state crypt_send;
	
	bool error_init() { terminate(); return false; }
	bytesr error_recv() { terminate(); return nullptr; }
	
public:
	receiver() {}
	
	async<bool> init(cstring host, uint16_t port)
	{
		co_return co_await this->init(co_await socket2::create(host, port));
	}
	async<bool> init(autoptr<socket2> sock_)
	{
		if (!sock_)
			co_return error_init();
		this->sock = std::move(sock_);
		uint8_t header[sizeof(uint32_t) + crypto_secretstream_xchacha20poly1305_HEADERBYTES];
		crypto_secretstream_xchacha20poly1305_init_push(&crypt_send, header+sizeof(uint32_t), the_key);
		writeu_le32(header, crypto_secretstream_xchacha20poly1305_HEADERBYTES);
		sock.send(bytesr(header));
		
		uint32_t len = co_await sock.u32l();
		if (len != crypto_secretstream_xchacha20poly1305_HEADERBYTES)
			co_return error_init();
		bytesr by = co_await sock.bytes(len);
		crypto_secretstream_xchacha20poly1305_init_pull(&crypt_recv, by.ptr(), the_key); // oddly enough, this can't fail
		co_return true;
	}
	void consume(receiver& prev)
	{
		sock = std::move(prev.sock);
		crypt_recv = prev.crypt_recv;
		crypt_send = prev.crypt_send;
	}
	
	async<bytearray> recv()
	{
		uint32_t len = co_await sock.u32l();
		if (len > 16*1024*1024) co_return error_recv();
		if (len < crypto_secretstream_xchacha20poly1305_ABYTES) co_return error_recv();
		
		bytesr by = co_await sock.bytes(len);
		bytearray plain;
		plain.resize(by.size() - crypto_secretstream_xchacha20poly1305_ABYTES);
		if (crypto_secretstream_xchacha20poly1305_pull(
				&crypt_recv, plain.ptr(), NULL, NULL, by.ptr(), by.size(), NULL, 0) != 0)
			co_return error_recv();
		
		co_return plain;
	}
	
	async<bytearray> request(bytesr by)
	{
		send(by);
		return recv();
	}
	
	void terminate() { sock = nullptr; }
	bool alive() { return sock; }
	
	void send(bytesr by)
	{
		bytearray crypt;
		crypt.resize(sizeof(uint32_t) + crypto_secretstream_xchacha20poly1305_ABYTES + by.size());
		writeu_le32(crypt.ptr(), crypto_secretstream_xchacha20poly1305_ABYTES + by.size());
		
		// always successful
		crypto_secretstream_xchacha20poly1305_push(&crypt_send, crypt.ptr()+sizeof(uint32_t), NULL, by.ptr(), by.size(), NULL, 0, 0);
		sock.send(crypt);
	}
};

// wire format: everything is in chunks
// chunk format: u32 ciphertext length, then libsodium crypto_secretstream_xchacha20poly1305 data
// first chunk must be the header (24 bytes), anything subsequent is body data (min 17 bytes)
// applies in both directions
// key agreement is not covered by this protocol
// all pathnames must start with a /; readdir return value is filename only, no path
// all pathnames and filenames are utf8
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

#define REQ_STATFS 11
// request:
// - (empty)
// response:
// - u64 filesystem size (bytes)
// - u64 filesystem freespace


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
