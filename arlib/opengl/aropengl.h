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
		
		t_opengl_es      = 0x001000, // Probably not supported.
		t_debug_context  = 0x002000, // Requests a debug context. Doesn't actually enable debugging, use gl.enableDefaultDebugger or gl.DebugMessageControl/etc.
		t_depth_buffer   = 0x004000, // These two only apply to the main buffer. You can always create additional FBOs with or without depth/stencil.
		t_stencil_buffer = 0x008000,
#ifdef AROPENGL_D3DSYNC
		//Direct3D vsync is an advanced feature that uses WGL_NV_DX_interop and D3DSWAPEFFECT_FLIPEX to ensure smooth framerate on Windows.
		//Advantages:
		//- Less stuttering, especially with DWM enabled
		//Disadvantages:
		//- Requires Windows 7 or newer
		//- Some graphics cards and drivers are not compatible
		//- Poorly tested driver path, may be slow or buggy
		//- You may not render to the default framebuffer, 0; you must render to gl.defaultFramebuffer()
		//    (if you don't use framebuffers, you can ignore this; defaultFrameBuffer is bound on creation)
		//- You must call gl.notifyResize() whenever the window is resized (whether by the application or the user), in addition to gl.Viewport/etc
		//- Swap intervals other than 0 and 1 are not supported. Not even -1.
		//- May be slower; with vsync off, clearing a 640x480 screen each frame drops from 3500 on pure OpenGL to 2750 with Direct3D vsync
		//The flag is ignored on non-Windows systems.
		//It is safe to use gl.defaultFramebuffer and gl.notifyResize on non-d3dsync objects.
		t_direct3d_vsync = 0x010000,
#endif
	};
	
	class context : nocopy {
	public:
		//this is basically the common subset of WGL/GLX/etc
		//you want the outer class, as it offers proper extension management
		static context* create(uintptr_t window, uint32_t flags);
		
		static context* create_x11(void* display, uintptr_t window, uint32_t flags);
		
		virtual void makeCurrent(bool make) = 0; // If false, releases the context. The context is current on creation.
		virtual void swapInterval(int interval) = 0;
		virtual void swapBuffers() = 0;
		virtual funcptr getProcAddress(const char * proc) = 0;
#ifdef AROPENGL_D3DSYNC
		virtual
#endif
		void notifyResize(GLsizei width, GLsizei height) {}
		
#ifdef AROPENGL_D3DSYNC
		virtual
#endif
		GLuint outputFramebuffer() { return 0; }
		
		virtual ~context() {}
	};
	
	bool create(context* core);
	
	bool create(uintptr_t window, uint32_t flags) { return create(context::create(window, flags)); }
	//Allows specifying which X11 connection the window is for.
	//You're probably happier with the default, this one exists only if you don't want a dependency on the rest of Arlib.
	bool create_x11(void* display, uintptr_t window, uint32_t flags) { return create(context::create_x11(display, window, flags)); }
	
	aropengl() { core=NULL; }
	aropengl(context* core) { create(core); }
	aropengl(uintptr_t window, uint32_t flags) { create(window, flags); }
	
	explicit operator bool() { return core!=NULL; }
	
	~aropengl() { delete core; }
	
	//Arlib usually uses underscores, but since OpenGL doesn't, this object follows suit.
	//To ensure no collisions, Arlib-specific functions start with a lowercase (or are C++-only, like operator bool), standard GL functions are uppercase.
	
	//If false, releases the context. The context is current on creation.
	void makeCurrent(bool make) { core->makeCurrent(make); }
	void swapInterval(int interval) { core->swapInterval(interval); }
	void swapBuffers() { core->swapBuffers(); }
	funcptr getProcAddress(const char * proc) { return core->getProcAddress(proc); }
	
	//If the window is resized, use this function to report the new size.
	void notifyResize(GLsizei width, GLsizei height) { core->notifyResize(width, height); }
	//Used only for Direct3D sync. If you're not using that, just use 0.
	GLuint outputFramebuffer() { return core->outputFramebuffer(); }
	
	//void (GLAPIENTRY * ClearColor)(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
	//void (GLAPIENTRY * Clear)(GLbitfield mask);
	//etc
#define AROPENGL_GEN_HEADER
#include "generated.c"
#undef AROPENGL_GEN_HEADER
	//It's intended that this object is named 'gl', resulting in gl.Clear(GL_etc), somewhat like WebGLRenderingContext.
	//It is not guaranteed that a non-NULL function will actually work, or even successfully return. Check gl.hasExtension.
	
	bool hasExtension(const char * ext);
	void enableDefaultDebugger(FILE* out = NULL); //Use only if the context was created with the debug flag.
	
private:
	context* core;
};
