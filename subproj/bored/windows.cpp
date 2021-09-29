// BoredomFS - because I didn't have anything better to do than learn the FUSE API.
#ifdef _WIN32
#include "arlib.h"
#include "shared.h"

struct linux_filetime {
	uint32_t sec;
	uint32_t nsec;
};
static linux_filetime filetime_to_linux(FILETIME time)
{
	ULARGE_INTEGER tmp;
	tmp.LowPart = time.dwLowDateTime;
	tmp.HighPart = time.dwHighDateTime;
	
	linux_filetime ret;
#define WINDOWS_TICK 10000000
#define SEC_TO_UNIX_EPOCH 11644473600LL
	ret.sec = tmp.QuadPart / WINDOWS_TICK - SEC_TO_UNIX_EPOCH;
	ret.nsec = tmp.QuadPart % WINDOWS_TICK * (1000000000/WINDOWS_TICK);
	return ret;
}
static void filetime_to_linux(bytestreamw& out, FILETIME time)
{
	linux_filetime ltime = filetime_to_linux(time);
	out.u32l(ltime.sec);
	out.u32l(ltime.nsec);
}

static void write_stat(bytestreamw& out, bool ok, const char * fn, DWORD attr,
                       uint32_t fszlo, uint32_t fszhi, FILETIME time_access, FILETIME time_modify)
{
	uint8_t type;
	if (!ok) type = 0;
	else if (attr & FILE_ATTRIBUTE_DIRECTORY) type = 1;
	else if (strstr(fn, ".exe") && strstr(fn, ".exe")[4]=='\0') type = 3;
	else type = 2;
	out.u8(type);
	
	out.u32l(fszlo);
	out.u32l(fszhi);
	filetime_to_linux(out, time_access);
	filetime_to_linux(out, time_modify);
}

static void create_pipe_rdasync(HANDLE* rd, HANDLE* wr)
{
	static uint32_t uniq = 0;
	uint32_t start = GetCurrentProcessId()*65537; // Windows PIDs are usually in the low thousands
again:
	char name[32];
	sprintf(name, "\\\\.\\pipe\\arlib%.8X"
#ifdef ARTYPE_DLL
		// DLLs are always at a multiple of 64K, and current hardware only supports 48bit address, so middle 32 bits are known unique
		// worst case (on future hardware), it'll get a collision and just try again
		// (and it'll probably stick to top 17 bits equal unless explicitly requested otherwise, like Linux 5-level paging)
		"%.8X", (unsigned)(uintptr_t(&uniq) >> (sizeof(uintptr_t)==8 ? 16 : 0))
#endif
		, (unsigned)(start + lock_incr<lock_loose>(&uniq)));
	*rd = CreateNamedPipe(name, PIPE_ACCESS_INBOUND|FILE_FLAG_OVERLAPPED, 0, 1, 4096, 4096, 0, NULL);
	if (*rd == INVALID_HANDLE_VALUE) goto again;
	*wr = CreateFile(name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (*wr == INVALID_HANDLE_VALUE) { CloseHandle(*rd); goto again; }
	// some people recommend sending a nonce to ensure nobody else snuck in, but as long as nMaxInstances = 1,
	//  another thread or process using that filename means one of the above two will fail
	
	// things that don't work:
	// - blank name <https://stackoverflow.com/a/51448441> (not implemented in Wine, not in my gcc headers, needs ntdll)
	// - ReOpenFile to make the fd async (doesn't work on pipes)
	// - NtSetInformationFile (doesn't work on pipes, seems to operate on files rather than fds)
	// (Wine allows ReadFileEx on synchronous pipes, but detecting that would be more effort than it's worth)
}

class handler;
static bytearray reqexec_begin(handler& src, bytestream req);
class handler {
public:
	receiver recv;
	array<HANDLE> fds;
	
	bytearray handle(uint32_t type, bytestream req)
	{
		//printf("handling %u\n", (unsigned)type);
		
		bytestreamw ret;
		switch (type)
		{
		case REQ_STAT:
		{
			const char * fn = req.strnul_ptr()+1;
			if (!*fn) fn = ".";
			
			WIN32_FILE_ATTRIBUTE_DATA stat = {};
			bool ok = GetFileAttributesExA(fn, GetFileExInfoStandard, &stat);
			
			write_stat(ret, ok, fn, stat.dwFileAttributes, stat.nFileSizeLow, stat.nFileSizeHigh,
			           stat.ftLastAccessTime, stat.ftLastWriteTime);
		}
		break;
		case REQ_READDIR:
		{
			const char * fn = req.strnul_ptr()+1;
			if (!*fn) fn = ".";
			
			WIN32_FIND_DATA find;
			HANDLE h = FindFirstFile(cstring(fn)+"/*", &find);
			if (h == INVALID_HANDLE_VALUE)
			{
				if (GetLastError() == ERROR_FILE_NOT_FOUND) ret.u8(0); // special case empty dir... windows, why are you like this...
				break;
			}
			
			ret.u8(0);
			do {
				if (strcmp(find.cFileName, ".") == 0) continue;
				if (strcmp(find.cFileName, "..") == 0) continue;
				write_stat(ret, true, find.cFileName, find.dwFileAttributes, find.nFileSizeLow, find.nFileSizeHigh,
				           find.ftLastAccessTime, find.ftLastWriteTime);
				ret.strnul(find.cFileName);
			} while (FindNextFile(h, &find));
			FindClose(h);
		}
		break;
		
		case REQ_OPEN:
		{
			const char * fn = req.strnul_ptr()+1;
			if (!*fn) fn = ".";
			uint32_t my_flags = req.u32l();
			
			DWORD access = ((my_flags&8) ? (GENERIC_READ|FILE_APPEND_DATA) : (my_flags&3) ? (GENERIC_READ|GENERIC_WRITE) : GENERIC_READ);
			static const DWORD creations[] = { OPEN_EXISTING, OPEN_EXISTING, CREATE_NEW, OPEN_ALWAYS,
			                                   0xFFFFFFFF, TRUNCATE_EXISTING, CREATE_NEW, CREATE_ALWAYS };
			DWORD creation = creations[my_flags&7];
			HANDLE h = CreateFile(fn, access, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
			                      NULL, creations[my_flags&7], FILE_ATTRIBUTE_NORMAL, NULL);
			
			if (h == INVALID_HANDLE_VALUE) break;
			
			size_t n;
			for (n=0;n<fds.size();n++)
			{
				if (fds[n] == INVALID_HANDLE_VALUE) break;
			}
			
			ret.u32l(n);
			if (n == fds.size()) fds.append(h);
			else fds[n] = h;
		}
		break;
		case REQ_CLOSE:
		{
			uint32_t n = req.u32l();
			CloseHandle(fds[n]);
			fds[n] = INVALID_HANDLE_VALUE;
			
			size_t m = fds.size();
			while (m && fds[m-1] == INVALID_HANDLE_VALUE) m--;
			if (m != fds.size()) fds.resize(m);
		}
		break;
		case REQ_READ:
		{
			HANDLE h = fds[req.u32l()];
			LARGE_INTEGER pos;
			pos.QuadPart = req.u64l();
			if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) break;
			
			DWORD len = req.u32l();
			bytearray ret;
			ret.resize(len);
			if (!ReadFile(h, ret.ptr(), len, &len, NULL)) break;
			ret.resize(len);
			return ret;
		}
		break;
		case REQ_WRITE:
		{
			HANDLE h = fds[req.u32l()];
			LARGE_INTEGER pos;
			pos.QuadPart = req.u64l();
			if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) break;
			
			DWORD len = req.remaining();
			if (!WriteFile(h, req.bytes(len).ptr(), len, &len, NULL)) break;
			ret.u32l(len);
		}
		break;
		
		case REQ_RENAME:
		{
			const char * src = req.strnul_ptr()+1;
			const char * dst = req.strnul_ptr()+1;
			if (MoveFile(src, dst)) ret.u8(1);
		}
		break;
		case REQ_DELETE:
		{
			const char * fn = req.strnul_ptr()+1;
			if (DeleteFile(fn)) ret.u8(1);
		}
		break;
		case REQ_MKDIR:
		{
			const char * fn = req.strnul_ptr()+1;
			if (CreateDirectory(fn, NULL)) ret.u8(1);
		}
		break;
		case REQ_RMDIR:
		{
			const char * fn = req.strnul_ptr()+1;
			if (RemoveDirectory(fn)) ret.u8(1);
		}
		break;
		
		case REQ_PING:
		{
			if (req.remaining() < 4) break;
			
			bytearray dummy;
			dummy.resize(req.u32l());
			return dummy;
		}
		break;
		case REQ_EXEC:
		{
			return reqexec_begin(*this, req);
		}
		break;
		
		default: break;
		}
		return ret.finish();
	}
	
	void on_msg(bytearray by)
	{
		if (by.size() < 4) return;
//puts("-> "+tostringhex_dbg(by));
		bytearray ret = handle(readu_le32(by.ptr()), bytestream(by.skip(4)));
//puts("<- "+tostringhex_dbg(ret));
		
		if (recv.alive()) recv.send(ret);
	}
	
	handler(autoptr<socket> sock)
	{
		recv.init(std::move(sock), bind_this(&handler::on_msg), [this](){ this->recv.stop(); });
	}
	
	~handler()
	{
		for (HANDLE h : fds)
		{
			if (h != INVALID_HANDLE_VALUE)
				CloseHandle(h);
		}
	}
};
refarray<handler> handlers;

class exechandler {
public:
	receiver recv;
	
	HANDLE hproc;
	HANDLE stdin_wr;
	HANDLE stdout_rd;
	bool stdout_active;
	
	OVERLAPPED stdout_ov;
	uint8_t stdout_buf[1024];
	
	void alert_completion(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered)
	{
		stdout_active = false;
		if (!recv.alive()) return;
		
		if (dwNumberOfBytesTransfered != 0)
		{
			writeu_le32(stdout_buf, REQ_EXEC_STDOUT);
			recv.send(bytesr(stdout_buf, sizeof(uint32_t)+dwNumberOfBytesTransfered));
		}
		if (dwErrorCode == 0)
		{
			stdout_ov = {};
			ReadFileEx(stdout_rd, stdout_buf+sizeof(uint32_t), sizeof(stdout_buf)-sizeof(uint32_t), &stdout_ov, alert_completion);
			stdout_active = true;
		}
		if (dwErrorCode != 0)
		{
			CloseHandle(stdout_rd);
			stdout_rd = NULL;
			
			TerminateProcess(hproc, 0); // so it doesn't lock up if the process closed stdout without terminating
			WaitForSingleObject(hproc, INFINITE);
			DWORD exc;
			GetExitCodeProcess(hproc, &exc);
			
			uint8_t tmp[sizeof(uint32_t)*2];
			writeu_le32(tmp, REQ_EXEC_EXIT);
			writeu_le32(tmp+sizeof(uint32_t), exc);
			recv.send(tmp);
			sock_err();
		}
	}
	
	static void WINAPI alert_completion(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped)
	{
		container_of<&exechandler::stdout_ov>(lpOverlapped)->alert_completion(dwErrorCode, dwNumberOfBytesTransfered);
	}
	
	void sock_msg(bytearray bytes)
	{
		switch (readu_le32(bytes.ptr()))
		{
		case REQ_EXEC_STDIN:
		{
			DWORD dummy;
			WriteFile(stdin_wr, bytes.ptr()+4, bytes.size()-4, &dummy, NULL);
		}
		break;
		case REQ_EXEC_STDIN_CLOSE:
		{
			CloseHandle(stdin_wr);
			stdin_wr = NULL;
		}
		break;
		case REQ_EXEC_RELEASE:
		{
			CloseHandle(hproc);
			hproc = NULL;
			sock_err();
		}
		break;
		}
	}
	
	void sock_err()
	{
		recv.stop();
		if (hproc) TerminateProcess(hproc, 0);
		if (stdout_rd) CloseHandle(stdout_rd);
		stdout_rd = NULL;
	};
	
	~exechandler()
	{
		if (hproc) TerminateProcess(hproc, 0);
		if (hproc) CloseHandle(hproc);
		if (stdin_wr) CloseHandle(stdin_wr);
		if (stdout_rd) CloseHandle(stdout_rd);
	}
};
refarray<exechandler> exechandlers;
static bytearray reqexec_begin(handler& src, bytestream req)
{
	// the only way I can find to determine if a process is CLI or GUI is SHGetFileInfoW, which requires COM for whatever reason
	// GetBinaryType exists, but doesn't differentiate CLI from GUI
	// let's just pretend everything is CLI
	
	string cwd = "C:"+req.strnul();
	
	string exe = req.strnul().replace("/","\\"); // executing /windows/system32/cmd.exe is interpreted as 'md .exe' aka mkdir
	string commandline = "\""+exe+"\"";
	while (req.remaining())
	{
		cstring arg = req.strnul();
		if (arg.contains(" ")) commandline += " \""+arg+"\"";
		else commandline += " "+arg;
	}
	
	HANDLE stdin_rd;
	HANDLE stdin_wr;
	HANDLE stdout_rd;
	HANDLE stdout_wr;
	
	CreatePipe(&stdin_rd, &stdin_wr, NULL, 0);
	create_pipe_rdasync(&stdout_rd, &stdout_wr);
	
	SetHandleInformation(stdin_rd, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
	SetHandleInformation(stdout_wr, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
	
	PROCESS_INFORMATION pi = {};
	STARTUPINFO si = {};
	si.cb = sizeof(STARTUPINFO);
	si.hStdError = stdout_wr;
	si.hStdOutput = stdout_wr;
	si.hStdInput = stdin_rd;
	si.dwFlags |= STARTF_USESTDHANDLES;
	
	if (!CreateProcess(exe, (char*)commandline.bytes().ptr(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi)) return {};
	
	CloseHandle(pi.hThread);
	CloseHandle(stdout_wr);
	CloseHandle(stdin_rd);
	
	exechandler& hdlr = exechandlers.append();
	hdlr.recv.consume(src.recv);
	hdlr.recv.callback(bind_ptr(&exechandler::sock_msg, &hdlr), bind_ptr(&exechandler::sock_err, &hdlr));
	hdlr.hproc = pi.hProcess;
	hdlr.stdin_wr = stdin_wr;
	hdlr.stdout_rd = stdout_rd;
	hdlr.alert_completion(0, 0);
	
	uint8_t ok[] = { 1 };
	hdlr.recv.send(ok);
	
	return NULL;
}

int main(int argc, char** argv)
{
	if (sodium_init() < 0) abort();
	
	WuTF_enable();
	SetCurrentDirectory("C:\\");
	
again:
	socketlisten* listen = socketlisten::create(3339, runloop::global(),
		[](autoptr<socket> sock) {
			for (size_t n=0;n<handlers.size();n++)
			{
				if (!handlers[n].recv.alive()) handlers.remove(n--);
			}
			for (size_t n=0;n<exechandlers.size();n++)
			{
				if (!exechandlers[n].recv.alive() && !exechandlers[n].stdout_active) exechandlers.remove(n--);
			}
			handlers.append(std::move(sock));
puts("now have "+tostring(handlers.size())+"+"+tostring(exechandlers.size())+" sockets");
		});
	if (!listen)
	{
		puts("listen failed");
		Sleep(1000);
		goto again;
	}
	puts("ready");
	runloop::global()->enter();
	return 0;
}
#endif
