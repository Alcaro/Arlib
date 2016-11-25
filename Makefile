PROGRAM = arlibtest
ARGUI = 1
AROPENGL = 1
ARTHREAD = 0
ARWUTF = 0
ARSOCKET = 1
#valid values: openssl, wolfssl, tlse, no
#ignored on windows (other than 'no', which is obeyed), always uses schannel
#default openssl
ARSOCKET_SSL = openssl
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
#./test (bash? python?)
#./configure (bash); possibly only verifies dependencies and lets makefile do the real job
#   or maybe makefile calls configure?
