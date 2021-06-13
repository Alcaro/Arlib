PROGRAM = bored
ARSOCKET = 1
ARTERMINAL = hybrid
ARWUTF = 1
ARXPSUPPORT = 32

include arlib/Makefile-head

ifeq ($(OS),linux)
  CONF_CFLAGS += $(shell pkg-config --cflags fuse)
  CONF_LFLAGS += $(shell pkg-config --libs fuse)
endif

CONF_CFLAGS += -Isodium-cfg/

CFLAGS_SODIUM += -DCONFIGURED=1 -DSODIUM_STATIC
# libsodium configures this stuff by ./configure, not by __x86_64__ (probably to better support old compilers)
# hardcoding works badly on anything other than x86, but it's hard to do anything better with the current makefile architecture
CFLAGS_SODIUM += -DHAVE_EMMINTRIN_H -DHAVE_TMMINTRIN_H -DHAVE_SMMINTRIN_H
# make libsodium stop trying to define its own in sse2_64_32.h, they optimize poorly
CFLAGS_SODIUM += -D_mm_set_epi64x=_mm_set_epi64x -D_mm_set1_epi64x=_mm_set1_epi64x -D_mm_cvtsi64_si128=_mm_cvtsi64_si128
# disable avx2 on 32bit, compile error due to missing #pragma GCC target("sse2") in sse2_64_32.h (I submitted a PR, but it's not merged yet)
ifneq ($(OS)$(WINBITS),windows32)
  CFLAGS_SODIUM += -DHAVE_AVX2INTRIN_H
endif
# _sodium_crit_init (sodium/core.c, _WIN32 only) is nonstatic for some unclear reason. Fixed upstream, but no stable release since then.
CFLAGS_SODIUM += -Wno-missing-declarations
CFLAGS_SODIUM += -Ilibsodium-1.0.18-stable/src/libsodium/include/sodium/
SOURCES_SODIUM  += libsodium-1.0.18-stable/src/libsodium/crypto_secretstream/xchacha20poly1305/secretstream_xchacha20poly1305.c
SOURCES_SODIUM  += libsodium-1.0.18-stable/src/libsodium/crypto_stream/chacha20/stream_chacha20.c
SOURCES_SODIUM  += libsodium-1.0.18-stable/src/libsodium/crypto_stream/chacha20/ref/chacha20_ref.c
SOURCES_SODIUM  += libsodium-1.0.18-stable/src/libsodium/crypto_stream/chacha20/dolbeau/chacha20_dolbeau-ssse3.c
SOURCES_SODIUM  += libsodium-1.0.18-stable/src/libsodium/crypto_stream/chacha20/dolbeau/chacha20_dolbeau-avx2.c
SOURCES_SODIUM  += libsodium-1.0.18-stable/src/libsodium/crypto_onetimeauth/poly1305/onetimeauth_poly1305.c
SOURCES_SODIUM  += libsodium-1.0.18-stable/src/libsodium/crypto_onetimeauth/poly1305/donna/poly1305_donna.c
SOURCES_SODIUM  += libsodium-1.0.18-stable/src/libsodium/crypto_verify/sodium/verify.c
SOURCES_SODIUM  += libsodium-1.0.18-stable/src/libsodium/crypto_core/hchacha20/core_hchacha20.c
SOURCES_SODIUM  += libsodium-1.0.18-stable/src/libsodium/randombytes/randombytes.c
SOURCES_SODIUM  += libsodium-1.0.18-stable/src/libsodium/sodium/utils.c
SOURCES_SODIUM  += libsodium-1.0.18-stable/src/libsodium/sodium/core.c
DOMAINS += SODIUM

*.cpp: | the_key.h
the_key.h:
	echo '// This is your BoredomFS password. Do not commit to Git or otherwise share it.' > the_key.h
	head -c32 /dev/urandom | xxd -i >> the_key.h

include arlib/Makefile
