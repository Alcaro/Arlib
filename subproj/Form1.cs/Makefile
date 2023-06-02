include arlib/Makefile-head

PROGRAM = Form1.cs
ARTYPE = exe
# this is the 'enable glib runloop support' flag, I need a better name for it...
ARGUI = 1
ARSOCKET = 1

CONF_CFLAGS += $(shell pkg-config --cflags gio-2.0)
CONF_LFLAGS += $(shell pkg-config --libs gio-2.0)
CONF_LFLAGS += -lssh2

include arlib/Makefile
