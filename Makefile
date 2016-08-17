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

include arlib/Makefile

#TODO:
#./test (bash? python?)
#./configure (bash); possibly only verifies dependencies and lets makefile do the real job
#   or maybe makefile calls configure?
