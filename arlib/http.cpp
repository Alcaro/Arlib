#ifdef ARLIB_SOCKET
#include "http.h"
#include "test.h"
#include "stringconv.h"

class http {
public:
	struct req {
		//Every field except 'url' is safe to leave as default.
		string url;
		
		string method;
		//These headers are implied by other members and added automatically:
		//Connection: keep-alive
		//Host: <from url>
		//Content-Length: <from postdata>
		array<string> headers; // TODO: multimap
		array<byte> postdata;
		
		//Varying these two between requests may give overpermissive results.
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
			e_only_one      = -5, // server used Connection: close or similar on a previous request
			//may also be a normal http status code (200, 302, 404, etc)
		};
		int status;
		
		string status_str;
		array<string> headers; // TODO: multimap
		array<byte> body;
		
		operator arrayvieww<byte>() { return body; }
		//operator string() { return body; } // throws ambiguity errors if enabled
	};
	
	static string request(string url) { return std::move(request((req)url).body); }
	static rsp request(const req& r) { http obj; obj.send(r); return std::move(obj.recv()); }
	
	//Multiple requests may be sent to the same object. This will make them use HTTP Keep-Alive.
	//The requests must be to the same protocol-domain-port tuple.
	//They will be returned in the same order as they're sent.
	//Returns false if the request is invalid or the host can't be reached. This does not affect the object.
	//If the server doesn't support Keep-Alive, remaining requests are cancelled.
	bool send(const req& r);
	bool ready();
	void progress(size_t& done, size_t& total); // Both are 0 if unknown.
	//void onReady(function<void(void)> callback);
	void await(); // Returns immediately if there's anything unfinished going on, or if nothing more is expected to be returned (including error in send()).
	rsp recv(bool partial = false); // Calls await() if needed. Only works once per send().
	
private:
	array<rsp> done;
	int n_unfinished = 0; // includes the one currently being processed
	
	int m_error = 0;
	bool error(int err) { sock = NULL; m_error = err; return false; }
	
	size_t maxsize = 0;
	uint32_t maxtime = 0;
	
	size_t sizelimit;
	time_t timelimit;
	
	struct location {
		bool https;
		string domain;
		int port;
		string loc;
	};
	location host; // host.loc is ignored
	autoptr<socket> sock;
	
	array<byte> received;
	
	array<byte> body;
	array<string> headers;
	
	void activity();
	
	
	static bool parseUrl(cstring url_, bool relative, location& out)
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
};

bool http::send(const req& r)
{
	bool freshsock;
	if (!sock)
	{
newsock:
		if (!parseUrl(r.url, false, this->host)) return error(rsp::e_bad_url);
		if (host.https) sock = socket::bufwrap(socketssl::create(host.domain, host.port));
		else sock = socket::bufwrap(socket::create(host.domain, host.port));
		if (!sock) return error(rsp::e_connect);
		freshsock = true;
	}
	else
	{
		location loc;
		if (!parseUrl(r.url, false, loc)) return error(rsp::e_bad_url);
		if (loc.https != host.https || loc.domain != host.domain || loc.port != host.port)
		{
			return error(rsp::e_different_url);
		}
		host.loc = loc.loc;
		freshsock = false;
	}
	
	cstring method = r.method;
	if (!method) method = (r.postdata ? "POST" : "GET");
	sock->send(method+" "+host.loc+" HTTP/1.1\r\n");
	
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
		sock->send(head+"\r\n");
	}
	
	if (!httpHost) sock->send("Host: "+host.domain+"\r\n");
	if (method=="POST" && !httpContentLength) sock->send("Content-Length: "+tostring(r.postdata.size())+"\r\n");
	if (method=="POST" && !httpContentType) sock->send("Content-Type: application/x-www-form-urlencoded;charset=UTF-8\r\n");
	//if (!httpConnection) sock->send("Connection: keep-alive\r\n");
	if (!httpConnection) sock->send("Connection: close\r\n");
	
	sock->send("\r\n");
	
	int bytes = sock->send(r.postdata);
	if (bytes < 0)
	{
		if (!freshsock) goto newsock;
		else return error(rsp::e_broken);
	}
	
	n_unfinished++;
	return true;
}

void http::await()
{
	if (m_error || !n_unfinished) return;
	
	//TODO
	rsp r;
	while (true)
	{
		array<byte> buf;
		int bytes = sock->recv(buf);
		if (bytes<0) break;
		r.body += buf;
	}
	return r;
}

http::rsp http::recv(bool partial)
{
	if (m_error || !n_unfinished)
	{
		rsp r;
		r.success = false;
		r.status = (m_error ? m_error : e_not_sent);
		return r;
	}
	await();
	rsp r = std::move(done[0]);
	done.remove(0);
	return r;
}


test()
{
	{
		string ret = http::request("http://detectportal.firefox.com/success.txt");
		assert_eq(ret, "success");
	}
	
	{
		http h;
		h.send(http::req("http://detectportal.firefox.com/success.txt"));
		assert_eq((string)h.recv(), "success");
		h.send((string)"http://detectportal.firefox.com/success.txt");
		assert_eq((string)h.recv(), "success");
	}
	
	{
		http::req r;
		r.url = "http://detectportal.firefox.com/success.txt";
		r.headers.append("Host: detectportal.firefox.com");
		r.postdata.append('x');
		
		http h;
		h.send(r);
		h.send(r);
		assert_eq((string)h.recv(), "success");
		assert_eq((string)h.recv(), "success");
	}
}
#endif
