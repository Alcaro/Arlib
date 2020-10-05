#include "arlib.h"
//#include <fcntl.h>
//#include <unistd.h>
//#include <sys/wait.h>
//#include <sys/ptrace.h>

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

/*
#ifndef ARLIB_TEST
__attribute__((constructor)) static void x1() { puts("ctor"); }
__attribute__((destructor))  static void x2() { puts("dtor"); }
#endif

extern uint32_t __cpu_model[4];

static const char const_a[] = "hello";
static const char * const_b = const_a;
static const char * const * const_c = &const_b;

#ifndef _WIN32
static void GetProcAddress() {}
#endif

DLLEXPORT void* x();
void* x() { return (void*)const_a; }

DLLEXPORT void y();
void y()
{
	//uint32_t g[4] = {__cpu_model[0],__cpu_model[1],__cpu_model[2],__cpu_model[3]};
	uint32_t* b = __cpu_model;
	auto* c = GetProcAddress;
	auto* d = const_a;
	auto* e = const_b;
	auto* f = const_c;
//#ifndef ARLIB_OPT
	arlib_hybrid_dll_init();
//#else
	//puts("main, no init");
//#endif
	printf("y() %p\n", y);
	//printf("%x,%x,%x,%x,",g[0],g[1],g[2],g[3]);
	printf("%p %p %p %p %p\n",c,b,d,e,f);
	//printf("%x,%x,%x,%x,",__cpu_model[0],__cpu_model[1],__cpu_model[2],__cpu_model[3]);
	printf("%p %p %p %p %p\n",GetProcAddress,__cpu_model,const_a,const_b,const_c);
	printf("%x,%x,%x,%x\n",__cpu_model[0],__cpu_model[1],__cpu_model[2],__cpu_model[3]);
	puts(file::exepath()+"");
}
*/

int main(int argc, char** argv)
{
	puts(tostring(0x123u));
	exit(0);

/*

#ifdef ARLIB_SANDBOX
	argparse args;
	array<string> child;
	args.add("", &child);
	arlib_init(args, argv);
#else
	arlib_init(NULL, argv);
#endif
	
	puts("Begin");
	{
		dylib d;
#ifdef _WIN32
		printf("init=%d ", d.init("test.dll"));
		printf("%p\n", *(void**)&d);
#else
		file::writeall("obj/fnep.so", file::readall("arlibtest"));
		printf("init=%d\n", d.init("obj/fnep.so"));
#endif
		
		void*(*d_x)() = (void*(*)())d.sym_func("x");
		printf("p=%p\n",x());
		printf("sym=%p\n", d_x);
		if (d_x) printf("p=%p\n",d_x());
		
		void(*d_y)() = d.sym_func("y");
		y();
		printf("sym=%p\n", d_y);
		if (d_y) d_y();
		
		puts("Unload");
	}
	puts("End");
	exit(0);
	
	//runloop* loop = runloop::global();
	//HTTP h(loop);
	//HTTP::rsp r;
	//function<void(HTTP::rsp)> break_runloop = bind_lambda([&](HTTP::rsp inner_r) { r = std::move(inner_r); loop->exit(); });
	//h.send(HTTP::req("https://floating.muncher.se/"), break_runloop);
	//loop->enter();
	//puts(r.text()+"");
	
	//puts(tostring(3.141592653589793238462));
	
	//e("jpg/black.jpg");
	//e("jpg/white.jpg");
	//e("jpg/whiteleft-blackright.jpg");
	//e("jpg/whitetop-blackbot.jpg");
	//e("jpg/whitetopleft-blackbotright.jpg");
	//e("jpg/whitetopleft-blackbotright-wide.jpg");
	//e("jpg/whitetopleft-blackbotright-tall.jpg");
	//e("jpg/whitetopleft-blackbotright-big.jpg");
	//e("jpg/whitetopleft-blackbotright-loop.jpg");
	
	/*
	if (ptrace(PTRACE_ATTACH, 13954, NULL, NULL) == -1){
		printf("Error attaching to process.\n");
		return EXIT_FAILURE;
	}
	waitpid(13954, NULL, 0);
	
	FILE* maps_fd = fopen("/proc/13954/maps", "r");
	int mem_fd = open("/proc/13954/mem", O_RDONLY);
	
	while (!feof(maps_fd))
	{
		char line[1024];
		fgets(line, 1024, maps_fd);
		puts(line);
		
		off64_t start, end;
		unsigned long int inode, foo;
		char mapname[PATH_MAX], perm[5], dev[6];
		
		sscanf(line, "%lx-%lx %4s %lx %5s %ld %s", &start, &end, perm, &foo, dev, &inode, mapname);
		printf("%lx %lx\n", start, end);
		if (start > end)
			continue;
		array<byte> out;
		out.resize(end - start);
		pread(mem_fd, out.ptr(), end-start, start);
		
		write(2,out.ptr(),end-start);
	}
	ptrace(PTRACE_DETACH, 13954, NULL, NULL);
	*/
	
	/*
	array<byte> n = file::readall("a.bin");
	
	uint8_t* start = n.ptr();
	uint8_t* end = start + n.size();
	while (true)
	{
		//SWF header: ascii 'FWS', (u8)version, (u32)filelen, (??)framesize, (u16)framerate, (u16)framecount
		uint8_t* swf = (uint8_t*)memmem(start, end-start, "FWS", 3);
		if (!swf) break;
		start = swf+1;
		
		bytestream bs = arrayview<byte>(swf, end-swf);
		bs.skip(3);
		bs.u8(); // version
		size_t swflen = bs.u32l();
		
		// //*
		// uint8_t bits_per_field = bs.u8();
		// bits_per_field = (uint8_t)bits_per_field >> 3;
		// uint8_t total_bits = 5 + bits_per_field * 4;
		// total_bits = ((total_bits) % 8) ? (total_bits / 8 + 1) : total_bits / 8;
		// bs.skip(total_bits - 1); //we started by reading 1 byte to determine the size of the RECT
		// /* /
		// bs.skip((5 + (bs.u8() >> 3 << 2) + 7) / 8 + 1);
		// //* /
		uint16_t framerate = bs.u16b();
		uint16_t framecount = bs.u16l();
		
		while(true){
			uint16_t tag = bs.u16l();
			uint16_t tag_type = tag >> 6;
			uint32_t tag_length = tag & 0x3F;
			
			if(tag_length >= 63){
				tag_length = (int32_t)bs.u32l();
			}
			
			bs.skip(tag_length);
			
			//Perfect place to scan all tag types and extract fonts and pictures
			if(tag_type == 0 || bs.tell() >= swflen){
				break;
			}
		}
		printf("%lx %lx %u %u %lx %lx %d\n", swf-n.ptr(), swflen, framerate, framecount, bs.tell(), swflen, bs.tell() == swflen);
		if (bs.tell() == swflen)
		{
			file::writeall(tostringhex((unsigned)(swf-n.ptr()))+".swf", arrayview<byte>(swf, swflen));
			start = swf + swflen;
		}
		
		//size_t swflen = readu_le32(swf+
	}
	*/
	
#ifdef ARLIB_SANDBOX
	sandproc ch(runloop::global());
	ch.set_stdout(process::output::create_stdout());
	ch.set_stderr(process::output::create_stderr());
	ch.fs_grant_syslibs(child[0]);
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
	ch.fs_hide("/usr/");
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
	
	ch.onexit([&](int lstatus) { runloop::global()->exit(); });
	
	ch.launch(child[0], child.skip(1));
	runloop::global()->enter();
#endif
	return 0;
}
