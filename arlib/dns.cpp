#include "dns.h"

class DNS {
	autoptr<socket> sock;
	
	void init(cstring resolver, int port)
	{
		sock = socket::create_udp(resolver, port);
	}
	void sock_cb(socket*)
	{
		
	}
public:
	static string default_resolver();
	
	DNS() { init(default_resolver(), 53); }
	DNS(cstring resolver, int port) { init(resolver, port); }
	
	void attach(runloop* loop) { if (sock) sock->callback(loop, bind_this(&DNS::sock_cb), NULL); }
	void resolve(cstring domain, function<void(string)> callback)
	{
		sock->send(domain.bytes());
		callback("");
	}
};

string DNS::default_resolver()
{
	//TODO: on Windows, https://stackoverflow.com/questions/2916675/programmatically-obtain-dns-servers-of-host
	return ("\n"+file::read("/etc/resolv.conf")).split<1>("\nnameserver ")[1].split<1>("\n")[0];
}

#include "test.h"
test()
{
	//test_skip("kinda slow");
	
	autoptr<runloop> loop = runloop_blocktest_create();
	
	assert(isdigit(DNS::default_resolver()[0]));
	
	DNS dns;
	dns.attach(loop);
	bool done = false;
	dns.resolve("google-public-dns-a.google.com", bind_lambda([&](string ip)
		{
			done = true; // not needed in the success case, but if dns.resolve immediately fails, it's needed to avoid deadlock
			loop->exit(); // put this first, otherwise it deadlocks
			assert_eq(ip, "8.8.8.8");
		}));
	
	if (!done) loop->enter();
}
