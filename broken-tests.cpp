#include "arlib.h"
#include <stdio.h>

#if 0
static void sandproc(sandbox* box)
{
	puts("Hi!");
	
	FILE* ok = fopen("main.cpp", "rt");
	FILE* deny = fopen("a.cpp", "rt");
	printf("allow: %p deny: %p\n", ok, deny);
	
	char* g = box->shalloc<char>(0, 10);
	printf("HI %p\n",g);
	strcpy(g, "test");
	
	box->release(0);
	
	//while(true)puts("a");
}

static void sandtest(int argc, char * argv[])
{
for (int i=0;i<argc;i++)printf("%i:%s\n",i,argv[i]);
	sandbox::enter(argc, argv);
	
	sandbox::params par{};
	par.flags = sandbox::params::allow_stdout;
	par.n_channel=1;
	par.n_shmem=1;
	par.run = sandproc;
	sandbox* box = sandbox::create(&par);
	puts("Hello!");
	
	char* g = (char*)box->shalloc(0, 10);
	printf("HELLO %p\n",g);
	box->wait(0);
	puts(g);
}

socket* roundtrip(socket* sock_)
{
	//socketssl* sock = (socketssl*)sock_;
	//sock->q();
	//uint8_t data[4096];
	//int datalen = sock->serialize_size();
	//int fd = sock->serialize(data, datalen);
	//
//printf("serialize: %i u8[%i]\n", fd, datalen);
	//return socketssl::unserialize(fd, data, datalen);
	return sock_;
}

int g(int argc, char * argv[])
{
	//sandtest(argc, argv);
	
//#define DOMAIN "www.microsoft.com"
//#define DOC    "/"

//#define DOMAIN "muncher.se"
//#define DOC    "/404.html"

#define DOMAIN "www.howsmyssl.com"
#define DOC    "/a/check"

	socket* sock = socketssl::create(DOMAIN, 443);
	printf("s=%i\n", sock->send("GET " DOC " HTTP/1.1\n"));
	//sock = roundtrip(sock);
	roundtrip(sock);
	printf("s=%i\n", sock->send("Host: " DOMAIN "\nConnection: close\n\n"));
	//for (int i=0;i<5;i++)
	//
	//socket* sock = socket::create("floating.muncher.se", 1686);
	//printf("s=%i\n", sock->send("12345678123456781234567812345678"));
	
	while (true)
	{
		char ret[8192];
		int b = sock->recv(ret, sizeof(ret), true);
		if (b==0) continue;
		printf("r=%i\n", b);
		if (b<0) break;
		puts(ret);
	}
	return 0;
}
#endif
