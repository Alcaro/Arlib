#include "arlib.h"

inline void e(const char*w)
{
	//image i;
	//puts(w);
	//i.init_decode_jpg(file::readall(file::exepath()+w));
	//for (unsigned y=0;y<i.height;y++)
	//for (unsigned x=0;x<i.width;x++)
	//{
		//printf("%.8X%c",i.pixels32[y*i.stride/4 + x], x==i.width-1 ? '\n' : ' ');
	//}
}
int main(int argc, char** argv)
{
	//arlib_init(NULL, argv);
	//e("jpg/black.jpg");
	//e("jpg/white.jpg");
	//e("jpg/whiteleft-blackright.jpg");
	//e("jpg/whitetop-blackbot.jpg");
	//e("jpg/whitetopleft-blackbotright.jpg");
	//e("jpg/whitetopleft-blackbotright-wide.jpg");
	//e("jpg/whitetopleft-blackbotright-tall.jpg");
	//e("jpg/whitetopleft-blackbotright-big.jpg");
	//e("jpg/whitetopleft-blackbotright-loop.jpg");
	
	puts(tostringhex(crc32(cstring("floating muncher").bytes())));

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
