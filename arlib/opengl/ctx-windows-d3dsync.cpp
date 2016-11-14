#ifdef _WIN32
#ifdef AROPENGL_D3DSYNC
#include "aropengl.h"

//https://www.opengl.org/registry/specs/NV/DX_interop.txt
//https://github.com/halogenica/WGL_NV_DX/blob/master/SharedResource.cpp
//https://msdn.microsoft.com/en-us/library/windows/desktop/bb174336(v=vs.85).aspx


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

#define interface struct
#include <D3D9.h>
#undef interface

#define bind bind_func

#ifndef D3DPRESENT_FORCEIMMEDIATE
#define D3DPRESENT_FORCEIMMEDIATE 0x00000100L
#endif
#ifndef D3DPRESENT_DONOTWAIT
#define D3DPRESENT_DONOTWAIT 0x00000001L
#endif

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

//SwapBuffers and various others are actually in gdi32.dll, which is used elsewhere and can safely be included here too


#define WGL_EXTS() \
	WGL_EXT(PFNWGLDXSETRESOURCESHAREHANDLENVPROC, DXSetResourceShareHandleNV) /* must be first */ \
	WGL_EXT(PFNWGLDXOPENDEVICENVPROC, DXOpenDeviceNV) \
	WGL_EXT(PFNWGLDXCLOSEDEVICENVPROC, DXCloseDeviceNV) \
	WGL_EXT(PFNWGLDXREGISTEROBJECTNVPROC, DXRegisterObjectNV) \
	WGL_EXT(PFNWGLDXUNREGISTEROBJECTNVPROC, DXUnregisterObjectNV) \
	WGL_EXT(PFNWGLDXOBJECTACCESSNVPROC, DXObjectAccessNV) \
	WGL_EXT(PFNWGLDXLOCKOBJECTSNVPROC, DXLockObjectsNV) \
	WGL_EXT(PFNWGLDXUNLOCKOBJECTSNVPROC, DXUnlockObjectsNV) \

struct {
#define WGL_SYM_N(str, ret, name, args) ret (WINAPI * name) args;
	WGL_SYMS()
#undef WGL_SYM_N
#define WGL_EXT(type, name) type name;
	WGL_EXTS()
#undef WGL_EXT
	HMODULE lib;
} static wgl;
#define WGL_SYM_N(str, ret, name, args) str "\0"
static const char wgl_proc_names[] = WGL_SYMS() ;
#undef WGL_SYM_N
#define WGL_EXT(type, name) "wgl" #name "\0"
static const char wgl_ext_names[] = WGL_EXTS() ;
#undef WGL_EXT

static aropengl gl;



typedef HRESULT (WINAPI * Direct3DCreate9Ex_t)(UINT SDKVersion, IDirect3D9Ex* * ppD3D);
static HMODULE hD3D9=NULL;
static Direct3DCreate9Ex_t lpDirect3DCreate9Ex;

static bool libLoadD3D()
{
	hD3D9=LoadLibrary("d3d9.dll");
	if (!hD3D9) return false;
	//lpDirect3DCreate9=Direct3DCreate9;//these are for verifying that Direct3DCreate9Ex_t matches the real function; they're not needed anymore
	//lpDirect3DCreate9Ex=Direct3DCreate9Ex;
	lpDirect3DCreate9Ex=(Direct3DCreate9Ex_t)GetProcAddress(hD3D9, "Direct3DCreate9Ex");
	if (!lpDirect3DCreate9Ex) { FreeLibrary(hD3D9); return false; }
	//if (!lpDirect3DCreate9Ex) return false;
	return true;
}

static void libReleaseD3D()
{
	FreeLibrary(hD3D9);
}

static bool InitGlobalGLFunctions()
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
	
	if (!libLoadD3D()) return false;
	
	return true;
}

static void DeinitGlobalGLFunctions()
{
	if (wgl.lib) FreeLibrary(wgl.lib);
	if (hD3D9) libReleaseD3D();
}

class aropengl_windows : public aropengl::context {
public:
	HWND D3D_hwnd;
	
	HWND GL_hwnd;
	HDC GL_hdc;
	HGLRC GL_hglrc;
	
	IDirect3DDevice9Ex* D3D_device;
	IDirect3DSurface9* D3D_backbuf;
	IDirect3DSurface9* D3D_GLtarget;
	
	HANDLE D3D_sharehandle;
	HANDLE D3D_sharetexture;
	HANDLE GL_htexture;
	
	GLuint GL_fboname;
	GLuint GL_texturename;
	
	bool vsync;
	
	/*private*/ bool init(HWND window, uint32_t version)
	{
		this->D3D_hwnd = window;
		
		this->GL_hwnd = CreateDummyWindow(window);
		this->GL_hdc = GetDC(this->GL_hwnd);
		this->GL_hglrc = NULL;
		
		if (!InitGlobalGLFunctions()) return false;
		if (!CreateContext(version)) return false;
		if (!CreateD3DContext()) return false;
		if (!JoinGLD3D()) return false;
		
		return true;
	}
	
	/*private*/ HWND CreateDummyWindow(HWND parent)
	{
		WNDCLASS wc = {};
		wc.lpfnWndProc = DefWindowProc;
		wc.lpszClassName = "arlib_opengl_dummy";
		RegisterClass(&wc);
		
		return CreateWindow("arlib_opengl_dummy", NULL, WS_CHILD, -1, 0, 1, 1, parent, NULL, NULL, NULL);
		//return CreateWindow("arlib_opengl_dummy", "OPENGL", WS_VISIBLE, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
	}
	
	/*private*/ bool CreateContext(uint32_t version)
	{
		if (wgl.GetCurrentContext()) return false;
		
		bool debug = (version & aropengl::t_debug_context);
		bool depthbuf = (version & aropengl::t_depth_buffer);
		bool stenbuf = (version & aropengl::t_stencil_buffer);
		version &= 0xFFF;
		
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
		SetPixelFormat(this->GL_hdc, ChoosePixelFormat(this->GL_hdc, &pfd), &pfd);
		this->GL_hglrc = wgl.CreateContext(this->GL_hdc);
		if (!this->GL_hglrc) return false;
		
		wgl.MakeCurrent(this->GL_hdc, this->GL_hglrc);
		
		if (version >= 310)
		{
			HGLRC hglrc_old = this->GL_hglrc;
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
			this->GL_hglrc = wglCreateContextAttribs(this->GL_hdc, /*share*/NULL, debug ? attribs_debug : attribs);
			wgl.DeleteContext(hglrc_old);
			
			if (!this->GL_hglrc)
			{
				wgl.MakeCurrent(NULL, NULL);
				return false;
			}
			
			wgl.MakeCurrent(this->GL_hdc, this->GL_hglrc);
		}
		
		const char * names = wgl_ext_names;
		FARPROC* functions = (FARPROC*)&wgl.DXSetResourceShareHandleNV;
		
		while (*names)
		{
			*functions = wgl.GetProcAddress(names);
			if (!*functions) return false;
			
			functions++;
			names += strlen(names)+1;
		}
		
		gl.create(this);
		
		return true;
	}
	
	/*private*/ bool CreateD3DContext()
	{
		IDirect3D9Ex* d3d;
		this->D3D_device = NULL;
		
		if (FAILED(lpDirect3DCreate9Ex(D3D_SDK_VERSION, &d3d))) return false;
		
		D3DPRESENT_PARAMETERS parameters = {};
		parameters.BackBufferCount = 2; // D3DPRESENT_FORCEIMMEDIATE|D3DPRESENT_DONOTWAIT doesn't work without this
		parameters.SwapEffect = D3DSWAPEFFECT_FLIPEX;
		parameters.hDeviceWindow = this->D3D_hwnd;
		parameters.Windowed = TRUE;
		//https://msdn.microsoft.com/en-us/library/windows/desktop/bb172585(v=vs.85).aspx
		//_ONE is _DEFAULT, but also calls timeBeginPeriod to improve precision
		//anything opting in to Direct3D vsync is clearly a high-performance program, and thus wants the increased precision
		parameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
		
		if (FAILED(d3d->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, this->D3D_hwnd,
		                               D3DCREATE_MIXED_VERTEXPROCESSING,
		                               &parameters, NULL, &this->D3D_device)))
		{
			return false;
		}
		
		d3d->Release();
		
		return true;
	}
	
	/*private*/ bool JoinGLD3D()
	{
		gl.GenFramebuffers(1, &GL_fboname);
		gl.BindFramebuffer(GL_FRAMEBUFFER, GL_fboname);
		
		gl.GenTextures(1, &GL_texturename);
		gl.BindTexture(GL_TEXTURE_2D, GL_texturename);
		
		gl.FramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_texturename, 0);
		
		D3D_sharehandle = wgl.DXOpenDeviceNV(this->D3D_device);
		D3D_sharetexture = NULL;
		
		this->D3D_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &this->D3D_backbuf);
		
		AllocRenderTarget();
		
		return true;
	}
	
	/*private*/ void AllocRenderTarget()
	{
		RECT wndsize;
		GetClientRect(this->D3D_hwnd, &wndsize);
		this->D3D_device->CreateRenderTarget(wndsize.right, wndsize.bottom, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, false, &this->D3D_GLtarget, &D3D_sharetexture);
		
		wgl.DXSetResourceShareHandleNV(this->D3D_GLtarget, D3D_sharetexture);
		this->GL_htexture = wgl.DXRegisterObjectNV(this->D3D_sharehandle, this->D3D_GLtarget, GL_texturename, GL_TEXTURE_2D, WGL_ACCESS_WRITE_DISCARD_NV);
		
		wgl.DXLockObjectsNV(D3D_sharehandle, 1, &this->GL_htexture);
	}
	
	/*private*/ void DeallocRenderTarget()
	{
		wgl.DXUnlockObjectsNV(D3D_sharehandle, 1, &this->GL_htexture);
		
		wgl.DXUnregisterObjectNV(D3D_sharehandle, this->GL_htexture);
		this->D3D_GLtarget->Release();
		CloseHandle(D3D_sharetexture);
		D3D_sharetexture = NULL;
	}
	
	
	
	void makeCurrent(bool make)
	{
		if (make) wgl.MakeCurrent(this->GL_hdc, this->GL_hglrc);
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
		vsync = (interval==1);
	}
	
	void swapBuffers()
	{
		//::SwapBuffers(this->GL_hdc);
		
		wgl.DXUnlockObjectsNV(D3D_sharehandle, 1, &this->GL_htexture);
		this->D3D_device->StretchRect(this->D3D_GLtarget, NULL, this->D3D_backbuf, NULL, D3DTEXF_NONE);
		this->D3D_device->PresentEx(NULL, NULL, NULL, NULL, (vsync ? 0 : D3DPRESENT_FORCEIMMEDIATE|D3DPRESENT_DONOTWAIT));
		wgl.DXLockObjectsNV(D3D_sharehandle, 1, &this->GL_htexture);
	}
	
	void notifyResize(GLsizei width, GLsizei height)
	{
		DeallocRenderTarget();
		AllocRenderTarget();
	}
	
	~aropengl_windows()
	{
		DeallocRenderTarget();
TerminateProcess(GetCurrentProcess(), -1); // TODO: do this right - that global GL struct causes the destructor to run twice
WaitForSingleObject(GetCurrentProcess(), INFINITE);

		if (wgl.MakeCurrent) wgl.MakeCurrent(NULL, NULL);
		if (this->GL_hglrc && wgl.DeleteContext) wgl.DeleteContext(this->GL_hglrc);
		if (this->GL_hdc) ReleaseDC(this->GL_hwnd, this->GL_hdc);
		
		if (this->D3D_device) this->D3D_device->Release();
		if (this->D3D_backbuf) this->D3D_backbuf->Release();
		
		DeinitGlobalGLFunctions();
	}
};

}

aropengl::context* aropengl::context::create(uintptr_t window, uint32_t version)
{
	aropengl_windows* ret = new aropengl_windows();
	if (ret->init((HWND)window, version)) return ret;
	
	delete ret;
	return NULL;
}
#endif
#endif
