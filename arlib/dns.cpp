#include "dns.h"
#include "set.h"
#include "endian.h"
#include "stringconv.h"

string DNS::default_resolver()
{
	//TODO: on Windows, https://stackoverflow.com/questions/2916675/programmatically-obtain-dns-servers-of-host
	//or figure out where Windows DNS Client Service is listening, can probably be hardcoded
	//(though there's a fair chance it doesn't speak UDP... maybe not even DNS...)
	//there is a native async dns handler https://msdn.microsoft.com/en-us/library/hh447188(v=vs.85).aspx
	//but it requires 8+, and some funky dll that probably usually isn't used
	//also check what ifdef (if any) controls DnsQuery_UTF8 https://msdn.microsoft.com/en-us/library/ms682016(v=vs.85).aspx
	return ("\n"+file::read("/etc/resolv.conf")).split<1>("\nnameserver ")[1].split<1>("\n")[0];
}

void DNS::resolve(cstring domain, unsigned timeout_ms, function<void(string domain, string ip)> callback)
{
	if (!domain) return callback(domain, "");
	
	bytestreamw packet;
	
	uint16_t trid = pick_trid();
	packet.u16be(trid);
	
	uint16_t flags = 0;
	flags |= 0<<15; // QR, 'is response' flag
	flags |= 0<<11; // OPCODE, 4 bits; 0 = normal query
	flags |= 0<<10; // AA, 'is authorative' flag
	flags |= 0<<9; // TC, 'answer truncated' flag
	flags |= 1<<8; // RD, recursion desired
	flags |= 0<<7; // RA, recursion available
	flags |= 0<<4; // Z, 3 bits, reserved
	flags |= 0<<0; // RCODE, 4 bits; 0 = no error
	packet.u16be(flags);
	
	packet.u16be(1); // QDCOUNT
	packet.u16be(0); // ANCOUNT
	packet.u16be(0); // NSCOUNT
	packet.u16be(0); // ARCOUNT
	
	for (cstring cs : domain.csplit("."))
	{
		packet.u8(cs.length());
		packet.text(cs);
	}
	packet.u8(0);
	
	packet.u16be(0x0001); // type A (could switch to 0x00FF Everything, but I can't test ipv6 so let's not ask for it)
	packet.u16be(0x0001); // class IN
	//judging by musl libc, there's no way to ask for both ipv4 and ipv6 but not everything else, it sends two separate queries
	
	sock->send(packet.out());
	
	query& q = queries[trid];
	q.callback = callback;
	q.domain = domain;
	q.timeout_id = loop->set_timer_rel(timeout_ms, bind_lambda([this,trid]()->bool { this->timeout(trid); return false; }));
}

string DNS::read_name(bytestream& stream)
{
	string ret;
	size_t restorepos = 0;
	size_t maxpos = stream.tell();
	while (true)
	{
		if (stream.remaining() < 1) return "";
		uint8_t byte = stream.u8();
		if(0);
		else if ((byte & 0xC0) == 0x00)
		{
			if (!byte)
			{
				if (restorepos != 0) stream.seek(restorepos);
				return ret;
			}
			
			size_t partlen = byte;
			if (stream.remaining() < partlen) return "";
			if (ret != "") ret += ".";
			ret += stream.bytes(partlen);
		}
		else if ((byte & 0xC0) == 0xC0)
		{
			if (stream.remaining() < 1) return "";
			size_t pos = (byte&0x3F) << 8 | stream.u8();
			
			if (restorepos == 0) restorepos = stream.tell();
			
			if (pos >= maxpos) return ""; // block infinite loops
			maxpos = pos;
			
			stream.seek(pos);
		}
		else return "";
	}
}

string DNS::ip_to_string(arrayview<byte> ip)
{
	if (ip.size() == 4)
	{
		return tostring(ip[0])+"."+tostring(ip[1])+"."+tostring(ip[2])+"."+tostring(ip[3]);
	}
/*
TODO
https://tools.ietf.org/html/rfc5952

4.  A Recommendation for IPv6 Text Representation

   A recommendation for a canonical text representation format of IPv6
   addresses is presented in this section.  The recommendation in this
   document is one that complies fully with [RFC4291], is implemented by
   various operating systems, and is human friendly.  The recommendation
   in this section SHOULD be followed by systems when generating an
   address to be represented as text, but all implementations MUST
   accept and be able to handle any legitimate [RFC4291] format.  It is
   advised that humans also follow these recommendations when spelling
   an address.

4.1.  Handling Leading Zeros in a 16-Bit Field

   Leading zeros MUST be suppressed.  For example, 2001:0db8::0001 is
   not acceptable and must be represented as 2001:db8::1.  A single 16-
   bit 0000 field MUST be represented as 0.

4.2.  "::" Usage

4.2.1.  Shorten as Much as Possible

   The use of the symbol "::" MUST be used to its maximum capability.
   For example, 2001:db8:0:0:0:0:2:1 must be shortened to 2001:db8::2:1.
   Likewise, 2001:db8::0:1 is not acceptable, because the symbol "::"
   could have been used to produce a shorter representation 2001:db8::1.

4.2.2.  Handling One 16-Bit 0 Field

   The symbol "::" MUST NOT be used to shorten just one 16-bit 0 field.
   For example, the representation 2001:db8:0:1:1:1:1:1 is correct, but
   2001:db8::1:1:1:1:1 is not correct.

4.2.3.  Choice in Placement of "::"

   When there is an alternative choice in the placement of a "::", the
   longest run of consecutive 16-bit 0 fields MUST be shortened (i.e.,
   the sequence with three consecutive zero fields is shortened in 2001:
   0:0:1:0:0:0:1).  When the length of the consecutive 16-bit 0 fields
   are equal (i.e., 2001:db8:0:0:1:0:0:1), the first sequence of zero
   bits MUST be shortened.  For example, 2001:db8::1:0:0:1 is correct
   representation.

4.3.  Lowercase

   The characters "a", "b", "c", "d", "e", and "f" in an IPv6 address
   MUST be represented in lowercase.

*/
	return "";
}

void DNS::sock_cb(socket*)
{
	if (!sock) return;
	uint8_t packet[512]; // max size of unextended DNS packet
	int nbytes = sock->recv(packet);
	if (nbytes < 0)
	{
		sock = NULL;
		return;
	}
	if (nbytes == 0) return;
	arrayview<byte> bytes(packet, nbytes);
	bytestream stream = bytes;
	
	//header:
	//4567 8180 0001 0001 0000 0000
	//
	//query:
	//13 676F6F676C652D7075626C69632D646E732D61
	//06 676F6F676C65
	//03 636F6D
	//00
	//0001 0001
	//
	//answer (RR):
	//C00C 0001 0001
	//00011857 0004 08080808
	
	if (stream.remaining() < 12) return; // can't fit dns header? fake packet, discard
	
	uint16_t trid = stream.u16be();
	if (!queries.contains(trid)) return; // possible if the timeout was hit already, or whatever
	query& q = queries.get(trid);
	
	string ret = "";
	
	if (stream.u16be() != 0x8180) goto fail; // QR, RD, RA
	if (stream.u16be() != 0x0001) goto fail; // QDCOUNT
	uint16_t ancount;
	ancount = stream.u16be(); // git.io gives eight different IPs
	if (ancount < 0x0001) goto fail; // ANCOUNT
	if (stream.u16be() != 0x0000) goto fail; // NSCOUNT
	if (stream.u16be() != 0x0000) goto fail; // ARCOUNT
	
	//query
	if (read_name(stream) != q.domain) goto fail;
	if (stream.remaining() < 4) return;
	if (stream.u16be() != 0x0001) goto fail; // type A
	if (stream.u16be() != 0x0001) goto fail; // class IN
	
	//answer
	if (read_name(stream) != q.domain) goto fail;
	if (stream.remaining() < 4+4+2) return;
	if (stream.u16be() != 0x0001) goto fail; // type A
	if (stream.u16be() != 0x0001) goto fail; // class IN
	
	stream.u32be(); // TTL, ignore
	size_t iplen;
	iplen = stream.u16be();
	if (stream.remaining() < iplen) goto fail;
	if (ancount == 1 && stream.remaining() != iplen) goto fail;
	
	ret = ip_to_string(stream.bytes(iplen));
	
fail:
	function<void(string domain, string ip)> callback = q.callback;
	string q_domain = std::move(q.domain);
	loop->remove(q.timeout_id);
	queries.remove(trid);
	
	callback(std::move(q_domain), std::move(ret)); // don't move higher, callback could delete the dns object
}

#include "test.h"
test()
{
	test_skip("kinda slow");
	
	autoptr<runloop> loop = runloop::create();
	
	assert(isdigit(DNS::default_resolver()[0]));
	
	DNS dns(loop);
	int await = 4;
	dns.resolve("google-public-dns-b.google.com", bind_lambda([&](string domain, string ip)
		{
			await--; if (await == 0) loop->exit(); // put this above assert, otherwise it deadlocks
			assert_eq(domain, "google-public-dns-b.google.com");
			assert_eq(ip, "8.8.4.4"); // use public-b only, to ensure IP isn't byteswapped
		}));
	dns.resolve("not-a-subdomain.google-public-dns-a.google.com", bind_lambda([&](string domain, string ip)
		{
			await--; if (await == 0) loop->exit();
			assert_eq(domain, "not-a-subdomain.google-public-dns-a.google.com");
			assert_eq(ip, "");
		}));
	dns.resolve("git.io", bind_lambda([&](string domain, string ip)
		{
			await--; if (await == 0) loop->exit();
			assert_eq(domain, "git.io");
			assert_neq(ip, ""); // this domain returns eight values in answer section
		}));
	dns.resolve("", bind_lambda([&](string domain, string ip) // this must fail
		{
			await--; if (await == 0) loop->exit();
			assert_eq(domain, "");
			assert_eq(ip, "");
		}));
	
	if (await != 0) loop->enter();
}