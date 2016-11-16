#ifdef ARGUIPROT_X11
#include "aropengl.h"

//TODO: wipe -lGL dependency
//TODO: fix SwapInterval
//TODO: wipe printfs

#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <dlfcn.h>

namespace {

#define GLX_SYM(ret, name, args) GLX_SYM_N("glX"#name, ret, name, args)
#define GLX_SYM_OPT(ret, name, args) GLX_SYM_N_OPT("glX"#name, ret, name, args)
#define GLX_SYM_ARB(ret, name, args) GLX_SYM_N("glX"#name"ARB", ret, name, args)
#define GLX_SYM_ARB_OPT(ret, name, args) GLX_SYM_N_OPT("glX"#name"ARB", ret, name, args)
#define GLX_SYMS() \
	/* GLX 1.0 */ \
	GLX_SYM(funcptr, GetProcAddress, (const GLubyte * procName)) \
	GLX_SYM(void, SwapBuffers, (Display* dpy, GLXDrawable drawable)) \
	GLX_SYM(Bool, MakeCurrent, (Display* dpy, GLXDrawable drawable, GLXContext ctx)) \
	GLX_SYM(Bool, QueryVersion, (Display* dpy, int* major, int* minor)) \
	GLX_SYM(XVisualInfo*, ChooseVisual, (Display* dpy, int screen, int * attribList)) \
	GLX_SYM(GLXContext, CreateContext, (Display* dpy, XVisualInfo* vis, GLXContext shareList, Bool direct)) \
	GLX_SYM(GLXContext, GetCurrentContext, ()) \
	/* GLX 1.3 */ \
	GLX_SYM(GLXFBConfig*, ChooseFBConfig, (Display* dpy, int screen, const int * attrib_list, int * nelements)) \
	GLX_SYM(XVisualInfo*, GetVisualFromFBConfig, (Display* dpy, GLXFBConfig config)) \
	GLX_SYM(GLXWindow, CreateWindow, (Display* dpy, GLXFBConfig config, Window win, const int * attrib_list)) \
	GLX_SYM(GLXContext, CreateNewContext, (Display* dpy, GLXFBConfig config, int render_type, GLXContext share_list, Bool direct)) \
	GLX_SYM(void, DestroyWindow, (Display* dpy, GLXWindow win)) \
	\
	GLX_SYM(void, SwapIntervalEXT, (Display* dpy, GLXDrawable drawable, int interval)) \

#define GLX_SYM_N_OPT GLX_SYM_N
#define GLX_SYM_N(str, ret, name, args) ret (*name) args;
struct { GLX_SYMS() void* lib; } static glx;
#undef GLX_SYM_N
#define GLX_SYM_N(str, ret, name, args) str,
const char * const glx_names[]={ GLX_SYMS() };
#undef GLX_SYM_N
#undef GLX_SYM_N_OPT

#define GLX_SYM_N(str, ret, name, args) 0,
#define GLX_SYM_N_OPT(str, ret, name, args) 1,
const uint8_t glx_opts[]={ GLX_SYMS() };
#undef GLX_SYM_N
#undef GLX_SYM_N_OPT

bool InitGlobalGLFunctions()
{
	glx.lib=dlopen("libGL.so", RTLD_LAZY);
	if (!glx.lib) return false;
	
	funcptr* functions=(funcptr*)&glx;
	for (unsigned int i=0;i<sizeof(glx_names)/sizeof(*glx_names);i++)
	{
		functions[i]=(funcptr)dlsym(glx.lib, glx_names[i]);
		if (!glx_opts[i] && !functions[i]) return false;
	}
	return true;
}

void DeinitGlobalGLFunctions()
{
	if (glx.lib) dlclose(glx.lib);
}



class aropengl_x11 : public aropengl::context {
public:
		Display *display;
  Colormap cmap;
  GLXContext ctx;
  Window win;
	
	
	
	/*private*/ bool init(Window parent, Window* window_, uint32_t flags)
	{
		//if (glx.GetCurrentContext()) return false;
		//
		//bool debug = (flags & aropengl::t_debug_context);
		//bool depthbuf = (flags & aropengl::t_depth_buffer);
		//bool stenbuf = (flags & aropengl::t_stencil_buffer);
		//uint32_t version = (flags & 0xFFF);
		
		display = window_x11.display;

  // Get a matching FB config
  static int visual_attribs[] =
    {
      GLX_X_RENDERABLE    , True,
      GLX_DRAWABLE_TYPE   , GLX_WINDOW_BIT,
      GLX_RENDER_TYPE     , GLX_RGBA_BIT,
      GLX_X_VISUAL_TYPE   , GLX_TRUE_COLOR,
      GLX_RED_SIZE        , 8,
      GLX_GREEN_SIZE      , 8,
      GLX_BLUE_SIZE       , 8,
      GLX_ALPHA_SIZE      , 8,
      GLX_DEPTH_SIZE      , 24,
      GLX_STENCIL_SIZE    , 8,
      GLX_DOUBLEBUFFER    , True,
      //GLX_SAMPLE_BUFFERS  , 1,
      //GLX_SAMPLES         , 4,
      None
    };

  int glx_major, glx_minor;
 
  // FBConfigs were added in GLX version 1.3.
  if ( !glXQueryVersion( display, &glx_major, &glx_minor ) || 
       ( ( glx_major == 1 ) && ( glx_minor < 3 ) ) || ( glx_major < 1 ) )
  {
    return false;
  }

  printf( "Getting matching framebuffer configs\n" );
  int fbcount;
  GLXFBConfig* fbc = glXChooseFBConfig(display, DefaultScreen(display), visual_attribs, &fbcount);
  if (!fbc)
  {
    printf( "Failed to retrieve a framebuffer config\n" );
    exit(1);
  }
  printf( "Found %d matching FB configs.\n", fbcount );

  // Pick the FB config/visual with the most samples per pixel
  printf( "Getting XVisualInfos\n" );
  int best_fbc = -1, worst_fbc = -1, best_num_samp = -1, worst_num_samp = 999;

  int i;
  for (i=0; i<fbcount; ++i)
  {
    XVisualInfo *vi = glXGetVisualFromFBConfig( display, fbc[i] );
    if ( vi )
    {
      int samp_buf, samples;
      glXGetFBConfigAttrib( display, fbc[i], GLX_SAMPLE_BUFFERS, &samp_buf );
      glXGetFBConfigAttrib( display, fbc[i], GLX_SAMPLES       , &samples  );
      
      printf( "  Matching fbconfig %d, visual ID 0x%2x: SAMPLE_BUFFERS = %d,"
              " SAMPLES = %d\n", 
              i, vi -> visualid, samp_buf, samples );

      if ( best_fbc < 0 || samp_buf && samples > best_num_samp )
        best_fbc = i, best_num_samp = samples;
      if ( worst_fbc < 0 || !samp_buf || samples < worst_num_samp )
        worst_fbc = i, worst_num_samp = samples;
    }
    XFree( vi );
  }

  GLXFBConfig bestFbc = fbc[ best_fbc ];

  // Be sure to free the FBConfig list allocated by glXChooseFBConfig()
  XFree( fbc );

  // Get a visual
  XVisualInfo *vi = glXGetVisualFromFBConfig( display, bestFbc );
  printf( "Chosen visual ID = 0x%lx\n", vi->visualid );

  printf( "Creating colormap\n" );
  XSetWindowAttributes swa;
  swa.colormap = cmap = XCreateColormap( display,
                                         parent, 
                                         vi->visual, AllocNone );
  swa.background_pixmap = None ;
  swa.border_pixel      = 0;
  swa.event_mask        = StructureNotifyMask;

  printf( "Creating window\n" );
  win = XCreateWindow( display, parent, 
                              0, 0, 100, 100, 0, vi->depth, InputOutput, 
                              vi->visual, 
                              CWBorderPixel|CWColormap|CWEventMask, &swa );
  if ( !win )
  {
    printf( "Failed to create window.\n" );
    return false;
  }
  *window_ = win;

  // Done with the visual info data
  XFree( vi );

  XStoreName( display, win, "GL 3.0 Window" );

  printf( "Mapping window %lu\n" ,win);
  XMapWindow( display, win );

  // NOTE: It is not necessary to create or make current to a context before
  // calling glXGetProcAddressARB
  PFNGLXCREATECONTEXTATTRIBSARBPROC glXCreateContextAttribsARB = 0;
  glXCreateContextAttribsARB = (PFNGLXCREATECONTEXTATTRIBSARBPROC)
           glXGetProcAddressARB( (const GLubyte *) "glXCreateContextAttribsARB" );


  // Install an X error handler so the application won't exit if GL 3.0
  // context allocation fails.
  //
  // Note this error handler is global.  All display connections in all threads
  // of a process use the same error handler, so be sure to guard against other
  // threads issuing X commands while this code is running.

  // Check for the GLX_ARB_create_context extension string and the function.
  // If either is not present, use GLX 1.3 context creation method.
  if (!glXCreateContextAttribsARB)
  {
    return false;
  }

  // If it does, try to get a GL 3.0 context!
  else
  {
    int context_attribs[] =
      {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 0,
        //GLX_CONTEXT_FLAGS_ARB        , GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
        None
      };

    printf( "Creating context\n" );
    ctx = glXCreateContextAttribsARB( display, bestFbc, 0,
                                      True, context_attribs );

    // Sync to ensure any errors generated are processed.
    XSync( display, False );
    if ( !ctx ) return false;
  }

  // Sync to ensure any errors generated are processed.
  XSync( display, False );

  if (!ctx )
  {
    printf( "Failed to create an OpenGL context\n" );
    exit(1);
  }
makeCurrent(true);
	}
	
	
	
	void makeCurrent(bool make)
	{
		if (make) glXMakeCurrent( display, win, ctx );
		else glXMakeCurrent( display, None, NULL );
	}
	
	funcptr getProcAddress(const char * proc)
	{
		return (funcptr)glXGetProcAddress((GLubyte*)proc);
	}
	
	void swapInterval(int interval)
	{
		
	}
	
	void swapBuffers()
	{
		glXSwapBuffers ( display, win );
	}
	
	void destroy()
	{
  glXMakeCurrent( display, 0, 0 );
  glXDestroyContext( display, ctx );

  XDestroyWindow( display, win );
  XFreeColormap( display, cmap );
	}
	
	~aropengl_x11() { destroy(); }
};

}

aropengl::context* aropengl::context::create(uintptr_t parent, uintptr_t* window, uint32_t flags)
{
	aropengl_x11* ret = new aropengl_x11();
	if (ret->init((Window)parent, (Window*)window, flags)) return ret;
	
	delete ret;
	return NULL;
}
#endif
