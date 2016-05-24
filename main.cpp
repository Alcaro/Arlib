#include "arlib.h"
#include <stdio.h>

void sandproc(sandbox* box)
{
	puts("Hi!");
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
	box->wait(0);
}
