PROGRAM = test
ARGUI = 0
ARTHREAD = 0
ARWUTF = 0
ARSOCKET = 1
ARSOCKET_SSL = wolfssl
#defaults to openssl on linux
#ignored on windows, always uses schannel
#can also be set to 'no'
ARSANDBOX = 0

include arlib/Makefile
