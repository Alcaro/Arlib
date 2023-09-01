// Form1.cs
// A network unblocking tool
// Designed for one computer only

#include "arlib.h"
#include <gio/gio.h>
#include <libssh2.h>

// known banned domains: github.com *.github.io ica.se rustacean.net 12ft.io www.darwinawards.com *.alphacoders.com *.sourceforge.net

class ssh_policy {
public:
	bool use_ssh(bytesr ip, uint16_t port)
	{
		return false;
	}
	bool is_child_of(cstring domain, cstring parent)
	{
		return domain.endswith(parent) && (domain.length() == parent.length() || domain[domain.length()-parent.length()-1] == '.');
	}
	bool use_ssh(cstring domain, uint16_t port)
	{
		if (domain == "muncher.se" && port == 993) return true;
		if (port == 22) return true;
		if (port == 465) return true;
		if (is_child_of(domain, "github.com")) return true;
		if (is_child_of(domain, "github.io")) return true;
		if (is_child_of(domain, "githubassets.com")) return true;
		if (is_child_of(domain, "githubusercontent.com")) return true;
		if (is_child_of(domain, "discord.com")) return true;
		if (is_child_of(domain, "discordapp.com")) return true;
		if (is_child_of(domain, "discord.gg")) return true;
		if (is_child_of(domain, "sourceforge.net")) return true;
		return false;
	}
} policy;

static GDBusConnection* g_dbus;

static GVariant* get_property(const char * bus_name, const char * object_path, const char * interface_name, const char * property_name)
{
	GVariant* var = g_dbus_connection_call_sync(g_dbus, bus_name, object_path, "org.freedesktop.DBus.Properties", "Get",
	                                            g_variant_new("(ss)", interface_name, property_name), G_VARIANT_TYPE("(v)"),
	                                            G_DBUS_CALL_FLAGS_NONE, 500, nullptr, nullptr);
	if (!var) return nullptr;
	GVariant* var2 = g_variant_get_child_value(var, 0);
	GVariant* var3 = g_variant_get_variant(var2);
	g_variant_unref(var);
	g_variant_unref(var2);
	return var3;
}

static bool is_guestwnet(const char * path)
{
	GVariant* var = get_property("org.freedesktop.NetworkManager", path, "org.freedesktop.NetworkManager.Connection.Active", "Id");
	if (!var)
		return false;
	const char * ssid = g_variant_get_string(var, nullptr);
printf("SSID=%s\n",ssid);
	bool ret = (!strcmp(ssid, "GuestWNET"));
	g_variant_unref(var);
	return ret;
}

static waiter<void> guestwnet_coro;
static async<void> accept_guestwnet_eula()
{
	bool first = true;
	if (false)
	{
	again_sleep:
		co_await runloop2::await_timeout(timestamp::in_ms(15000));
	}
again:
	http_t::rsp rsp1 = co_await http_t::get("http://neverssl.com/");
	cstring location = rsp1.header("Location");
	puts(format("guestwnet eula: ",rsp1.status," ",location));
	if (rsp1.status == http_t::e_connect)
	{
		puts("retry...");
		if (first)
			co_await runloop2::await_timeout(timestamp::in_ms(2000));
		else
			co_await runloop2::await_timeout(timestamp::in_ms(5000));
		first = false;
		goto again;
	}
	if (rsp1.status == 200 && !location)
	{
		puts("done already.");
		co_return;
	}
	if (rsp1.status != 200 || !location)
	{
		printf("failed... %d\n", rsp1.status);
		goto again_sleep;
	}
	
	http_t http;
	
	http_t::req q2 = { location };
	q2.headers.append("User-Agent: Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:100.0) Gecko/20100101 Firefox/100.0");
	http_t::rsp rsp2 = co_await http.request(q2);
	puts(format("guestwnet eula: ",rsp2.status," ",rsp2.header("Set-Cookie")));
	
	http_t::req q3 = { rsp2.follow() };
	q3.headers.append("User-Agent: Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:100.0) Gecko/20100101 Firefox/100.0");
	http_t::rsp rsp3 = co_await http.request(q3);
	puts(format("guestwnet eula: ",rsp3.status," ",rsp3.header("Set-Cookie")));
	
	cstring cookie_portal;
	for (cstring header : rsp3.headers)
	{
		puts(format("guestwnet eula: ",rsp3.status," ",header));
		if (header.startswith("Set-Cookie: portalSessionId="))
			cookie_portal = header.substr(strlen("Set-Cookie: portalSessionId="), strlen("Set-Cookie: portalSessionId=")+36);
	}
	puts(format("guestwnet eula: ",cookie_portal));
	if (!cookie_portal)
		goto again_sleep;
	
	http_t::req q4 = { q3.loc };
	q4.loc.path = "/portal/DoCoA.action";
	q4.headers.append("User-Agent: Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:100.0) Gecko/20100101 Firefox/100.0");
	q4.body = ("delayToCoA=0&coaType=Reauth&waitForCoA=true&portalSessionId="+cookie_portal).bytes();
	http_t::rsp rsp4 = co_await http.request(q4);
	puts(format("guestwnet eula: ",rsp4.status," ",rsp4.text()));
	if (rsp4.status != 200)
		goto again_sleep;
}

static void set_proxy_state(bool enable)
{
	GSettings* set = g_settings_new("org.gnome.system.proxy");
puts(format("set proxy to ",enable ? "manual" : "none"));
	g_settings_set_string(set, "mode", enable ? "manual" : "none");
	g_object_unref(set);
	g_settings_sync();
}

static void process_primary_connection(const char * path)
{
	bool guestwnet = is_guestwnet(path);
	if (guestwnet)
	{
		if (!guestwnet_coro.is_waiting())
			accept_guestwnet_eula().then(&guestwnet_coro);
	}
	set_proxy_state(guestwnet);
}

static void dbus_cb(GDBusConnection* bus, const char * sender_name, const char * object_path,
                    const char * interface_name, const char * signal_names, GVariant* parameters, void* userdata)
{
	// parameters is always tuple("org.freedesktop.NetworkManager", dict(str -> any))
	GVariant* props = g_variant_get_child_value(parameters, 1);
	GVariant* prim_conn = g_variant_lookup_value(props, "PrimaryConnection", G_VARIANT_TYPE_OBJECT_PATH);
	g_variant_unref(props);
	if (!prim_conn) return;
	process_primary_connection(g_variant_get_string(prim_conn, nullptr));
	g_variant_unref(prim_conn);
}


class magic_proxy {
public:
	autoptr<socketlisten> incoming;
	
	socketbuf ssh_sock;
	LIBSSH2_SESSION * ssh_sess = nullptr;
	co_mutex ssh_mut;
	bool ssh_active = false;
	
	waiter<void> sshwait = make_waiter<&magic_proxy::sshwait, &magic_proxy::ssh_complete>();
	
	struct direct_child {
		autoptr<socket2> sock1;
		autoptr<socket2> sock2;
		size_t n_to_sock1 = 0;
		size_t n_to_sock2 = 0;
		uint8_t buf1[512];
		uint8_t buf2[512];
		
		// anything involving n_to_sock2
		waiter<void> wait1 = make_waiter<&direct_child::wait1, &direct_child::complete1>();
		
		// anything involving n_to_sock1
		waiter<void> wait2 = make_waiter<&direct_child::wait2, &direct_child::complete2>();
		
		void init(autoptr<socket2> sock1, autoptr<socket2> sock2)
		{
			this->sock1 = std::move(sock1);
			this->sock2 = std::move(sock2);
			n_to_sock1 = 0;
			n_to_sock2 = 0;
			set_waiters();
		}
		
		void set_waiters()
		{
			if (!wait1.is_waiting())
			{
				if (n_to_sock2)
					sock2->can_send().then(&wait1);
				else
					sock1->can_recv().then(&wait1);
			}
			if (!wait2.is_waiting())
			{
				if (n_to_sock1)
					sock1->can_send().then(&wait2);
				else
					sock2->can_recv().then(&wait2);
			}
		}
		
		void complete1()
		{
			if (n_to_sock2 == 0)
			{
				ssize_t n = sock1->recv_sync(buf1);
				if (n < 0)
					return cancel();
				n_to_sock2 = n;
			}
			if (n_to_sock2 != 0)
			{
				ssize_t n = sock2->send_sync(bytesr(buf1, n_to_sock2));
				if (n < 0)
					return cancel();
				memmove(buf1, buf1+n, n_to_sock2-n);
				n_to_sock2 -= n;
			}
			set_waiters();
		}
		
		void complete2()
		{
			if (n_to_sock1 == 0)
			{
				ssize_t n = sock2->recv_sync(buf2);
				if (n < 0)
					return cancel();
				n_to_sock1 = n;
			}
			if (n_to_sock1 != 0)
			{
				ssize_t n = sock1->send_sync(bytesr(buf2, n_to_sock1));
				if (n < 0)
					return cancel();
				memmove(buf2, buf2+n, n_to_sock1-n);
				n_to_sock1 -= n;
			}
			set_waiters();
		}
		
		void cancel()
		{
			wait1.cancel();
			wait2.cancel();
			sock1 = nullptr;
			sock2 = nullptr;
		}
	};
	refarray<direct_child> direct_children;
	
	struct ssh_child {
		autoptr<socket2> sock1;
		LIBSSH2_CHANNEL* sock2;
		
		size_t n_to_sock1 = 0;
		size_t n_to_sock2 = 0;
		uint8_t buf1[512];
		uint8_t buf2[512];
		
		// anything involving n_to_sock2
		waiter<void> wait1 = make_waiter<&ssh_child::wait1, &ssh_child::complete1>();
		
		// anything involving n_to_sock1
		waiter<void> wait2 = make_waiter<&ssh_child::wait2, &ssh_child::complete2>();
		
		void init(autoptr<socket2> sock1, LIBSSH2_CHANNEL* sock2)
		{
			this->sock1 = std::move(sock1);
			this->sock2 = std::move(sock2);
			n_to_sock1 = 0;
			n_to_sock2 = 0;
			set_waiters();
		}
		
		void set_waiters()
		{
			if (!sock1)
				return;
			
			if (!wait1.is_waiting() && !n_to_sock2)
				sock1->can_recv().then(&wait1);
			if (!wait2.is_waiting() && n_to_sock1)
				sock1->can_send().then(&wait2);
		}
		
		void complete1()
		{
			if (!sock1)
				return;
			
			if (n_to_sock2 == 0)
			{
				ssize_t n = sock1->recv_sync(buf1);
				if (n < 0)
					return cancel();
				n_to_sock2 = n;
				complete_ssh();
			}
			set_waiters();
		}
		
		void complete2()
		{
			if (!sock1)
				return;
			
			if (n_to_sock1 != 0)
			{
				ssize_t n = sock1->send_sync(bytesr(buf2, n_to_sock1));
				if (n < 0)
					return cancel();
				memmove(buf2, buf2+n, n_to_sock1-n);
				n_to_sock1 -= n;
				complete_ssh();
			}
			set_waiters();
		}
		
		void complete_ssh()
		{
			if (!sock1)
				return;
			
			if (n_to_sock1 == 0)
			{
				ssize_t n = libssh2_channel_read(sock2, (char*)buf2, sizeof(buf2));
//printf("rd ssh %ld\n", n);
//if(n>0)fwrite(buf2,1,n,stdout);
				if (n == LIBSSH2_ERROR_EAGAIN)
					n = 0;
				else if (n <= 0)
					n = -1;
				if (n < 0)
					return cancel();
				n_to_sock1 = n;
			}
			
			if (!sock1)
				return;
			
			if (n_to_sock2 != 0)
			{
				ssize_t n = libssh2_channel_write(sock2, (char*)buf1, n_to_sock2);
//printf("wr ssh %ld\n", n);
//if(n>0)fwrite(buf1,1,n,stdout);
				if (n == LIBSSH2_ERROR_EAGAIN)
					n = 0;
				else if (n <= 0)
					n = -1;
				if (n < 0)
					return cancel();
				memmove(buf1, buf1+n, n_to_sock2-n);
				n_to_sock2 -= n;
				complete1();
			}
			
			if (!sock1)
				return;
			
			set_waiters();
		}
		
		void cancel()
		{
			if (sock2)
				libssh2_channel_free(sock2);
			wait1.cancel();
			wait2.cancel();
			sock1 = nullptr;
			sock2 = nullptr;
		}
	};
	refarray<ssh_child> ssh_children;
	
	void ssh_complete()
	{
		// libssh2 doesn't seem to have any way to ask which, if any, channels are active; must poll all
		// nor any way to read incoming data (which is probably a keepalive) if there are no channels
		// if there are no channels, just discard the socket and create a new one later
		size_t n_children = 0;
		for (ssh_child& ch : ssh_children)
		{
			if (ch.sock1)
				n_children++;
			ch.complete_ssh();
		}
		if (!n_children)
			ssh_sock = nullptr;
		if (!ssh_sock)
		{
			ssh_children.reset();
			return;
		}
		if (!sshwait.is_waiting())
			ssh_sock.can_recv().then(&sshwait);
	}
	
	co_holder handshake_children;
	
	magic_proxy()
	{
		incoming = socketlisten::create(1080, bind_this(&magic_proxy::create_cb));
		if (!incoming)
			exit(1);
	}
	
	async<void> sock_inner(autoptr<socket2> sock)
	{
//puts("BEGIN");
		uint8_t buf[512];
		ssize_t n_recv = 0;
		
		bool v5_auth_sent = false;
		
	again:
		if ((size_t)n_recv == ARRAY_SIZE(buf)) // unlikely, but can happen if a goofy client submits an overlong domain name
			co_return;
		co_await sock->can_recv();
		ssize_t n_this = sock->recv_sync(bytesw(buf).skip(n_recv));
		if (n_this < 0)
			co_return;
		if (n_this == 0)
			goto again;
		n_recv += n_this;
		
		bytesr send_to_local;
		bytesr send_to_remote;
		
		bytesr remote_addr;
		cstring remote_domain;
		uint16_t remote_port;
		
		if (n_recv < 1)
			goto again;
		if (buf[0] == 4)
		{
			if (n_recv < 9)
				goto again;
			if (buf[1] != 1) // only command code = connect allowed
				co_return;
			if (buf[8] != 0) // just error out if USERID is nonempty
				co_return;
			remote_port = readu_be16(buf+2);
			remote_addr = bytesr(buf+4, 4);
			static const uint8_t socks4_reply[] = { 0, 90, 0,0, 0,0,0,0 }; // why are DSTPORT and DSTIP in socks4 spec if documented useless
			send_to_local = socks4_reply;
			
			if (remote_addr[0] == 0 && remote_addr[1] == 0 && remote_addr[2] == 0 && remote_addr[3] != 0)
			{
				bytesr domain_buf = bytesr(buf+9, n_recv-9);
				size_t domain_len = domain_buf.find(0);
				if (domain_len == (size_t)-1)
					goto again;
				
				remote_domain = domain_buf.slice(0, domain_len);
				send_to_remote = domain_buf.skip(domain_len+1);
			}
			else
			{
				send_to_remote = bytesr(buf+9, n_recv-9);
			}
		}
		else if (buf[0] == 5)
		{
			if (n_recv >= 3 && !v5_auth_sent)
			{
				if (buf[1] == 1 && buf[2] == 0) // NMETHODS = 1, METHOD = NO AUTHENTICATION REQUIRED
				{
					static const uint8_t socks5_auth_reply[] = { 5, 0 }; // version, chosen method
					send_to_local = socks5_auth_reply;
					while (send_to_local)
					{
						co_await sock->can_send();
						ssize_t n = sock->send_sync(send_to_local);
						if (n < 0)
							co_return;
						send_to_local = send_to_local.skip(n);
					}
					v5_auth_sent = true;
				}
				else
					co_return;
			}
			if (n_recv < 3 + 4+2)
				goto again;
			if (buf[3] != 5 || buf[4] != 1 || buf[5] != 0) // VER=5, CMD=CONNECT, RSV=0
				co_return;
			
			static const uint8_t socks5_bind_reply[] = { 5, 0, 0, 1, 0,0,0,0, 0,0 };
			send_to_local = socks5_bind_reply;
			
			if (buf[6] == 1)
			{
				if (n_recv < 3+4+4+2)
					goto again;
				
				remote_port = readu_be16(buf+3+4+4);
				remote_addr = bytesr(buf+3+4, 4);
				send_to_remote = bytesr(buf, n_recv).skip(3+4+4+2);
			}
			else if (buf[6] == 4)
			{
				if (n_recv < 3+4+16+2)
					goto again;
				
				remote_port = readu_be16(buf+3+4+16);
				remote_addr = bytesr(buf+3+4, 16);
				send_to_remote = bytesr(buf, n_recv).skip(3+4+16+2);
			}
			else if (buf[6] == 3)
			{
				if (n_recv < 3+4+buf[7]+2)
					goto again;
				
				remote_port = readu_be16(buf+3+4+1+buf[7]);
				remote_domain = bytesr(buf+3+4+1, buf[7]);
				send_to_remote = bytesr(buf, n_recv).skip(3+4+1+buf[7]+2);
			}
		}
		else
			co_return;
		
		if (send_to_remote)
		{
			puts("PROXY CLIENT IS TOO TALKACTIVE");
			co_return;
		}
		
		bool use_ssh = (remote_domain ? policy.use_ssh(remote_domain, remote_port) : policy.use_ssh(remote_addr, remote_port));
		string remote_target = remote_domain ? remote_domain : socket2::address(remote_addr).as_str();
		puts("connect "+remote_target+" "+tostring(remote_port)+" "+(use_ssh?"proxy":"direct"));
		if (use_ssh)
		{
//puts("BEGIN_SSH");
			co_mutex::lock lk = co_await ssh_mut;
			
			if (!ssh_active || !ssh_sock)
			{
				for (ssh_child& ch : ssh_children)
					ch.cancel();
				if (ssh_sess)
					libssh2_session_free(ssh_sess);
				sshwait.cancel();
				
				ssh_active = false;
				ssh_sock = co_await socket2::create("muncher.se", 8080);
				
				ssh_sess = libssh2_session_init_ex(nullptr, nullptr, nullptr, this);
				libssh2_session_set_blocking(ssh_sess, false);
				
				typedef ssize_t (*libssh2_sendcb_t)(libssh2_socket_t sockfd, const void * buffer, size_t length, int flags, void** abstract);
				typedef ssize_t (*libssh2_recvcb_t)(libssh2_socket_t sockfd, void * buffer, size_t length, int flags, void** abstract);
				libssh2_sendcb_t send_cb = [](libssh2_socket_t sockfd, const void * buffer, size_t length, int flags, void** abstract) -> ssize_t
				{
					// the only possible flag is MSG_NOSIGNAL, per LIBSSH2_SOCKET_SEND_FLAGS in libssh2_priv.h
					// safe to ignore
					magic_proxy* self = (magic_proxy*)*abstract;
					if (!self->ssh_sock)
						return -1;
					self->ssh_sock.send(bytesr((uint8_t*)buffer, length));
					return length;
				};
				libssh2_recvcb_t recv_cb = [](libssh2_socket_t sockfd, void * buffer, size_t length, int flags, void** abstract) -> ssize_t
				{
					magic_proxy* self = (magic_proxy*)*abstract;
					if (!self->ssh_sock)
						return -1;
					bytesr by = self->ssh_sock.bytes_sync(length);
					size_t n = by.size();
					memcpy(buffer, by.ptr(), n);
					if (n == 0) return -EAGAIN;
					else return n;
				};
				libssh2_session_callback_set(ssh_sess, LIBSSH2_CALLBACK_SEND, (void*)send_cb);
				libssh2_session_callback_set(ssh_sess, LIBSSH2_CALLBACK_RECV, (void*)recv_cb);
				
				while (true)
				{
					int rc = libssh2_session_handshake(ssh_sess, -2); // fd -1 would make more sense, but libssh2 hardcodes that exact value
					printf("e=%d\n",rc);
					if (rc == 0)
						break;
					else if (rc != LIBSSH2_ERROR_EAGAIN || !ssh_sock)
						co_return;
					co_await ssh_sock.can_recv();
				}
				
				puts("connect done");
				
				LIBSSH2_KNOWNHOSTS* hosts = libssh2_knownhost_init(ssh_sess);
				libssh2_knownhost_readfile(hosts, "/home/walrus/.ssh/known_hosts", LIBSSH2_KNOWNHOST_FILE_OPENSSH);
				size_t fp_len;
				const char * fingerprint = libssh2_session_hostkey(ssh_sess, &fp_len, nullptr);
				int ok = libssh2_knownhost_check(hosts, "muncher.se", fingerprint, fp_len,
				                                 LIBSSH2_KNOWNHOST_TYPE_PLAIN|LIBSSH2_KNOWNHOST_KEYENC_RAW, nullptr);
				libssh2_knownhost_free(hosts);
				
				if (ok != LIBSSH2_KNOWNHOST_CHECK_MATCH)
					co_return;
				
				while (true)
				{
					int rc = libssh2_userauth_publickey_fromfile(ssh_sess, "alcaro", "/home/walrus/.ssh/id_ed25519.pub",
					                                             "/home/walrus/.ssh/id_ed25519", nullptr); 
					printf("e=%d\n",rc);
					if (rc == 0)
						break;
					else if (rc != LIBSSH2_ERROR_EAGAIN || !ssh_sock)
						co_return;
					co_await ssh_sock.can_recv();
				}
				
				ssh_active = true;
//puts("connected");
			}
			
			LIBSSH2_CHANNEL* remote_ssh;
			while (true)
			{
				remote_ssh = libssh2_channel_direct_tcpip(ssh_sess, remote_target, remote_port);
//printf("remote_ssh %s %p %d\n", (const char*)remote_target, remote_ssh, libssh2_session_last_errno(ssh_sess));
				if (remote_ssh)
					break;
				if (libssh2_session_last_errno(ssh_sess) == LIBSSH2_ERROR_EAGAIN)
				{
					co_await ssh_sock.can_recv();
					continue;
				}
				else
				{
					// just drop it
					// if it was a network error, it's been handled inside LIBSSH2_CALLBACK_RECV
					co_return;
				}
			}
			
			while (send_to_local)
			{
				co_await sock->can_send();
				ssize_t n = sock->send_sync(send_to_local);
				if (n < 0)
					co_return;
				send_to_local = send_to_local.skip(n);
			}
			
			ssh_child* chp = nullptr;
			for (ssh_child& ch : ssh_children)
			{
				if (!ch.sock1)
				{
					chp = &ch;
					break;
				}
			}
			if (!chp)
				chp = &ssh_children.append();
			chp->init(std::move(sock), remote_ssh);
			
			ssh_complete();
		}
		else
		{
			autoptr<socket2> remote_sock;
			if (remote_domain)
				remote_sock = co_await socket2::create(remote_domain, remote_port);
			else
				remote_sock = co_await socket2::create(socket2::address(remote_addr, remote_port));
			if (!remote_sock)
				co_return;
			
			while (send_to_local)
			{
				co_await sock->can_send();
				ssize_t n = sock->send_sync(send_to_local);
				if (n < 0)
					co_return;
				send_to_local = send_to_local.skip(n);
			}
			
			while (send_to_remote)
			{
				co_await remote_sock->can_send();
				ssize_t n = remote_sock->send_sync(send_to_remote);
				if (n < 0)
					co_return;
				send_to_remote = send_to_remote.skip(n);
			}
			
	//puts("READY");
			direct_child* chp = nullptr;
			for (direct_child& ch : direct_children)
			{
				if (!ch.sock1)
				{
					chp = &ch;
					break;
				}
			}
			if (!chp)
				chp = &direct_children.append();
			chp->init(std::move(sock), std::move(remote_sock));
		}
		
		co_return;
	}
	
	void create_cb(autoptr<socket2> sock)
	{
		handshake_children.add(sock_inner(std::move(sock)));
	}
};

int main(int argc, char** argv)
{
	struct sigaction act = {};
	act.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &act, nullptr);
	
	act.sa_sigaction = [](int, siginfo_t*, void*) {
		struct sigaction act = {};
		act.sa_handler = SIG_DFL;
		sigaction(SIGINT, &act, nullptr);
		
		// doing this in a signal handler is UB, but 99% chance it's sitting in poll() and it's safe
		// the last percent will probably crash and terminate anyways; worst case, I'll ^C again
		set_proxy_state(false);
		
		raise(SIGINT);
	};
	act.sa_flags = SA_SIGINFO;
	sigaction(SIGINT, &act, nullptr);
	
	if (libssh2_init(0) < 0)
		abort();
	
	g_dbus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
	g_dbus_connection_signal_subscribe(g_dbus, "org.freedesktop.NetworkManager", "org.freedesktop.DBus.Properties", "PropertiesChanged",
	                                   "/org/freedesktop/NetworkManager", nullptr, G_DBUS_SIGNAL_FLAGS_NONE, dbus_cb, nullptr, nullptr);
	
	GVariant* prim_conn = get_property("org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager",
	                                   "org.freedesktop.NetworkManager", "PrimaryConnection");
	process_primary_connection(g_variant_get_string(prim_conn, nullptr));
	g_variant_unref(prim_conn);
	
	magic_proxy prox;
	
	while (true)
		runloop2::step();
}
