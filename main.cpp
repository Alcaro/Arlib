#include "arlib.h"

int main(int argc, char** argv)
{
	zip z;
	{
		array<byte> map = file::read(argv[1]);
		if (!map)
		{
			puts("fread");
			return 0;
		}
		if (!z.init(map))
		{
			puts("zipopen");
			return 0;
		}
	}
	
	puts(z.repaired() ? "CORRUPT:YES" : "CORRUPT:NO");
	array<string> fnames = z.files();
	for (string fname : fnames)
	{
		array<byte> data;
		string error;
		z.read(fname, data, &error);
		puts(fname+" "+tostring(data.size())+" "+error+" "+tostringhex(crc32(data)));
	}
	
	if (argv[2])
	{
		z.clean();
		file::write(argv[2], z.pack());
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
