#include "arlib.h"

int main(int argc, char** argv)
{
	array<string> lines1 = string(file::read("/home/alcaro/x/Glide64_Ini_sjis.c"))                 .split("\n");
	array<string> lines2 = string(file::read("/home/alcaro/x/Glide64_Ini_utf8.c")).replace("‾","~").split("\n");
	for (int i=0;i<lines1.size();i++)
	{
		if (lines1[i] == lines2[i]) puts(lines1[i]);
		else
		{
			array<string> parts1 = lines1[i].split("\"");
			array<string> parts2 = lines2[i].split("\"");
			if (parts1.size() != 3 || parts2.size() != 3 || parts1[0]!=parts2[0] || parts1[2]!=parts2[2]) abort();
			string out = parts1[0] + "\"";
			
			for (int j=0;j<parts1[1].length();j++)
			{
				out += "\\x"+tostringhex<2>((uint8_t)parts1[1][j]);
			}
			out += "\"" + parts1[2] + " // " + parts2[1];
			
			puts(out);
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
