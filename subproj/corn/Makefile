PROGRAM = corn
ARGUI = 1

#apt install libavdevice-dev depends on all other ffmpeg libs, though only a few of them are necessary
CONF_CFLAGS += $(shell pkg-config --cflags libavformat libavcodec libavutil)
CONF_LFLAGS += $(shell pkg-config --libs libavformat libavcodec libavutil)

CONF_CFLAGS += $(shell pkg-config --cflags libpulse-mainloop-glib)
CONF_LFLAGS += $(shell pkg-config --libs libpulse-mainloop-glib)

include arlib/Makefile
