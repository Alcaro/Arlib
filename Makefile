PROGRAM = test
ARGUI = 0
ARTHREAD = 0
ARWUTF = 0
ARSOCKET = 1
#valid values: openssl, wolfssl, tlse, no
#ignored on windows (other than 'no', which is obeyed), always uses schannel
#default openssl
ARSOCKET_SSL = tlse
ARSANDBOX = 1

include arlib/Makefile
