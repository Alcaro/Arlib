PROGRAM = test
ARGUI = 0
ARTHREAD = 0
ARWUTF = 0
ARSOCKET = 1
#defaults to openssl on linux
#ignored on windows, always uses schannel
#can also be set to 'no'
ARSOCKET_SSL = openssl
ARSANDBOX = 1

include arlib/Makefile
