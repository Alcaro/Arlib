#ifdef ARLIB_SSL_BEARSSL
#ifdef __cplusplus
extern "C" {
#endif

#include "../deps/bearssl-0.3/inc/bearssl.h"

//I need to use these functions, which aren't part of the public API. (Thanks for not making x509's init static.)
//It's about nine million kinds of unsafe, but I have no other choice, other than changing upstream code...
extern void br_ssl_hs_client_init_main(void* ctx);
extern void br_x509_minimal_init_main(void* ctx);

#ifdef __cplusplus
}
#endif


//This file exports the following four functions:

//After this, 'sc' may only be used as the 'frozen' parameter to br_ssl_client_unfreeze.
void br_ssl_client_freeze(br_ssl_client_context* sc);

//'target' must be initialized in the same way as the original 'sc' prior to calling this.
//This includes, but is not limited to, the following functions (if used originally):
//- br_ssl_client_init_full
//- br_ssl_engine_set_x509
//- br_ssl_engine_set_buffer
//- br_ssl_client_set_client_certificate
//- br_ssl_client_set_single_ec
//- br_ssl_client_set_single_rsa
//- br_ssl_client_set_rsapub
//This includes the list of trust anchors.
//Do not call br_ssl_client_reset.
//'frozen' will be invalidated, and 'target' will become operational.
//Can not be used to upgrade BearSSL versions, not even tiny patches.
void br_ssl_client_unfreeze(br_ssl_client_context* frozen, br_ssl_client_context* target);

//'reference' must be whatever SSL engine this x509 is used by. May be frozen or unfrozen.
void br_x509_minimal_freeze(br_x509_minimal_context* sc, br_ssl_engine_context* reference);

//'reference' must be functional.
void br_x509_minimal_unfreeze(br_x509_minimal_context* frozen, br_x509_minimal_context* target, br_ssl_engine_context* reference);




static const uint8_t * br_t0_extract(void(*fn)(void* t0ctx))
{
	uint32_t rp[32]; // the init functions use rp
	struct {
		uint32_t * dp;
		uint32_t * rp;
		const uint8_t * ip;
	} cpu = { NULL, rp, NULL };
	fn(&cpu);
	return cpu.ip;
}

static void br_ssl_engine_freeze(br_ssl_engine_context* cc, const uint8_t * t0_init)
{
	if (cc->icbc_in    && cc->in.vtable == &cc->icbc_in   ->inner) cc->in.vtable = (br_sslrec_in_class*)1;
	if (cc->igcm_in    && cc->in.vtable == &cc->igcm_in   ->inner) cc->in.vtable = (br_sslrec_in_class*)2;
	if (cc->ichapol_in && cc->in.vtable == &cc->ichapol_in->inner) cc->in.vtable = (br_sslrec_in_class*)3;
	if (cc->icbc_out    && cc->out.vtable == &cc->icbc_out   ->inner) cc->out.vtable = (br_sslrec_out_class*)1;
	if (cc->igcm_out    && cc->out.vtable == &cc->igcm_out   ->inner) cc->out.vtable = (br_sslrec_out_class*)2;
	if (cc->ichapol_out && cc->out.vtable == &cc->ichapol_out->inner) cc->out.vtable = (br_sslrec_out_class*)3;
	
	cc->cpu.dp -= ((uintptr_t)cc->dp_stack)/4; // /4 because these are u32 pointers
	cc->cpu.rp -= ((uintptr_t)cc->rp_stack)/4;
	cc->cpu.ip -= (uintptr_t)t0_init; // I can't find the buffer start, only a fixed point in it, so it'll probably overflow, but that's fine
	
	if (cc->hbuf_in )       cc->hbuf_in        -= (uintptr_t)cc->ibuf - 1; // -1 to make sure hbuf_in==ibuf doesn't change hbuf_in==NULL
	if (cc->hbuf_out)       cc->hbuf_out       -= (uintptr_t)cc->obuf - 1;
	if (cc->saved_hbuf_out) cc->saved_hbuf_out -= (uintptr_t)cc->obuf - 1;
	
	//TODO: These are unsupported
	//const br_x509_certificate *chain;
	//size_t chain_len;
	//const unsigned char *cert_cur;
	//size_t cert_len;
}

static void br_ssl_engine_unfreeze(br_ssl_engine_context* frozen, br_ssl_engine_context* target, const uint8_t * t0_init)
{
	frozen->ibuf = target->ibuf;
	frozen->obuf = target->obuf;
	
	frozen->icbc_in     = target->icbc_in;
	frozen->igcm_in     = target->igcm_in;
	frozen->ichapol_in  = target->ichapol_in;
	frozen->icbc_out    = target->icbc_out;
	frozen->igcm_out    = target->igcm_out;
	frozen->ichapol_out = target->ichapol_out;
	
	if (frozen->in.vtable == (void*)1) frozen->in.vtable = &target->icbc_in->inner;
	if (frozen->in.vtable == (void*)2) frozen->in.vtable = &target->igcm_in->inner;
	if (frozen->in.vtable == (void*)3) frozen->in.vtable = &target->ichapol_in->inner;
	if (frozen->out.vtable == (void*)1) frozen->out.vtable = &target->icbc_out->inner;
	if (frozen->out.vtable == (void*)2) frozen->out.vtable = &target->igcm_out->inner;
	if (frozen->out.vtable == (void*)3) frozen->out.vtable = &target->ichapol_out->inner;
	
	frozen->cpu.dp += ((uintptr_t)target->dp_stack)/4;
	frozen->cpu.rp += ((uintptr_t)target->rp_stack)/4;
	frozen->cpu.ip += (uintptr_t)t0_init;
	if (frozen->hbuf_in )       frozen->hbuf_in        += (uintptr_t)target->ibuf - 1;
	if (frozen->hbuf_out)       frozen->hbuf_out       += (uintptr_t)target->obuf - 1;
	if (frozen->saved_hbuf_out) frozen->saved_hbuf_out += (uintptr_t)target->obuf - 1;
	frozen->hsrun = target->hsrun;
	
	memcpy(&frozen->mhash.impl, &target->mhash.impl, sizeof(frozen->mhash.impl));
	
	frozen->x509ctx = target->x509ctx;
	frozen->protocol_names = target->protocol_names;
	
	frozen->prf10 = target->prf10;
	frozen->prf_sha256 = target->prf_sha256;
	frozen->prf_sha384 = target->prf_sha384;
	frozen->iaes_cbcenc = target->iaes_cbcenc;
	frozen->iaes_cbcdec = target->iaes_cbcdec;
	frozen->iaes_ctr = target->iaes_ctr;
	frozen->ides_cbcenc = target->ides_cbcenc;
	frozen->ides_cbcdec = target->ides_cbcdec;
	frozen->ighash = target->ighash;
	frozen->ichacha = target->ichacha;
	frozen->ipoly = target->ipoly;
	frozen->icbc_in = target->icbc_in;
	frozen->icbc_out = target->icbc_out;
	frozen->igcm_in = target->igcm_in;
	frozen->igcm_out = target->igcm_out;
	frozen->ichapol_in = target->ichapol_in;
	frozen->ichapol_out = target->ichapol_out;
	frozen->iec = target->iec;
	frozen->irsavrfy = target->irsavrfy;
	frozen->iecdsa = target->iecdsa;
	
	memcpy(target, frozen, sizeof(*target));
}

static const uint8_t * br_ssl_client_get_default_t0()
{
	return br_t0_extract(br_ssl_hs_client_init_main);
}

void br_ssl_client_freeze(br_ssl_client_context* sc)
{
	br_ssl_engine_freeze(&sc->eng, br_ssl_client_get_default_t0());
}

void br_ssl_client_unfreeze(br_ssl_client_context* frozen, br_ssl_client_context* target)
{
	frozen->client_auth_vtable = target->client_auth_vtable;
	//TODO: this is, like, 50 kinds of unsafe, fix later (or never, I don't need client auth)
	//memcpy(&frozen->client_auth, &target->client_auth, sizeof(frozen->client_auth));
	frozen->irsapub = target->irsapub;
	br_ssl_engine_unfreeze(&frozen->eng, &target->eng, br_ssl_client_get_default_t0());
}

static const uint8_t * br_x509_minimal_get_default_t0()
{
	return br_t0_extract(br_x509_minimal_init_main);
}

void br_x509_minimal_freeze(br_x509_minimal_context* sc, br_ssl_engine_context* reference)
{
	if (sc->pkey.key_type==BR_KEYTYPE_RSA)
	{
		if (sc->pkey.key.rsa.n) sc->pkey.key.rsa.n -= (uintptr_t)sc; // these point to either NULL, ->pkey_data or ->ee_pkey_data
		if (sc->pkey.key.rsa.e) sc->pkey.key.rsa.e -= (uintptr_t)sc;
	}
	else
	{
		if (sc->pkey.key.ec.q) sc->pkey.key.ec.q -= (uintptr_t)sc;
	}
	
	sc->cpu.dp -= ((uintptr_t)sc->dp_stack)/4;
	sc->cpu.rp -= ((uintptr_t)sc->rp_stack)/4;
	sc->cpu.ip -= (uintptr_t)br_x509_minimal_get_default_t0();
	
	if (sc->hbuf) sc->hbuf -= (uintptr_t)reference; // points to ssl_engine->pad if set
}

void br_x509_minimal_unfreeze(br_x509_minimal_context* frozen, br_x509_minimal_context* target, br_ssl_engine_context* reference)
{
	frozen->vtable = target->vtable;
	
	if (frozen->pkey.key_type==BR_KEYTYPE_RSA)
	{
		if (frozen->pkey.key.rsa.n) frozen->pkey.key.rsa.n += (uintptr_t)target;
		if (frozen->pkey.key.rsa.e) frozen->pkey.key.rsa.e += (uintptr_t)target;
	}
	else
	{
		if (frozen->pkey.key.ec.q) frozen->pkey.key.ec.q += (uintptr_t)target;
	}
	
	frozen->cpu.dp += ((uintptr_t)target->dp_stack)/4;
	frozen->cpu.rp += ((uintptr_t)target->rp_stack)/4;
	frozen->cpu.ip += (uintptr_t)br_x509_minimal_get_default_t0();
	
	if (frozen->server_name) frozen->server_name = reference->server_name;
	
	if (frozen->hbuf) frozen->hbuf += (uintptr_t)reference;
	
	frozen->trust_anchors = target->trust_anchors;
	
	memcpy(&frozen->mhash.impl, &target->mhash.impl, sizeof(frozen->mhash.impl));
	
	frozen->dn_hash_impl = target->dn_hash_impl;
	frozen->dn_hash.vtable = target->dn_hash.vtable;
	
	frozen->name_elts = target->name_elts;
	
	frozen->irsa = target->irsa;
	frozen->iecdsa = target->iecdsa;
	frozen->iec = target->iec;
	
	memcpy(target, frozen, sizeof(*target));
}
#endif
