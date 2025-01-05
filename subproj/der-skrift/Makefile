PROGRAM = der-skrift
ARSOCKET = 1

ifneq ($(DS_IN),)
  SOURCES := $(DS_IN)
  CFLAGS_ARLIB = $(OPTFLAGS) $(OPTFLAGS_CXX)
  CONF_LFLAGS += $(OPTLFLAGS)
  OBJNAME += ds
  PROGRAM := $(DS_OUT)
  DEFINES += DER_SKRIPT
  CONF_CFLAGS += -Wno-missing-declarations
  #CONF_CFLAGS += -iquote $(DS_SRCDIR)
  $(eval $(DS_XFLAGS))
else
  CFLAGS_DEFAULT += -DBUILD_DIR="$(dir $(abspath $(firstword $(MAKEFILE_LIST))))"
endif

include arlib/Makefile
