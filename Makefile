PROGRAM = arlibtest
ARGUI = 1
AROPENGL = 1
ARTHREAD = 0
ARWUTF = 0
ARSOCKET = 1
#valid values: openssl, gnutls, tlse, no
#ignored on windows (other than 'no', which is obeyed), always uses schannel
#default openssl
ARSOCKET_SSL = gnutls
ARSANDBOX = 1

#honored variables, in addition to the ones listed here:
#OPT, DEBUG, PROFILE
#  OPT=1 enables heavy optimizations; DEBUG=0 removes debug flags; PROFILE=gen/opt are for PGO
#CFLAGS, LFLAGS, CC, CXX, LD (CLI)
#  override compiler choice and flags
#CONF_CFLAGS, CONF_LFLAGS
#  additional compiler/linker flags needed by this program
#EXCEPTIONS
#  set to 1 if needed
#SOURCES
#  extra files to compile, in addition to *.cpp
#  supports .c and .cpp
#SOURCES_NOWARN
#  like SOURCES, but compiled with warnings disabled
#  should only be used for third-party code that can't be fixed
#SOURCES_FOO, CFLAGS_FOO, DOMAINS
#  SOURCES_FOO is compiled with CFLAGS_FOO, in addition to the global ones
#  requires 'DOMAINS += FOO'
#  (SOURCES and SOURCES_NOWARN are implemented as domains)
#OBJNAME (CLI)
#  added to object file names, to allow building for multiple platforms without a make clean
#the ones listed (CLI) should not be set by the program, but should instead be reserved for command-line arguments

include arlib/Makefile

#TODO:
#./configure; possibly only verifies dependencies and lets makefile do the real job
#  or maybe makefile calls configure?
#./test (bash? python?)
#  make test calls some python script, which calls the makefile with new arguments? the current setup is fairly stupid
#    maybe the make test script is in arlib/, not ./
#test all SSLs at once, rename socketssl_impl to socketssl_tlse and ifdef socketssl::create
#  maybe always compile all SSLs, rely on --gc-sections to wipe the unused ones
