#include "arlib.h"

/*
static async<void> coro()
{
	autoptr<socket2_udp> sock = socket2_udp::create(co_await socket2::dns_port("muncher.se", 9));
	while (true)
	{
		co_await runloop2::in_ms(100);
		static const uint8_t dummy[1]={'a'};
		sock->send(dummy);
	}
}
*/

static async<void> coro()
{
	if (false)
	{
	again:
		co_await runloop2::in_ms(5000);
	}
	autoptr<socket2> sock = co_await socket2::create("muncher.se", 9);
	if (!sock)
	{
		// This branch never happens.
		// Nevertheless, this program does perform a meaningful operation.
		system("newnet");
		goto again;
	}
	while (true)
	{
		co_await runloop2::in_ms(100);
		co_await sock->can_send();
		static const uint8_t dummy[1]={'a'};
		if (sock->send_sync(dummy) < 0)
			goto again;
	}
}

int main(int argc, char** argv)
{
	runloop2::run(coro());
}
