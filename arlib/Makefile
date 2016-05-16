all: $(PROGRAM)_dummy

ifeq ($(OS),Windows_NT)
  OS = windows
  #$(error objdump something and check which sections can be nuked)
else
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Linux)
    OS = linux
  endif
  ifeq ($(UNAME_S),Darwin)
    OS = osx
  endif
endif

ARGUI ?= 0
ARTHREADS ?= 0
ARWUTF ?= 0
ARSOCKET ?= 0

DEBUG ?= 1

CC = gcc
CFLAGS ?=
CXX = g++
CXXFLAGS ?= $(CFLAGS)
LD = g++
LFLAGS ?=
OBJSUFFIX ?=

EXESUFFIX =
EXTRAOBJ =
CONF_CXXFLAGS += $(CONF_CFLAGS)

ifeq ($(OS),linux)
  CONF_LFLAGS += -ldl
  ifeq ($(ARTHREADS),1)
    CONF_CFLAGS += -pthread
    CONF_LFLAGS += -pthread
  endif
  OBJSUFFIX = -linux
endif

## function rwildcard(directory, pattern)
## mostly stolen from bsnes, but slightly improved (can use . as a directory)
#rwildcard = \
#  $(strip \
#    $(warning 1 1 $1 : 2 $2 : c $(if $(strip $1),$1,.)) \
#    $(filter $(if $2,$2,%), \
#      $(foreach f, \
#        $(wildcard $(if $(strip $1),$1,.)/*), \
#        $(eval t = $(call rwildcard,$f)) \
#        $(warning 2 t $t : f $f : 1 $1 : 2 $2) \
#        $(if $t,$t,$f) \
#      ) \
#    ) \
#  )

ifeq ($(OS),windows)
  EXESUFFIX = .exe
#  EXTRAOBJ = obj/resource$(OBJSUFFIX).o
#  RC = windres
#  RCFLAGS =
#obj/resource$(OBJSUFFIX).o: ico/*
#	$(RC) $(RCFLAGS) ico/minir.rc obj/resource$(OBJSUFFIX).o
  OBJSUFFIX = -windows
endif

OPTFLAGS := -Os -fomit-frame-pointer -fmerge-all-constants -fvisibility=hidden
OPTFLAGS += -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables
OPTFLAGS += -ffunction-sections -fdata-sections
OPTFLAGS += -Werror

ifeq ($(OPT),1)
  CFLAGS += $(OPTFLAGS)
  LFLAGS += -Wl,--gc-sections -s
  DEBUG = 0
endif
ifeq ($(DEBUG),1)
  CFLAGS += -g
endif

ifeq ($(PROFILE),gen)
  CONF_CFLAGS += -fprofile-generate
  CONF_LFLAGS += -lgcov
endif
ifeq ($(PROFILE),use)
  CONF_CFLAGS += -fprofile-use -fprofile-correction
endif

OUTNAME = $(PROGRAM)$(EXESUFFIX)

#stolen from http://stackoverflow.com/questions/22586084/makefile-with-multiple-rules-sharing-same-recipe-with-patternrules
define ADDDIR_CORE
OBJS += $(patsubst arlib/$(1)/%.cpp,obj/_arlib_$(1)_%$(OBJSUFFIX).o,$(wildcard arlib/$(1)/*.cpp))
obj/_arlib_$(1)_%$(OBJSUFFIX).o: arlib/$(1)/%.cpp | obj
	$$(CXX) $$(TRUE_CXXFLAGS) -c $$< -o $$@
endef

define ADDDIR
$(eval $(call ADDDIR_CORE,$(1)))
endef

OBJS := $(patsubst %.cpp,obj/%$(OBJSUFFIX).o,$(wildcard *.cpp)) $(EXTRAOBJ)
# obj/miniz$(OBJSUFFIX).o

$(call ADDDIR,.)

ifeq ($(ARGUI),1)
  $(call ADDDIR,gui)
  ifeq ($(OS),windows)
    CONF_CFLAGS += -DARGUI_WINDOWS
    CONF_LFLAGS += -lgdi32 -lcomctl32 -lcomdlg32 -ldinput8 -ldxguid -lopengl32
  endif
  ifeq ($(OS),linux)
    CONF_CFLAGS += $(shell pkg-config --cflags gtk+-3.0) -DARGUI_GTK3 -DARGUIPROT_X11
    CONF_LFLAGS += -ldl -lX11 -lGL -lXi -lXext $(shell pkg-config --libs gtk+-3.0)
  endif
else
  CONF_CFLAGS += -DARGUI_NONE
endif

ifeq ($(ARTHREADS),1)
  $(call ADDDIR,threads)
  CONF_CFLAGS += -DARLIB_THREADS
  ifeq ($(OS),linux)
    CONF_CFLAGS += -pthread
    CONF_LFLAGS += -pthread
  endif
endif

ifeq ($(ARWUTF),1)
  $(call ADDDIR,wutf)
  CONF_CFLAGS += -DARLIB_WUTF
endif

ifeq ($(ARSOCKET),1)
  $(call ADDDIR,socket)
  CONF_CFLAGS += -DARLIB_SOCKET
endif

CCXXFLAGS = -fvisibility=hidden -fno-exceptions -Wall -Wno-comment
TRUE_CFLAGS = -std=c99 $(CCXXFLAGS) $(CFLAGS) $(CONF_CFLAGS)
TRUE_CXXFLAGS =-std=c++11 -fno-rtti $(CCXXFLAGS) $(CXXFLAGS) $(CONF_CXXFLAGS)
TRUE_LFLAGS = $(LFLAGS) -fvisibility=hidden $(CONF_LFLAGS)

#double gcc bug combo:
#(1) GCC hates this pattern:
#//define foo(a,b,c) \
#//  bar(a) \
#//  bar(b) \
#//  bar(c)
#(2) https://gcc.gnu.org/bugzilla/show_bug.cgi?id=53431 says '#pragma GCC diagnostic ignored "-Wcomment"' does nothing
TRUE_CFLAGS += -Wno-comment
TRUE_CXXFLAGS += -Wno-comment

#On Windows, cleaning up the object directory is expected to be done with 'del /q obj\*' in a batch script.
clean:
	rm obj/* || true

clean-prof:
	rm obj/*.o || true

obj:
	mkdir obj

obj/miniz$(OBJSUFFIX).o: miniz.c | obj
	$(CC) $(TRUE_CFLAGS) -c $< -o $@

obj/%$(OBJSUFFIX).o: %.cpp | obj
	$(CXX) $(TRUE_CXXFLAGS) -c $< -o $@

$(OUTNAME): $(OBJS)
	$(LD) $+ $(TRUE_LFLAGS) -o $@ -lm

$(PROGRAM)_dummy: $(OUTNAME)
