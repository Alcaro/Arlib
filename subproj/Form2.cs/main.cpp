#include "arlib.h"

// Contrary to how it looks, this program does have usecases.

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
