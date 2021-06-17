#include "arlib.h"
#include "libsodium-1.0.18-stable/src/libsodium/include/sodium.h"

// override a few libsodium features that are easier to reimplement than cram into my makefiles

extern "C" {

int _sodium_runtime_get_cpu_features();
int _sodium_runtime_get_cpu_features() { return 0; }
int sodium_runtime_has_ssse3() { return runtime__SSSE3__; }
int sodium_runtime_has_avx2() { return runtime__AVX2__; }

// if these end up used, I'll get duplicate or missing symbol errors and can easily remove these stubs
int _crypto_pwhash_argon2_pick_best_implementation();
int _crypto_pwhash_argon2_pick_best_implementation() { return 0; }
int _crypto_generichash_blake2b_pick_best_implementation();
int _crypto_generichash_blake2b_pick_best_implementation() { return 0; }
int _crypto_scalarmult_curve25519_pick_best_implementation();
int _crypto_scalarmult_curve25519_pick_best_implementation() { return 0; }
int _crypto_stream_salsa20_pick_best_implementation();
int _crypto_stream_salsa20_pick_best_implementation() { return 0; }

struct randombytes_implementation randombytes_sysrandom_implementation = {
    /* implementation_name */ NULL,
    /* random */ NULL,
    /* stir */ NULL,
    /* uniform */ NULL,
    /* buf */ [](void* buf, size_t size) { if (size > 256) abort(); rand_secure(buf, size); },
    /* close */ NULL,
};

};
