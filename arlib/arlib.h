//TODO:
//- cool down on string.h refcounting
//- remove all mallocs, except containers.h and maybe some other system-level code
//- figure out if intarray.h is used; if no, remove
//- specialize array<uint8_t> and similar for primitive types
//- remove maybe, or find a better implementation
//- reimplement files
//- remove every non-reference pointer from public APIs
//   everything guilty, except gui/, is handled
//- replace variadic widget constructors with templates
//- socket::recv should take array<uint8_t>* as out parameter, errors/length are in return
//- add some mandatory define in makefile; if not present, enable all features, for MSVC compat

//WARNING: Arlib comes with zero stability guarantees. It can and will change in arbitrary ways, for any reason and at any time.

#pragma once
#include "bml.h"
#include "containers.h"
#include "endian.h"
#include "file.h"
#include "function.h"
#include "intwrap.h"
#include "os.h"
#include "serialize.h"
#include "string.h"
#include "stringconv.h"
#include "test.h"

//not in #ifdef, it contains some dummy implementations if threads are disabled
#include "thread/thread.h"

#if !defined(ARGUI_NONE) && !defined(ARGUI_WINDOWS) && !defined(ARGUI_GTK3)
#define ARGUI_NONE
#endif
#ifndef ARGUI_NONE
#include "gui/window.h"
#endif

#ifdef ARLIB_OPENGL
#include "opengl/aropengl.h"
#endif

#ifdef ARLIB_WUTF
#include "wutf/wutf.h"
#endif

#ifdef ARLIB_SANDBOX
#include "sandbox/sandbox.h"
#endif

#ifdef ARLIB_SOCKET
#include "socket/socket.h"
#endif
