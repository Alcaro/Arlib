#ifdef ARLIB_SOCKET
#include "http.h"
#include "test.h"
#include "stringconv.h"

bool http::parseUrl(cstring url_, bool relative, location& out)
{
	string url = url_; // TODO: clean up once cstring acts sanely
	if (url.startswith("http:")) { out.https = false; url = url.substr(5, ~0); }
	else if (url.startswith("https:")) { out.https = true; url = url.substr(6, ~0); }
	else if (!relative) return false;
	
	if (url.startswith("//"))
	{
		url = url.substr(2, ~0);
		
		array<string> host_loc = url.split<1>("/");
		out.loc = "/"+host_loc[1];
		if (!out.loc) out.loc = "/";
		array<string> domain_port = host_loc[0].split<1>(":");
		out.domain = domain_port[0];
		if (domain_port[1])
		{
			if (!fromstring(domain_port[1], out.port)) return false;
			if (out.port <= 0 || out.port > 65535) return false;
		}
		else
		{
			if (!out.https) out.port = 80;
			else out.port = 443;
		}
	}
	else if (!relative) return false;
	else if (url[0]=='/') out.loc = url;
	//TODO: ? #
	else return false;
	
	return true;
}

bool http::send(const req& r)
{
	if (!sock)
	{
		if (!parseUrl(r.url, false, this->host)) return error_f(rsp::e_bad_url);
	}
	else
	{
		location loc;
		if (!parseUrl(r.url, false, loc)) return error_f(rsp::e_bad_url);
		if (loc.https != host.https || loc.domain != host.domain || loc.port != host.port)
		{
			return error_f(rsp::e_different_url);
		}
		host.loc = loc.loc;
	}
	
	
	cstring method = r.method;
	if (!method) method = (r.postdata ? "POST" : "GET");
	tosend.push(method, " ", host.loc, " HTTP/1.1\r\n");
	
	bool httpHost = false;
	bool httpContentLength = false;
	bool httpContentType = false;
	bool httpConnection = false;
	for (cstring head : r.headers)
	{
		if (head.startswith("Host:")) httpHost = true;
		if (head.startswith("Content-Length:")) httpContentLength = true;
		if (head.startswith("Content-Type:")) httpContentType = true;
		if (head.startswith("Connection:")) httpConnection = true;
		tosend.push(head, "\r\n");
	}
	
	if (!httpHost) tosend.push("Host: ", host.domain, "\r\n");
	if (method=="POST" && !httpContentLength) tosend.push("Content-Length: ", tostring(r.postdata.size()), "\r\n");
	if (method=="POST" && !httpContentType) tosend.push("Content-Type: application/x-www-form-urlencoded;charset=UTF-8\r\n");
	if (!httpConnection) tosend.push("Connection: keep-alive\r\n");
	
	tosend.push("\r\n");
	
	tosend.push(r.postdata);
	
	bool freshsock = false;
	if (!sock)
	{
	newsock:
		if (host.https) sock = socketssl::create(host.domain, host.port);
		else sock = socket::create(host.domain, host.port);
		if (!sock) return error_f(rsp::e_connect);
		freshsock = true;
	}
	
	arrayview<byte> sendbuf = tosend.pull_buf();
	int bytes = sock->sendp(sendbuf, false);
	if (bytes < 0)
	{
		if (!freshsock && !n_unfinished) goto newsock;
		else return error_f(rsp::e_broken);
	}
	
	n_unfinished++;
	return true;
}

void http::activity(bool block)
{
	if (!sock) return;
	
	array<byte> newrecv;
	
	if (tosend.remaining())
	{
		if (sock->recv(newrecv, false) < 0) return error_v(close_ok ? rsp::e_only_one : rsp::e_broken);
		
		if (!newrecv)
		{
			arrayview<byte> sendbuf = tosend.pull_buf();
			//this can give deadlocks if
			//- a request was made for something big
			//- a big request (big POST body) was sent on the same object
			//- the server does not infinitely buffer written data, but insists I read stuff on my end
			//- the network is jittery, so sock->recv() above returns empty
			//- and this is called from await(), not ready()
			//which is so unlikely I ignore it.
			//and it'll eventually exit, anyways; server will close the connection, breaking the send().
			int bytes = sock->sendp(sendbuf, block);
			if (bytes < 0) return error_v(close_ok ? rsp::e_only_one : rsp::e_broken);
			if (bytes > 0)
			{
				block = false;
				tosend.pull_done(bytes);
			}
		}
	}
	
	if (sock->recv(newrecv, block) < 0) return error_v(close_ok ? rsp::e_only_one : rsp::e_broken);
	
again:
	if (!newrecv) return;
	switch (state)
	{
	case st_status:
	case st_header:
	case st_body_chunk_len:
		if (newrecv.contains('\n'))
		{
			size_t n = newrecv.find('\n');
			fragment += newrecv.slice(0, n);
			if (fragment.endswith("\r")) fragment = fragment.substr(0, ~1);
			newrecv = newrecv.skip(n+1);
			
			if (state == st_status)
			{
				if (!fragment) {} // chunked transfer ends with 0\r\n\r\n; this could be the second \r\n
				else if (fragment.startswith("HTTP/"))
				{
					close_ok = false;
					string status_i = fragment.split<2>(" ")[1];
					fromstring(status_i, next.status);
					state = st_header;
				}
				else return error_v(rsp::e_not_http);
			}
			else if (state == st_header)
			{
				if (fragment != "")
				{
					next.headers.append(fragment);
				}
				else
				{
					string transferEncoding = next.header("Transfer-Encoding");
					if (transferEncoding)
					{
						if (transferEncoding == "chunked")
						{
							state = st_body_chunk_len;
						}
						else
						{
							//valid: chunked, (compress, deflate, gzip), identity
							//ones in parens only with Accept-Encoding
							abort();
						}
					}
					else
					{
						if (!fromstring(next.header("Content-Length"), bytesleft))
						{
							bytesleft = -1;
						}
						state = st_body;
					}
				}
			}
			else // st_body_chunk_len
			{
				if (fragment)
				{
					fromstring(fragment, bytesleft);
					if (bytesleft) state = st_body_chunk;
					else goto req_finish;
				}
				//chunks are terminated by a \r\n, which looks like blank line to us; discard it
			}
			fragment = "";
		}
		else fragment += (string)newrecv;
		break;
		
	case st_body:
	case st_body_chunk:
		size_t bytes = min(newrecv.size(), bytesleft);
		next.body += newrecv.slice(0, bytes);
		if (bytesleft != (size_t)-1) bytesleft -= bytes;
		
		if (!bytesleft)
		{
			newrecv = newrecv.skip(bytes);
			if (state == st_body)
			{
				goto req_finish;
			}
			else
			{
				state = st_body_chunk_len;
			}
		}
		break;
	}
	goto again;
	
req_finish:
	state = st_status;
	if (error && error != rsp::e_only_one) next.status = error;
	done.append(std::move(next));
	next = rsp();
	n_unfinished--;
	close_ok = true;
	fragment = "";
	goto again;
}

void http::await()
{
	while (!i_ready()) activity(true);
}

http::rsp http::recv(bool partial)
{
	await();
	if (done)
	{
		rsp r = std::move(done[0]);
		done.remove(0);
		return r;
	}
	else
	{
		rsp r;
		r.success = false;
		r.status = (error ? error : rsp::e_not_sent);
		return r;
	}
}

//https://httpbin.org/stream-bytes/1024
test()
{
	test_skip("too slow");
#define URL "http://httpbin.org/user-agent"
#define CONTENTS "{\n  \"user-agent\": null\n}\n"
	{
		string ret = http::request(URL);
		assert_eq(ret, CONTENTS);
	}
	
	{
		http h;
		h.send(http::req(URL));
		assert_eq(h.ready(), false);
		assert_eq((string)h.recv(), CONTENTS);
		h.send((string)URL);
		assert_eq((string)h.recv(), CONTENTS);
		assert_eq(h.recv().status, http::rsp::e_not_sent);
	}
	
	{
		http::req r;
		r.url = "http://httpbin.org/post";
		r.headers.append("Host: httpbin.org");
		r.postdata.append('x');
		
		http h;
		h.send(r);
		h.send(r);
		string data1 = (string)h.recv();
		assert(data1.startswith("{\n"));
		string data2 = (string)h.recv();
		assert_eq(data2, data1);
		assert_eq(h.recv().status, http::rsp::e_not_sent);
	}
	
	{
		http::req r;
		r.url = URL;
		r.headers.append("Connection: close");
		
		http h;
		h.send(r);
		h.send(r);
		assert_eq(h.ready(), false);
		assert_eq((string)h.recv(), CONTENTS);
		assert_eq(h.ready(), true);
		assert_eq(h.recv().status, http::rsp::e_only_one);
	}
	
	{
		http::req r("https://httpbin.org/stream-bytes/32?chunk_size=3&seed=1");
		http h;
		h.send(r);
		h.send(r);
		
		http::rsp r1 = h.recv();
		assert_eq(r1.status, 200);
		assert_eq(r1.body.size(), 32);
		
		http::rsp r2 = h.recv();
		assert_eq(r2.status, 200);
		assert_eq(r2.body.size(), 32);
		
		assert_eq(tostringhex(r1.body), tostringhex(r2.body));
	}
	
	//could mess with .ready, but it keeps giving race conditions
}
#endif
