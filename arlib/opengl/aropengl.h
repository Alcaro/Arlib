#pragma once
#include "../global.h"

#include <GL/gl.h>
#include <GL/glext.h>

#ifndef GLAPIENTRY
#ifdef _WIN32
#define GLAPIENTRY APIENTRY
#else
#define GLAPIENTRY
#endif
#endif

class aropengl : nocopy {
public:
	enum {
		t_ver_1_0 = 100, t_ver_1_1 = 110, t_ver_1_2 = 120, t_ver_1_3 = 130, t_ver_1_4 = 140, t_ver_1_5 = 150,
		t_ver_2_0 = 200, t_ver_2_1 = 210,
		t_ver_3_0 = 300, t_ver_3_1 = 310, t_ver_3_2 = 320, t_ver_3_3 = 330,
		t_ver_4_0 = 400, t_ver_4_1 = 410, t_ver_4_2 = 420, t_ver_4_3 = 430, t_ver_4_4 = 440, t_ver_4_5 = 450,
		
		t_opengl_es      = 0x1000,
		t_debug_context  = 0x2000, // This only requests a debug context, it doesn't actually enable any debugging. Use SetDefaultDebugCb.
		t_depth_buffer   = 0x4000,
		t_stencil_buffer = 0x8000, // These two only apply to the main buffer. You can foobar it.
	};
	
	class context : nocopy {
	public:
		//this is basically the common subset of WGL/GLX/etc
		//you want the outer class
		static context* create(uintptr_t window, uint32_t type);
		static context* create_x11(void* display, uintptr_t window, uint32_t type);
		
		virtual void makeCurrent(bool make) = 0; // If false, releases the context. The context is current on creation.
		virtual void swapInterval(int interval) = 0;
		virtual void swapBuffers() = 0;
		virtual funcptr getProcAddress(const char * proc) = 0;
		
		virtual ~context() {}
	};
	
	bool create(context* core);
	
	bool create(uintptr_t window, uint32_t type) { return create(context::create(window, type)); }
	bool create_x11(void* display, uintptr_t window, uint32_t type) { return create(context::create_x11(display, window, type)); }
	
	aropengl(context* core) { create(core); }
	aropengl(uintptr_t window, uint32_t type) { create(window, type); }
	
	explicit operator bool() { return core; }
	
	~aropengl() { delete core; }
	
	//Arlib usually uses underscores, but since OpenGL doesn't, this object follows suit.
	//Arlib-specific functions are named in lowercase, standard GL functions are not.
	
	//Named after provider-specific functions.
	void makeCurrent(bool make) { core->makeCurrent(make); } // If false, releases the context. The context is current on creation.
	void swapInterval(int interval) { core->swapInterval(interval); }
	void swapBuffers() { core->swapBuffers(); }
	funcptr getProcAddress(const char * proc) { return core->getProcAddress(proc); }
	
	//void (GLAPIENTRY * ClearColor)(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
	//void (GLAPIENTRY * Clear)(GLbitfield mask);
	//etc
#define AROPENGL_GEN_HEADER
#include "generated.c"
#undef AROPENGL_GEN_HEADER
	//It's intended that this object is named 'gl', resulting in gl.Clear(GL_etc), somewhat like WebGLRenderingContext.
	//It is not guaranteed that a non-NULL function will actually work, or even successfully return. Check gl.hasExtension.
	
	//These must borrow your GetInterface results.
	bool hasExtension(const char * ext);
	void enableDefaultDebugger(FILE* out = NULL); //Use only if the context was created with the debug flag.
	
private:
	context* core;
};
