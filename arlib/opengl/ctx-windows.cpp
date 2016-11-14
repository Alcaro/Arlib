#ifdef _WIN32g
#include "aropengl.h"

#undef bind
#ifdef _MSC_VER
//MSVC's gl.h doesn't seem to include the stuff it should. Copying these five lines from mingw's gl.h...
# if !(defined(WINGDIAPI) && defined(APIENTRY))
#  include <windows.h>
# else
#  include <stddef.h>
# endif
//Also disable a block of code that defines int32_t to something not identical to my msvc-compatible stdint.h.
# define GLEXT_64_TYPES_DEFINED
#endif

#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/wglext.h>

#define bind bind_func

namespace {

//not autogenerating this since we know what this module wants
#define WGL_SYM(ret, name, args) WGL_SYM_N("wgl"#name, ret, name, args)
#define WGL_SYM_ANON(ret, name, args) WGL_SYM_N(#name, ret, name, args)
#define WGL_SYMS() \
	WGL_SYM(HGLRC, CreateContext, (HDC hdc)) \
	WGL_SYM(BOOL, DeleteContext, (HGLRC hglrc)) \
	WGL_SYM(HGLRC, GetCurrentContext, ()) \
	WGL_SYM(PROC, GetProcAddress, (LPCSTR lpszProc)) \
	WGL_SYM(BOOL, MakeCurrent, (HDC hdc, HGLRC hglrc)) \
	/*WGL_SYM(BOOL, SwapBuffers, (HDC hdc)) - it's in gdi32.dll, which is linked already */ \
	
//WINGDIAPI BOOL  WINAPI wglCopyContext(HGLRC, HGLRC, UINT);
//WINGDIAPI HGLRC WINAPI wglCreateContext(HDC);
//WINGDIAPI HGLRC WINAPI wglCreateLayerContext(HDC, int);
//WINGDIAPI BOOL  WINAPI wglDeleteContext(HGLRC);
//WINGDIAPI HGLRC WINAPI wglGetCurrentContext(VOID);
//WINGDIAPI HDC   WINAPI wglGetCurrentDC(VOID);
//WINGDIAPI PROC  WINAPI wglGetProcAddress(LPCSTR);
//WINGDIAPI BOOL  WINAPI wglMakeCurrent(HDC, HGLRC);
//WINGDIAPI BOOL  WINAPI wglShareLists(HGLRC, HGLRC);
//WINGDIAPI BOOL  WINAPI wglUseFontBitmapsA(HDC, DWORD, DWORD, DWORD);
//WINGDIAPI BOOL  WINAPI wglUseFontBitmapsW(HDC, DWORD, DWORD, DWORD);
//#ifdef UNICODE
//#define wglUseFontBitmaps  wglUseFontBitmapsW
//#else
//#define wglUseFontBitmaps  wglUseFontBitmapsA
//#endif // !UNICODE
//WINGDIAPI BOOL  WINAPI SwapBuffers(HDC);

struct {
#define WGL_SYM_N(str, ret, name, args) ret (WINAPI * name) args;
	WGL_SYMS()
#undef WGL_SYM_N
	PFNWGLSWAPINTERVALEXTPROC SwapInterval;
	HMODULE lib;
} static wgl;
#define WGL_SYM_N(str, ret, name, args) str "\0"
const char wgl_proc_names[] = WGL_SYMS() ;
#undef WGL_SYM_N

bool InitGlobalGLFunctions()
{
	//this can yield multiple unsynchronized writers to global variables
	//however, this is safe, because they all write the same values in the same order
	//(except if writing that cache line also discards other changes to the same cache line, but that just won't happen.)
	wgl.lib = LoadLibrary("opengl32.dll");
	if (!wgl.lib) return false;
	
	//HMODULE gdilib=GetModuleHandle("gdi32.dll");
	
	const char * names = wgl_proc_names;
	FARPROC* functions = (FARPROC*)&wgl;
	
	while (*names)
	{
		*functions = GetProcAddress(wgl.lib, names);
		if (!*functions) return false;
		
		functions++;
		names += strlen(names)+1;
	}
	
	return true;
}

void DeinitGlobalGLFunctions()
{
	if (wgl.lib) FreeLibrary(wgl.lib);
}

class aropengl_windows : public aropengl::context {
public:
	HWND hwnd;
	HDC hdc;
	HGLRC hglrc;
	
	/*private*/ bool init(HWND window, uint32_t flags)
	{
		this->hwnd = window;
		this->hdc = GetDC(this->hwnd);
		this->hglrc = NULL;
		
		if (!InitGlobalGLFunctions()) return false;
		if (!CreateContext(flags)) return false;
		
		return true;
	}
	
	/*private*/ bool CreateContext(uint32_t flags)
	{
		if (wgl.GetCurrentContext()) return false;
		
		bool debug = (flags & aropengl::t_debug_context);
		bool depthbuf = (flags & aropengl::t_depth_buffer);
		bool stenbuf = (flags & aropengl::t_stencil_buffer);
		bool d3dsync = (flags & aropengl::t_direct3d_vsync);
		if (d3dsync)
		{
#ifdef AROPENGL_D3DSYNC
			
#else
			return false;
#endif
		}
		uint32_t version = (flags & 0xFFF);
		
		PIXELFORMATDESCRIPTOR pfd;
		memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
		pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
		pfd.nVersion = 1;
		pfd.dwFlags = PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
		pfd.iPixelType = PFD_TYPE_RGBA;
		pfd.cColorBits = 24;
		pfd.cAlphaBits = 0;
		pfd.cAccumBits = 0;
		pfd.cDepthBits = (stenbuf ? 24 : depthbuf ? 16 : 0);
		pfd.cStencilBits = (stenbuf ? 8 : 0);
		pfd.cAuxBuffers = 0;
		pfd.iLayerType = PFD_MAIN_PLANE;
		SetPixelFormat(this->hdc, ChoosePixelFormat(this->hdc, &pfd), &pfd);
		this->hglrc = wgl.CreateContext(this->hdc);
		if (!this->hglrc) return false;
		
		wgl.MakeCurrent(this->hdc, this->hglrc);
		
		if (version >= 310)
		{
			HGLRC hglrc_old = this->hglrc;
			PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribs =
				(PFNWGLCREATECONTEXTATTRIBSARBPROC)wgl.GetProcAddress("wglCreateContextAttribsARB");
			const int attribs[] = {
				WGL_CONTEXT_MAJOR_VERSION_ARB, (int)version/100,
				WGL_CONTEXT_MINOR_VERSION_ARB, (int)version/10%10,
				//WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
				//https://www.opengl.org/wiki/Core_And_Compatibility_in_Contexts says do not use
				0 };
			const int attribs_debug[] = {
				WGL_CONTEXT_MAJOR_VERSION_ARB, (int)version/100,
				WGL_CONTEXT_MINOR_VERSION_ARB, (int)version/10%10,
				WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
				0 };
			this->hglrc = wglCreateContextAttribs(this->hdc, /*share*/NULL, debug ? attribs_debug : attribs);
			wgl.DeleteContext(hglrc_old);
			
			if (!this->hglrc)
			{
				wgl.MakeCurrent(NULL, NULL);
				return false;
			}
			
			wgl.MakeCurrent(this->hdc, this->hglrc);
		}
		
		wgl.SwapInterval = (PFNWGLSWAPINTERVALEXTPROC)this->getProcAddress("wglSwapIntervalEXT");
		
		return true;
	}
	
	void makeCurrent(bool make)
	{
		if (make) wgl.MakeCurrent(this->hdc, this->hglrc);
		else wgl.MakeCurrent(NULL, NULL);
	}
	
	funcptr getProcAddress(const char * proc)
	{
		PROC ret = wgl.GetProcAddress(proc);
		if (!ret) ret = ::GetProcAddress(wgl.lib, proc); // lol windows
		return (funcptr)ret;
	}
	
	void swapInterval(int interval)
	{
		wgl.SwapInterval(interval);
	}
	
	void swapBuffers()
	{
		::SwapBuffers(this->hdc);
	}
	
	~aropengl_windows()
	{
		if (wgl.MakeCurrent) wgl.MakeCurrent(NULL, NULL);
		if (this->hglrc && wgl.DeleteContext) wgl.DeleteContext(this->hglrc);
		if (this->hdc) ReleaseDC(this->hwnd, this->hdc);
		DeinitGlobalGLFunctions();
	}
};

}

aropengl::context* aropengl::context::create(uintptr_t window, uint32_t flags)
{
	aropengl_windows* ret = new aropengl_windows();
	if (ret->init((HWND)window, flags)) return ret;
	
	delete ret;
	return NULL;
}
#endif
