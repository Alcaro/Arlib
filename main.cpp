#include "arlib.h"

//__attribute__((optimize("O0")))
__attribute__((noinline)) static uint32_t div1000_1(uint32_t n)
{
	return 274877907*(uint64_t)n >> 38;
}
//__attribute__((optimize("O0")))
__attribute__((noinline)) static uint32_t div1000_2(uint32_t n)
{
	return n/1000;
}
//__attribute__((optimize("O0")))
__attribute__((noinline)) static uint32_t div1000_3(uint32_t n)
{
	return n/1000;
}


int main(int argc, char** argv)
{
	arlib_init(NULL, argv);
	
	unsigned sum = 0;
	
	uint64_t time1 = time_us_ne();
	for (unsigned i=0;i<2000000000;i++)
	{
		sum += div1000_1(i); // 3745776us for two billion calls
	}
	uint64_t time2 = time_us_ne();
	for (unsigned i=0;i<2000000000;i++)
	{
		sum += div1000_2(i); // 7452683us for two billion calls
	}
	uint64_t time3 = time_us_ne();
	for (unsigned i=0;i<2000000000;i++)
	{
		sum += div1000_3(i); // 7470231us for two billion calls
	}
	uint64_t time4 = time_us_ne();
	
	printf("%lu %lu %lu %i\n", time2-time1, time3-time2, time4-time3, sum);
	
	/*
	uint64_t time = time_us_ne();
	uint64_t top20raw[20] = {};
	arrayvieww<uint64_t> top20 = top20raw;
uint64_t start=time;
uint64_t n=0;
	while (true)
	{
		uint64_t newtime = time_us_ne();
n++;
if(newtime-start>10000000)break;
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
printf("%lu calls in 10s\n",n);
*/
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
	return 0;
}
