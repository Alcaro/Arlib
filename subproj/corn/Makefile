PROGRAM = corn
ARGUI = 1

CONF_CFLAGS += $(shell pkg-config --cflags gstreamer-1.0)
CONF_LFLAGS += -lX11 $(shell pkg-config --libs gstreamer-1.0)

#apt install libavdevice-dev depends on all other ffmpeg libs
CONF_CFLAGS += $(shell pkg-config --cflags libavdevice libavformat libavfilter libavcodec libswresample libswscale libavutil)
CONF_LFLAGS += $(shell pkg-config --libs libavdevice libavformat libavfilter libavcodec libswresample libswscale libavutil)
#https://github.com/leandromoreira/ffmpeg-libav-tutorial

include arlib/Makefile
