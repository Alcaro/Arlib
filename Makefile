PROGRAM = arlibtest
ARGUI = 0
ARTHREAD = 0
ARWUTF = 0
ARSOCKET = 1
#valid values: openssl, wolfssl, tlse, no
#ignored on windows (other than 'no', which is obeyed), always uses schannel
#default openssl
ARSOCKET_SSL = tlse
ARSANDBOX = 1

#honored variables, in addition to the ones listed here:
#OBJNAME
#  added to object file names, to allow building for multiple platforms without a make clean
#  better set on command line than in the makefile
#SOURCES
#  extra files to compile, in addition to *.cpp
#SOURCES_NOWARN
#  like SOURCES, but compiled with -w
#SOURCES_foo, CFLAGS_foo, DOMAINS
#  SOURCES_foo is compiled with CFLAGS_foo, in addition to the global ones
#  requires 'DOMAINS += foo'
#  (SOURCES and SOURCES_NOWARN are implemented as domains)

include arlib/Makefile

#TODO:
#./test (bash? python?)
#./configure (bash); possibly only verifies dependencies and lets makefile do the real job
#   or maybe makefile calls configure?
