PROGRAM = corn
ARGUI = 1

CONF_CFLAGS += $(shell pkg-config --cflags gstreamer-1.0)
CONF_LFLAGS += -lX11 $(shell pkg-config --libs gstreamer-1.0)

include arlib/Makefile
