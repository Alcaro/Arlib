#ifdef ARLIB_SOCKET
#include "http.h"
#include "test.h"
#include "stringconv.h"

bool HTTP::parseUrl(cstring url_, bool relative, location& out)
{
	string url = url_; // TODO: clean up once cstring acts sanely
	
	int pos = 0;
	while (islower(url[pos])) pos++;
	if (url[pos]==':')
	{
		out.proto = url.substr(0, pos);
		url = url.substr(pos+1, ~0);
	}
	else if (!relative) return false;
	
	if (url.startswith("//"))
	{
		url = url.substr(2, ~0);
		
		array<string> host_loc = url.split<1>("/");
		out.path = "/"+host_loc[1];
		if (!host_loc[1])
		{
			host_loc = url.split<1>("?");
			if (host_loc[1])
			{
				out.path = "/?"+host_loc[1];
			}
		}
		array<string> domain_port = host_loc[0].split<1>(":");
		out.domain = domain_port[0];
		if (domain_port[1])
		{
			if (!fromstring(domain_port[1], out.port)) return false;
			if (out.port <= 0 || out.port > 65535) return false;
		}
		else
		{
			out.port = 0;
		}
	}
	else if (!relative) return false;
	else if (url[0]=='/') out.path = url;
	else if (url[0]=='?') out.path = out.path.split<1>("?")[0] + url;
	else out.path = out.path.rsplit<1>("/")[0] + "/" + url;
	
	return true;
}

bool HTTP::send(const req& r)
{
	//this allows changing hostname. probably shouldn't, but nobody's gonna rely on that
	if (error == rsp::e_only_one && !n_unfinished) error = 0;
	
	if (!sock)
	{
		if (!parseUrl(r.url, false, this->host)) return error_f(rsp::e_bad_url);
	}
	else
	{
		activity(false); // check if socket's closed
		
		location loc;
		if (!parseUrl(r.url, false, loc)) return error_f(rsp::e_bad_url);
		if (loc.proto != host.proto || loc.domain != host.domain || loc.port != host.port)
		{
			return error_f(rsp::e_different_url);
		}
		host.path = loc.path;
	}
	if (error == rsp::e_only_one && !n_unfinished) error = 0;
	
	cstring method = r.method;
	if (!method) method = (r.postdata ? "POST" : "GET");
	tosend.push(method, " ", host.path, " HTTP/1.1\r\n");
	
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
	if (method!="GET" && !httpContentLength) tosend.push("Content-Length: ", tostring(r.postdata.size()), "\r\n");
	if (method!="GET" && !httpContentType)
	{
		if (r.postdata && (r.postdata[0] == '[' || r.postdata[0] == '{'))
		{
			tosend.push("Content-Type: application/json\r\n");
		}
		else
		{
			tosend.push("Content-Type: application/x-www-form-urlencoded\r\n");
		}
	}
	if (!httpConnection) tosend.push("Connection: keep-alive\r\n");
	
	tosend.push("\r\n");
	
	tosend.push(r.postdata);
	
	bool freshsock = false;
	if (!sock)
	{
	newsock:
		if (host.proto == "https")  sock = socketssl::create(host.domain, host.port ? host.port : 443);
		else if (host.proto == "http") sock = socket::create(host.domain, host.port ? host.port : 80);
		else return error_f(rsp::e_bad_url);
		if (!sock) return error_f(rsp::e_connect);
		
		freshsock = true;
		close_ok = false;
		fragment = "";
	}
	
	arrayview<byte> sendbuf = tosend.pull_buf();
	int bytes = sock->sendp(sendbuf, false);
	if (bytes < 0)
	{
		if (!freshsock && !n_unfinished) goto newsock;
		else return error_f(rsp::e_broken);
	}
	tosend.pull_done(bytes);
	
	n_unfinished++;
	return true;
}

void HTTP::activity(bool block)
{
netagain:
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
	block = false;
	if (!newrecv) return;
	
again:
	if (!newrecv) goto netagain;
	switch (state)
	{
	case st_status:
	case st_header:
	case st_body_chunk_len:
	case st_body_chunk_term:
	case st_body_chunk_term_final:
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
						cstring lengthstr = next.header("Content-Length");
						if (!lengthstr && next.status == 204) bytesleft = 0; // 204 No Content
						else if (!fromstring(next.header("Content-Length"), bytesleft))
						{
							bytesleft = -1;
						}
						state = st_body;
						if (bytesleft == 0) goto req_finish;
					}
				}
			}
			else if (state == st_body_chunk_len)
			{
				fromstringhex(fragment, bytesleft);
				if (bytesleft) state = st_body_chunk;
				else goto req_finish;
			}
			else if (state == st_body_chunk_term)
			{
				state = st_body_chunk_len;
			}
			else // st_body_chunk_term_final
			{
				goto req_finish;
			}
			fragment = "";
			goto again;
		}
		else fragment += (string)newrecv;
		goto netagain;
		
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
				state = st_body_chunk_term;
				goto again;
			}
		}
		goto netagain;
	}
	abort(); // not allowed
	
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

void HTTP::await()
{
	while (!i_ready() && n_unfinished && sock) activity(true);
}

HTTP::rsp HTTP::recv(bool partial)
{
	await();
	if (done)
	{
		rsp r = std::move(done[0]);
		r.success = true;
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

static void test_url(cstring url, cstring url2, cstring proto, cstring domain, int port, cstring path)
{
	HTTP::location loc;
	assert(HTTP::parseUrl(url, false, loc));
	if (url2) assert(HTTP::parseUrl(url2, true, loc));
	assert_eq(loc.proto, proto);
	assert_eq(loc.domain, domain);
	assert_eq(loc.port, port);
	assert_eq(loc.path, path);
}
static void test_url(cstring url, cstring proto, cstring domain, int port, cstring path)
{
	test_url(url, "", proto, domain, port, path);
}
test("URL parser")
{
	test_url("wss://gateway.discord.gg?v=5&encoding=json", "wss", "gateway.discord.gg", 0, "/?v=5&encoding=json");
	test_url("wss://gateway.discord.gg", "?v=5&encoding=json", "wss", "gateway.discord.gg", 0, "/?v=5&encoding=json");
	test_url("http://example.com/foo/bar.html?baz", "/bar/foo.html", "http", "example.com", 0, "/bar/foo.html");
	test_url("http://example.com/foo/bar.html?baz", "foo.html", "http", "example.com", 0, "/foo/foo.html");
	test_url("http://example.com/foo/bar.html?baz", "?quux", "http", "example.com", 0, "/foo/bar.html?quux");
}

test("HTTP")
{
	test_skip("too slow");
#define URL "http://httpbin.org/user-agent"
#define CONTENTS "{\n  \"user-agent\": null\n}\n"
	{
		string ret = HTTP::request(URL);
		assert_eq(ret, CONTENTS);
	}
	
	{
		HTTP h;
		h.send(HTTP::req(URL));
		assert_eq(h.ready(), false);
		assert_eq((string)h.recv(), CONTENTS);
		h.send((string)URL);
		assert_eq((string)h.recv(), CONTENTS);
		assert_eq(h.recv().status, HTTP::rsp::e_not_sent);
	}
	
	{
		HTTP::req r;
		r.url = "http://httpbin.org/post";
		r.headers.append("Host: httpbin.org");
		r.postdata.append('x');
		
		HTTP h;
		h.send(r);
		h.send(r);
		string data1 = (string)h.recv();
		assert(data1.startswith("{\n"));
		string data2 = (string)h.recv();
		assert_eq(data2, data1);
		assert_eq(h.recv().status, HTTP::rsp::e_not_sent);
	}
	
	{
		HTTP::req r;
		r.url = URL;
		r.headers.append("Connection: close");
		
		HTTP h;
		h.send(r);
		h.send(r);
		assert_eq(h.ready(), false);
		assert_eq((string)h.recv(), CONTENTS);
		assert_eq(h.ready(), true);
		assert_eq(h.recv().status, HTTP::rsp::e_only_one);
	}
	
	{
		HTTP::req r;
		r.url = URL;
		r.headers.append("Connection: close");
		
		HTTP h;
		h.send(r);
		assert_eq((string)h.recv(), CONTENTS);
		h.send(r); // ensure it opens a new socket
		assert_eq((string)h.recv(), CONTENTS);
	}
	
	{
		HTTP::req r("https://httpbin.org/stream-bytes/128?chunk_size=30&seed=1"); // throw in a https test too for no reason
		HTTP h;
		h.send(r);
		h.send(r);
		
		HTTP::rsp r1 = h.recv();
		assert_eq(r1.status, 200);
		assert_eq(r1.body.size(), 128);
		
		HTTP::rsp r2 = h.recv();
		assert_eq(r2.status, 200);
		assert_eq(r2.body.size(), 128);
		
		assert_eq(tostringhex(r1.body), tostringhex(r2.body));
	}
	
	{
		HTTP::rsp r = HTTP::request(HTTP::req("https://www.smwcentral.net/ajax.php?a=getdiscordusers"));
		assert(r.success);
		assert_eq(r.status, 200);
		assert(r.body.size() > 20000);
		assert_eq(r.body[0], '[');
		assert_eq(r.body[r.body.size()-1], ']');
	}
}
#endif
