PROGRAM = der-skrift
#valid values: exe (executable), dll (shared library), hybrid (usable as both exe and dll, named as if it's an exe)
ARTYPE = exe
ARGUI = 0
AROPENGL = 0
ARTHREAD = 0
ARWUTF = 0
ARSOCKET = 1
#valid values: openssl (default), gnutls, bearssl, no
ARSOCKET_SSL = openssl
#valid values: schannel (default), bearssl, no (others may work, not tested)
ARSOCKET_SSL_WINDOWS = schannel
ARSANDBOX = 0

ifneq ($(DS_IN),)
  SOURCES := $(DS_IN)
  CFLAGS_ARLIB = $(OPTFLAGS) $(OPTFLAGS_CXX)
  CONF_LFLAGS += $(OPTLFLAGS)
  OBJNAME += ds
  PROGRAM := $(DS_OUT)
  DEFINES += DER_SKRIPT
  CONF_CFLAGS += -Wno-missing-declarations
  CONF_CFLAGS += -iquote $(DS_SRCDIR)
else
  CFLAGS_DEFAULT += -DBUILD_DIR="$(dir $(abspath $(firstword $(MAKEFILE_LIST))))"
endif

include arlib/Makefile
