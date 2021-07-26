ifneq ($(OS),linux)
  $(error sandbox is only supported on Linux)
endif

SOURCES += sandbox/*.cpp

SETUID_CFLAGS := 
SETUID_LFLAGS := 

#ensure we're usable as linker, with ASLR enabled
PRELOADER_CFLAGS += -fpic
PRELOADER_LFLAGS += -static -shared -Wl,-Bsymbolic -Wl,--no-dynamic-linker

#runtime libraries aren't available, don't use them and don't use anything using them
PRELOADER_CFLAGS += -ffreestanding
PRELOADER_LFLAGS += -nostdlib -nostartfiles -nodefaultlibs
PRELOADER_CFLAGS += -fno-builtin-memcpy -fno-builtin-memset -fno-builtin-memmove
SETUID_CFLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables
#nothing should nor can override our symbols, and allowing it causes relocation issues
SETUID_CFLAGS += -fvisibility=hidden
#gcc sure loves inserting calls to random stuff, force it not to
#I'd prefer if I could enable -fstack-protector, but the stack cookie is at %fs:40, which is a segfault
#I could set it up, but that'd require a bunch of assembly, leak memory, and confuse the real libc when it tries to set up %fs
#glibc seems built without stack protector anyways, and the low amount of stack used by this thing is easy to audit by hand
PRELOADER_CFLAGS += -fno-stack-protector -fno-exceptions -fno-rtti -fno-jump-tables
ifeq ($(CC_FAMILY),gcc)
  PRELOADER_CFLAGS += -fno-sync-libcalls
endif

#AT&T assembly is ugly
#yes, there's only ~7 lines of asm in this thing, AT&T is still ugly
PRELOADER_CFLAGS += -masm=intel
ifeq ($(CC_FAMILY),clang)
#TODO: reenable integrated-as once Clang's integrated assembler supports mov %eax,42 (rather than just mov eax,42)
#as of Clang 6.0.0, it does not
#(Clang will also need a __asm__(".intel_syntax") early in the C++ - under an ifdef, gcc doesn't like that)
PRELOADER_CFLAGS += -fno-integrated-as
endif

#I could include TRUE_{C,L}FLAGS, but that includes silly stuff like -pthread and -lgtk-3, so just hardcode them
#CFLAGS/LFLAGS are set by command line only, empty by default, let's include them
PRELOADER_CFLAGS += -std=c++17 $(CFLAGS)
PRELOADER_CFLAGS += -Wall -Wmissing-declarations -Wno-comment
SETUID_LFLAGS += -Wl,-z,now $(LFLAGS)
PRELOADER_CFLAGS += $(patsubst %,-D%,$(DEFINES))

ifneq ($(OPT),0)
  SETUID_CFLAGS += $(OPTFLAGS)
  SETUID_LFLAGS += $(OPTLFLAGS)
endif
ifeq ($(DEBUG),1)
  #not sure if gdb can find the sandbox preloader's debug symbols, or do anything useful inside the child at all
  #(strace works fine, but doesn't care about symbols anyways)
  #but gdb can run the preloader without the sandbox, so keep debug anyways
  PRELOADER_CFLAGS += -g
endif

sandbox/bpf.inc: sandbox/bpf.S sandbox/bpfasm.py
	python3 sandbox/bpfasm.py $< $@
$(call OBJMANGLE,DEFAULT,sandbox/launch-linux-sand.cpp): sandbox/launch-linux-sand.cpp sandbox/bpf.inc | obj


PRELOADER_SOURCES := sandbox/preload-linux-sand.cpp sandbox/sysemu-linux-sand.cpp
PRELOADER_OBJS := $(call OBJMANGLE,ARSANDPL,$(PRELOADER_SOURCES))
obj/ARSANDPL___%.cpp.o: $$(call SOURCENAME,$$@) sandbox/internal-linux-sand.h
	$(ECHOQ) CXX $<
	$(Q)$(CXX) -D SANDBOX_PRELOAD $(PRELOADER_CFLAGS) $(SETUID_CFLAGS) -c $< -o $@

obj/sand-preload-$(OBJNAME).elf: $(PRELOADER_OBJS) | obj
	$(ECHOQ) LD $@
	$(Q)$(CXX) $(PRELOADER_LFLAGS) $(SETUID_LFLAGS) $(PRELOADER_OBJS) -o $@
#ensure failure if this thing isn't 100% static
#requires .DELETE_ON_ERROR, which is set in Arlib's main makefile
	$(Q)objdump -R $@ 2>&1 | grep -A500 -E 'DYNAMIC RELOCATION RECORDS$$' || true
	$(Q)objdump -R $@ 2>&1 | grep -qF 'DYNAMIC RELOCATION RECORDS (none)' || \
	    objdump -R $@ 2>&1 | grep -qF 'not a dynamic object'
$(call OBJMANGLE,DEFAULT,sandbox/preload-linux-sand.cpp): obj/sand-preload-$(OBJNAME).elf


SETUID_SOURCES := sandbox/lockdown-linux-sand.cpp
SETUID_OBJS := $(call OBJMANGLE,ARSANDSU,$(SETUID_SOURCES))
obj/ARSANDSU___%.cpp.o: $$(call SOURCENAME,$$@) | obj
	$(ECHOQ) CXX $<
	$(Q)$(CXX) -D SANDBOX_SETUID $(SETUID_CFLAGS) -c $< -o $@

arlib-sandbox-setuid: $(SETUID_OBJS) | obj
	$(ECHOQ) LD $@
	$(Q)$(CXX) $(SETUID_LFLAGS) $(SETUID_OBJS) -o $@
	chown root:root $@ && chmod +s $@ || true

arlib-default: arlib-sandbox-setuid
