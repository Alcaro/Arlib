#pragma once
#ifdef ARLIB_SOCKET
#include "global.h"
#include "containers.h"
#include "socket.h"
#include "bytepipe.h"

class HTTP {
public:
	struct req {
		//Every field except 'url' is safe to leave as default.
		string url;
		
		string method;
		//These headers are implied by other members and added automatically, if not already present:
		//Connection: keep-alive
		//Host: <from url>
		//Content-Length: <from postdata> (if not GET)
		//Content-Type: application/x-www-form-urlencoded
		//           or application/json if postdata starts with [ or {
		array<string> headers; // TODO: multimap
		array<byte> postdata;
		
		//Applies to the entire HTTP object, not just this request. Must be same for all.
		size_t maxsize = 1000000; // Limits number of bytes sent by server. Actual data received will be less.
		uint32_t timeout = 5; // In seconds.
		
		req() {}
		req(string url) : url(url) {}
	};
	
	struct rsp {
		bool success;
		
		enum {
			e_not_sent      = -1, // too few send() calls
			e_bad_url       = -2, // couldn't parse URL
			e_different_url = -2, // can't use Keep-Alive between these, create a new http object
			e_connect       = -3, // couldn't open TCP/SSL stream
			e_broken        = -4, // server unexpectedly closed connection, or timeout
			                      // if partial=true in recv(), this may be a real status code instead; to detect this, look for success=false
			e_not_http      = -5, // the server isn't speaking HTTP
			e_only_one      = -6, // server used Connection: close or similar on a previous request
			//may also be a normal http status code (200, 302, 404, etc)
		};
		int status;
		//string status_str; // useless
		
		array<string> headers; // TODO: multimap
		array<byte> body;
		
		operator arrayvieww<byte>()
		{
			if (status >= 200 && status <= 299) return body;
			else return NULL;
		}
		//operator string() { return body; } // throws ambiguity errors if enabled
		
		cstring header(cstring name) const
		{
			for (cstring head : headers)
			{
				if (head.istartswith(name) && head[name.length()]==':' && head[name.length()+1]==' ')
				{
					return head.csubstr(name.length()+2, ~0);
				}
			}
			return "";
		}
		
		cstring text() const { return body; }
	};
	
	static string request(string url) { return std::move(request((req)url).body); }
	static rsp request(const req& r) { HTTP http; http.send(r); return std::move(http.recv()); }
	
	//Multiple requests may be sent to the same object. This will make them use HTTP Keep-Alive.
	//The requests must be to the same protocol-domain-port tuple.
	//They will be returned in the same order as they're sent.
	//Returns false if the request is invalid or the host can't be reached. This does not affect the object.
	//If the server doesn't support Keep-Alive, remaining requests are cancelled.
	bool send(const req& r);
	bool ready() { activity(false); return i_ready(); }
	void progress(size_t& done, size_t& total); // Both are 0 if unknown, or if empty. Do not use for checking if it's done.
	//Returns immediately if there's anything unfinished going on, or if nothing more is expected to be returned (including error in send()).
	void await();
	rsp recv(bool partial = false); // Calls await() if needed. Only works once per send().
	
	//Discards any unfinished requests, errors, and similar.
	void reset()
	{
		done.reset();
		n_unfinished = 0;
		error = 0;
		host = location();
		sock = NULL;
		tosend.reset();
	}
	
	//If this key is returned, call .ready(), then .monitor() again.
	void monitor(socket::monitor& mon, void* key) { mon.add(sock, key, true, tosend.remaining() ? true : false); }
	
	
	struct location {
		string proto;
		string domain;
		int port;
		string path;
	};
	static bool parseUrl(cstring url_, bool relative, location& out);
	
	
private:
	array<rsp> done;
	int n_unfinished = 0; // includes the one currently being processed
	
	bool i_ready() const
	{
		return (error || !n_unfinished || done.size());
	}
	
	int error = 0;
	void error_v(int err)
	{
		sock = NULL;
		error = err;
	}
	bool error_f(int err) { error_v(err); return false; }
	
	size_t sizelimit;
	time_t timelimit;
	location host; // used to verify the same socket can be reused
	
	autoptr<socket> sock;
	bytepipe tosend;
	bool close_ok = false;
	
	rsp next;
	string fragment;
	size_t bytesleft;
	enum {
		st_status, // waiting for HTTP/1.1 200 OK
		st_header, // waiting for header, or \r\n\r\n
		st_body, // waiting for additional bytes, non-chunked
		st_body_chunk_len, // waiting for chunk length
		st_body_chunk, // waiting for chunk
	} state = st_status;
	
	void activity(bool block);
};
#endif
