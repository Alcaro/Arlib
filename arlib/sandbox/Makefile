ifneq ($(ARTHREAD),1)
  $(error the sandbox requires threads support)
endif

ifneq ($(OS),linux)
  $(error sandbox is only supported on Linux)
endif

SOURCES_ARLIB += arlib/sandbox/*.cpp
DEFINES += ARLIB_SANDBOX

#Enable ASLR and ensure we're usable as linker
PRELOADER_FLAGS := -fPIC -static -shared
#Runtime libraries aren't available, don't use them and don't use anything using them
PRELOADER_FLAGS += -nostdlib -nostartfiles -ffreestanding
#Pretty sure most of these are implied by -ffreestanding, but better safe than sorry
PRELOADER_FLAGS += -fno-stack-protector -fno-exceptions -fno-rtti -fno-plt -fno-jump-tables -fno-sync-libcalls
#Nothing should nor can override our symbols, and allowing it causes relocation issues
PRELOADER_FLAGS += -fvisibility=hidden
#AT&T assembly is ugly
PRELOADER_FLAGS += -masm=intel

ifneq ($(OPT),0)
  #Remove some wastes of space (custom linker script also shrinks stuff)
  PRELOADER_FLAGS += -fno-ident -Wl,--build-id=none
  #section GC doesn't help us
  PRELOADER_FLAGS += -fno-function-sections -fno-data-sections
  #Discard even more stuff I don't need
  PRELOADER_FLAGS += -Wl,-T,arlib/sandbox/linux-preload.T
  PRELOADER_FLAGS += -s
endif

#this could be in obj/, but if I put it there, Python is mandatory for building Arlib. I'd rather not.
arlib/sandbox/bpf.inc: arlib/sandbox/bpf.S arlib/sandbox/bpfasm.py
	python3 arlib/sandbox/bpfasm.py $< $@
$(call OBJMANGLE,ARLIB,arlib/sandbox/linux-sand.cpp): arlib/sandbox/linux-sand.cpp arlib/sandbox/bpf.inc obj/sand-preload.elf | obj

obj/sand-preload.elf: arlib/sandbox/linux-sand-preload.cpp | obj
	$(CXX) -D SANDBOX_INTERNAL $(TRUE_CXXFLAGS) $(PRELOADER_FLAGS) $< -o $@
$(call OBJMANGLE,ARLIB,arlib/sandbox/linux-sand-preload.cpp): arlib/sandbox/linux-sand-preload.cpp obj/sand-preload.elf | obj
