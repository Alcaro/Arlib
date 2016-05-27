#include "arlib.h"
#include <stdio.h>

void sandproc(sandbox* box)
{
	puts("Hi!");
	
	FILE* ok = fopen("main.cpp", "rt");
	FILE* deny = fopen("a.cpp", "rt");
	printf("allow: %p deny: %p\n", ok, deny);
	
	char* g = box->shalloc<char>(0, 10);
	strcpy(g, "test");
	
	box->release(0);
}

int main(int argc, char * argv[])
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
	
	box->wait(0);
	
	puts(g);
}
