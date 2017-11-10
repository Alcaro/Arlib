#include "arlib.h"

int main(int argc, char** argv)
{
	uint64_t time = time_us_ne();
	array<uint64_t> top20;
	for (int i=0;i<20;i++) top20[i] = 0;
	while (true)
	{
		uint64_t newtime = time_us_ne();
		uint64_t diff = newtime - time;
		time = newtime;
		if (diff > top20[0])
		{
			top20[0] = diff;
			top20.sort();
			
			puts(top20.select([](uint64_t n)->string { return tostring(n); }).as_array().join(",")+" "+tostring(diff));
			
			time = time_us_ne();
		}
	}
/*
	sandproc ch;
	ch.set_stdout(process::output::create_stdout());
	ch.set_stderr(process::output::create_stderr());
	ch.fs_grant_syslibs(argv[1]);
	ch.fs_grant_cwd(100);
	
	//for gcc
	ch.fs_grant("/usr/bin/make");
	ch.fs_grant("/usr/bin/gcc");
	ch.fs_grant("/usr/bin/g++");
	ch.fs_grant("/usr/bin/as");
	ch.fs_grant("/usr/bin/ld");
	ch.fs_grant("/usr/lib/");
	ch.fs_hide("/usr/gnu/");
	ch.fs_grant("/lib/");
	ch.fs_hide("/usr");
	ch.fs_hide("/usr/local/include/");
	ch.fs_grant("/usr/include/");
	ch.fs_hide("/usr/x86_64-linux-gnu/");
	ch.fs_hide("/usr/bin/gnm");
	ch.fs_hide("/bin/gnm");
	ch.fs_grant("/usr/bin/nm");
	ch.fs_hide("/usr/bin/gstrip");
	ch.fs_hide("/bin/gstrip");
	ch.fs_grant("/usr/bin/strip");
	ch.fs_hide("/usr/bin/uname");
	ch.fs_grant("/bin/uname");
	ch.fs_grant("/usr/bin/objdump");
	ch.fs_hide("/usr/bin/grep");
	ch.fs_grant("/bin/grep");
	
	ch.launch(argv[1], arrayview<const char*>(argv+2, argc-2).cast<string>());
	ch.wait();
*/
}
