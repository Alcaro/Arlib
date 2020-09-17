PROGRAM = corn
#valid values: exe (executable), dll (shared library), hybrid (usable as both exe and dll, named as if it's an exe)
ARTYPE = exe
ARGUI = 1
AROPENGL = 0
ARTHREAD = 0
ARWUTF = 0
ARSOCKET = 0
#valid values: openssl (default), gnutls, bearssl, no
ARSOCKET_SSL = openssl
#valid values: schannel (default), bearssl, no (others may work, not tested)
ARSOCKET_SSL_WINDOWS = schannel
ARSANDBOX = 0

CONF_CFLAGS += $(shell pkg-config --cflags gstreamer-1.0)
CONF_LFLAGS += -lX11 $(shell pkg-config --libs gstreamer-1.0)

include arlib/Makefile
