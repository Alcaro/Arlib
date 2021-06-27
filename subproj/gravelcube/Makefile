# The sandbox requires namespaces. If unprivileged user namespaces are enabled (like on Ubuntu's default config), nothing else is needed;
#  if they are not (like on Debian's default config), an arlib-sandbox-setuid binary is also provided. Make it setuid root,
#  and put it beside the Gravelcube binary.

include arlib/Makefile-head

PROGRAM = gvc
ARTERMINAL = 1
DEFINES += ARLIB_OVERRIDEABLE_PROCESS

include sandbox/Makefile
include arlib/Makefile
