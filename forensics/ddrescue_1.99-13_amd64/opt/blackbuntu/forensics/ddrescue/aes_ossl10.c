/** aes_ossl10.c
 *
 * Wrapper for openSSL-1.0 to fit into dd_rescue's crypt
 * structures. Mainly useful for testing
 *
 * (c) Kurt Garloff <kurt@garloff.de>, 8/2014
 * License: GPLv2 or v3 or BSD (3-clause)
 */

#include "aes.h"
#include "aes_ossl.h"
#ifdef HAVE_OPENSSL_EVP_H
#include <openssl/evp.h>
#endif
#include <assert.h>
#include "secmem.h"
#include <string.h>

#include <netinet/in.h>

void AES_OSSL_Bits_EKey_Expand(const EVP_CIPHER *cipher, const unsigned char* userkey, unsigned char *ctx)
{
	EVP_CIPHER_CTX *evpctx = (EVP_CIPHER_CTX*)ctx;
	EVP_CIPHER_CTX_init(evpctx);
	EVP_EncryptInit_ex(evpctx, cipher, NULL, userkey, NULL);
}
void AES_OSSL_Bits_DKey_Expand(const EVP_CIPHER *cipher, const unsigned char* userkey, unsigned char *ctx)
{
	EVP_CIPHER_CTX *evpctx = (EVP_CIPHER_CTX*)ctx;
	EVP_CIPHER_CTX_init(evpctx);
	EVP_DecryptInit_ex(evpctx, cipher, NULL, userkey, NULL);
}

void AES_OSSL_Recycle(unsigned char *ctx)
{
	EVP_CIPHER_CTX *evpctx = (EVP_CIPHER_CTX*)ctx;
	evpctx->final_used = 0;	evpctx->buf_len = 0;
	evpctx->num = 0; /*CTR */
}

#define AES_OSSL_KEY_EX(BITS, ROUNDS, CHAIN)	\
void AES_OSSL_##BITS##_EKey_Expand_##CHAIN (const unsigned char *userkey, unsigned char *ctx, unsigned int rounds)	\
{							\
	assert(rounds == ROUNDS);			\
	AES_OSSL_Bits_EKey_Expand(EVP_aes_##BITS##_##CHAIN (), userkey, ctx);	\
};							\
void AES_OSSL_##BITS##_DKey_Expand_##CHAIN (const unsigned char *userkey, unsigned char *ctx, unsigned int rounds)	\
{							\
	assert(rounds == ROUNDS);			\
	AES_OSSL_Bits_DKey_Expand(EVP_aes_##BITS##_##CHAIN (), userkey, ctx);	\
}

#define AES_OSSL_CRYPT(BITCHAIN, IV, DOPAD)			\
int AES_OSSL_##BITCHAIN##_Encrypt(const unsigned char* ctx, unsigned int rounds,\
			        unsigned char* iv, unsigned int pad, 		\
				const unsigned char* in, unsigned char* out, 	\
				ssize_t len, ssize_t *flen)			\
{								\
	int olen, elen, ores;					\
	EVP_CIPHER_CTX *evpctx = (EVP_CIPHER_CTX*)ctx;		\
	/*EVP_EncryptInit(evpctx, NULL, NULL, NULL);*/		\
	EVP_CIPHER_CTX_set_padding(evpctx, DOPAD? pad: 0);	\
	if (IV) {						\
		memcpy(evpctx->oiv, iv, 16); memcpy(evpctx->iv, iv, 16);	\
	}							\
	if (!len && !pad) { *flen = 0; return 0; }		\
       	if (DOPAD && !pad && (len&15)) {			\
		ores = EVP_EncryptUpdate(evpctx, out, &olen, in, len-(len&15));	\
		assert(ores);					\
		uchar *ibf = crypto->blkbuf2;			\
		memcpy(ibf, in+olen, len&15);			\
		memset(ibf+(len&15), 0, 16-(len&15));		\
		ores = EVP_EncryptUpdate(evpctx, out+olen, &elen, ibf, 16);	\
		memset(ibf, 0, len&15);				\
		LFENCE;						\
		assert(ores);					\
	} else {								\
		if (DOPAD && !(len%16) && pad == PAD_ASNEEDED)	\
			EVP_CIPHER_CTX_set_padding(evpctx, 0);	\
		ores = EVP_EncryptUpdate(evpctx, out, &olen, in, len);		\
		assert(ores);					\
		ores = EVP_EncryptFinal(evpctx, out+olen, &elen);		\
		assert(ores);					\
		if (0 && elen && (len&15)) olen -= 16;		\
	}							\
	*flen = olen+elen;					\
	if (0 && DOPAD && pad == PAD_ASNEEDED && !(len&15))			\
		*flen -= 16;					\
	if (0 && olen+elen < len)				\
		fprintf(stderr, "Encryption length mismatch %i+%i != %zi\n",	\
			olen, elen, len);			\
	if (IV)							\
		memcpy(iv, evpctx->iv, 16);			\
	return (DOPAD && (pad == PAD_ALWAYS || (len&15)))? 16-(len&15): 0;	\
};								\
int AES_OSSL_##BITCHAIN##_Decrypt(const unsigned char* ctx, unsigned int rounds,\
			        unsigned char* iv, unsigned int pad,		\
				const unsigned char* in, unsigned char* out,	\
	       			ssize_t len, ssize_t *flen)			\
{								\
	int olen, elen = 0, ores;				\
	int ilen = (len&15)? len+15-(len&15): len;		\
	EVP_CIPHER_CTX *evpctx = (EVP_CIPHER_CTX*)ctx;		\
	/*EVP_DecryptInit(evpctx, NULL, NULL, NULL);*/		\
	EVP_CIPHER_CTX_set_padding(evpctx, DOPAD && pad != PAD_ASNEEDED?pad:0);	\
	if (IV) {						\
		memcpy(evpctx->oiv, iv, 16); memcpy(evpctx->iv, iv, 16);	\
	}							\
	if (!len && pad != PAD_ALWAYS) { *flen = 0; return 0; }	\
	if (DOPAD && pad == PAD_ASNEEDED) {			\
		int olen1;					\
		uchar *buf = crypto->blkbuf3;			\
		ores = EVP_DecryptUpdate(evpctx, out, &olen, in, ilen-16);	\
		assert(ores);					\
		EVP_CIPHER_CTX ctx2;				\
		memcpy(&ctx2, evpctx, sizeof(ctx2));		\
		/* Save piece that gets overwritten */		\
		if (in == out)					\
			memcpy(buf, out+olen, 16);		\
		EVP_CIPHER_CTX_set_padding(evpctx, 1);		\
		ores = EVP_DecryptUpdate(evpctx, out+olen, &olen1, in+ilen-16, 16);	\
		assert(ores); assert(!olen1);			\
		ores = EVP_DecryptFinal(evpctx, out+olen, &elen);		\
		if (!ores) {					\
			memcpy(evpctx, &ctx2, sizeof(ctx2));	\
			if (in == out)				\
				memcpy(out+olen, buf, 16);	\
			ores = EVP_DecryptUpdate(evpctx, out+olen, &olen1, in+ilen-16, 16);	\
			assert(ores); assert(olen1 == 16);	\
			olen += olen1;				\
			ores = EVP_DecryptFinal(evpctx, out+olen, &elen);	\
			assert(ores);				\
		}						\
		memset(&ctx2, 0, sizeof(ctx2));			\
		LFENCE;						\
	} else {						\
		ores = EVP_DecryptUpdate(evpctx, out, &olen, in, ilen);	\
		assert(ores);					\
		ores = EVP_DecryptFinal(evpctx, out+olen, &elen);	\
	}							\
	if (DOPAD && pad) {					\
		*flen = olen + elen;				\
	} else							\
		*flen = len;					\
	if (IV)							\
		memcpy(iv, evpctx->iv, 16);			\
	if (DOPAD && pad == PAD_ASNEEDED)			\
		return (elen? 16-elen: ILLEGAL_PADDING);	\
	return ores - 1;					\
}

void AES_OSSL_Release(unsigned char *ctx, unsigned int rounds)
{
	EVP_CIPHER_CTX *evpctx = (EVP_CIPHER_CTX*)ctx;
	EVP_CIPHER_CTX_cleanup(evpctx);
}

AES_OSSL_KEY_EX(128, AES_128_ROUNDS, ecb);
AES_OSSL_KEY_EX(128, AES_128_ROUNDS, cbc);
AES_OSSL_KEY_EX(128, AES_128_ROUNDS, ctr);

AES_OSSL_CRYPT(128_ECB, 0, 1);
AES_OSSL_CRYPT(128_CBC, 1, 1);
AES_OSSL_CRYPT(128_CTR, 1, 0);

AES_OSSL_KEY_EX(192, AES_192_ROUNDS, ecb);
AES_OSSL_KEY_EX(192, AES_192_ROUNDS, cbc);
AES_OSSL_KEY_EX(192, AES_192_ROUNDS, ctr);

AES_OSSL_CRYPT(192_ECB, 0, 1);
AES_OSSL_CRYPT(192_CBC, 1, 1);
AES_OSSL_CRYPT(192_CTR, 1, 0);

AES_OSSL_KEY_EX(256, AES_256_ROUNDS, ecb);
AES_OSSL_KEY_EX(256, AES_256_ROUNDS, cbc);
AES_OSSL_KEY_EX(256, AES_256_ROUNDS, ctr);

AES_OSSL_CRYPT(256_ECB, 0, 1);
AES_OSSL_CRYPT(256_CBC, 1, 1);
AES_OSSL_CRYPT(256_CTR, 1, 0);

/* Double encryption 
 * This only works in a straightforward way for ECB ...
 * For the others we need to break up the loop:
 * ECB: AES2(AES1(p)) == AESx2(p)
 * CBC: AES2(IV2^AES1(IV1^p)) != AESx2(IV^p)
 * CTR: AES2(CTR)^AES1(CTR)^p != AES2(AES1(CTR))^p == AESx2(CTR)^p
 * */

#include "sha256.h"

void AES_OSSL_Bits_EKey_ExpandX2(const EVP_CIPHER *cipher, const unsigned char* userkey, unsigned char *ctx, unsigned int bits)
{
	EVP_CIPHER_CTX *evpctx = (EVP_CIPHER_CTX*)ctx;
	EVP_CIPHER_CTX_init(evpctx);
	EVP_EncryptInit_ex(evpctx, cipher, NULL, userkey, NULL);
	//EVP_CipherInit_ex(evpctx, cipher, NULL, userkey, NULL, 1);
	//EVP_CIPHER_CTX_set_padding(evpctx, 0);
	hash_t hv;
	sha256_init(&hv);
	sha256_calc(userkey, bits/8, bits/8, &hv);
	uchar usrkey2[32];
	sha256_beout(usrkey2, &hv);
	sha256_init(&hv);
	EVP_CIPHER_CTX_init(evpctx+1);
	EVP_EncryptInit_ex(evpctx+1, cipher, NULL, usrkey2, NULL);
	//EVP_CIPHER_CTX_set_padding(evpctx+1, 0);
	memset(usrkey2, 0, 32);
	LFENCE;
}
void AES_OSSL_Bits_DKey_ExpandX2(const EVP_CIPHER *cipher, const unsigned char* userkey, unsigned char *ctx, unsigned int bits)
{
	EVP_CIPHER_CTX *evpctx = (EVP_CIPHER_CTX*)ctx;
	EVP_CIPHER_CTX_init(evpctx);
	EVP_DecryptInit_ex(evpctx, cipher, NULL, userkey, NULL);
	//EVP_CIPHER_CTX_set_padding(evpctx, 0);
	hash_t hv;
	sha256_init(&hv);
	sha256_calc(userkey, bits/8, bits/8, &hv);
	uchar usrkey2[32];
	sha256_beout(usrkey2, &hv);
	sha256_init(&hv);
	EVP_CIPHER_CTX_init(evpctx+1);
	EVP_DecryptInit_ex(evpctx+1, cipher, NULL, usrkey2, NULL);
	//EVP_CIPHER_CTX_set_padding(evpctx+1, 0);
	memset(usrkey2, 0, 32);
	LFENCE;
}

void AES_OSSL_RecycleX2(unsigned char *ctx)
{
	EVP_CIPHER_CTX *evpctx = (EVP_CIPHER_CTX*)ctx;
	evpctx[0].final_used = 0; evpctx[0].buf_len = 0;
	/* Not needed?:
	evpctx[0].num = 0;
	evpctx[1].final_used = 0; evpctx[1].buf_len = 0;
	evpctx[1].num = 0;
	*/
}

#define AES_OSSL_KEY_EX2(BITS, ROUNDS, CHAIN)	\
void AES_OSSL_##BITS##_EKey_ExpandX2_##CHAIN (const unsigned char *userkey, unsigned char *ctx, unsigned int rounds)	\
{							\
	assert(rounds == 2*ROUNDS);			\
	AES_OSSL_Bits_EKey_ExpandX2(EVP_aes_##BITS##_##CHAIN (), userkey, ctx, BITS);	\
};							\
void AES_OSSL_##BITS##_DKey_ExpandX2_##CHAIN (const unsigned char *userkey, unsigned char *ctx, unsigned int rounds)	\
{							\
	assert(rounds == 2*ROUNDS);			\
	AES_OSSL_Bits_DKey_ExpandX2(EVP_aes_##BITS##_##CHAIN (), userkey, ctx, BITS);	\
}

#define AES_OSSL_CRYPT2(BITCHAIN, IV)	\
int  AES_OSSL_##BITCHAIN##_EncryptX2(const unsigned char* ctx, unsigned int rounds,	\
			        unsigned char* iv, unsigned int pad,			\
				const unsigned char* in, unsigned char* out,		\
	       			ssize_t len, ssize_t *flen)				\
{								\
	int olen, elen, ores;					\
	EVP_CIPHER_CTX *evpctx = (EVP_CIPHER_CTX*)ctx;		\
	EVP_CIPHER_CTX_set_padding(evpctx, pad);		\
	EVP_CIPHER_CTX_set_padding(evpctx+1, 0);		\
	if (IV) {						\
		memcpy(evpctx->oiv, iv, 16); memcpy(evpctx->iv, iv, 16);		\
		memcpy((evpctx+1)->oiv, iv, 16); memcpy((evpctx+1)->iv, iv, 16);	\
	}							\
	if (!len && !pad) { *flen = 0; return 0; }		\
       	if (!pad && (len&15)) {					\
		ores = EVP_EncryptUpdate(evpctx, out, &olen, in, len-(len&15));		\
		assert(ores);					\
		uchar *ibf = crypto->blkbuf2;			\
		memcpy(ibf, in+olen, len&15);			\
		memset(ibf+(len&15), 0, 16-(len&15));		\
		ores = EVP_EncryptUpdate(evpctx, out+olen, &elen, ibf, 16);		\
		memset(ibf, 0, len&15);				\
		LFENCE;						\
		assert(ores);					\
	} else {						\
		ores = EVP_EncryptUpdate(evpctx, out, &olen, in, len);	\
		assert(ores);					\
		ores = EVP_EncryptFinal(evpctx, out+olen, &elen);	\
		assert(ores);					\
	}							\
	ores = EVP_EncryptUpdate(evpctx+1, out, &olen, out, olen+elen);	\
	assert(ores);						\
	ores = EVP_EncryptFinal(evpctx+1, out+olen, &elen);	\
	assert(ores);						\
	*flen = olen+elen;					\
	if (pad == PAD_ASNEEDED && !(len&15))			\
		*flen -= 16;					\
	if (IV)							\
		memcpy(iv, evpctx->iv, 16);			\
	return (pad == PAD_ALWAYS || (len&15))? 16-(len&15): 0;	\
};								\
int  AES_OSSL_##BITCHAIN##_DecryptX2(const unsigned char* ctx, unsigned int rounds,	\
			        unsigned char* iv, unsigned int pad,			\
				const unsigned char* in, unsigned char* out,		\
	       			ssize_t len, ssize_t *flen)				\
{								\
	int olen, elen, ores;					\
	int rlen = (len&15)? len+16-(len&15): len;		\
	EVP_CIPHER_CTX *evpctx = (EVP_CIPHER_CTX*)ctx;		\
	EVP_CIPHER_CTX_set_padding(evpctx+1, 0);		\
	EVP_CIPHER_CTX_set_padding(evpctx, pad==PAD_ASNEEDED? 0: pad);	\
	if (IV) {						\
		memcpy((evpctx+1)->oiv, iv, 16); memcpy((evpctx+1)->iv, iv, 16);	\
		memcpy(evpctx->oiv, iv, 16); memcpy(evpctx->iv, iv, 16);		\
	}							\
	if (!len && pad != PAD_ALWAYS) { *flen = 0; return 0; }	\
	ores = EVP_DecryptUpdate(evpctx+1, out, &olen, in, rlen);		\
	assert(ores);						\
	ores = EVP_DecryptFinal(evpctx+1, out+olen, &elen);	\
	assert(ores);						\
	if (pad == PAD_ASNEEDED) {				\
		int ilen = olen, olen1;				\
		uchar *buf = crypto->blkbuf3;			\
		ores = EVP_DecryptUpdate(evpctx, out, &olen, out, ilen-16);	\
		assert(ores); assert(olen == ilen-16);		\
		/* Save piece that gets overwritten */		\
		memcpy(buf, out+olen, 16);			\
		EVP_CIPHER_CTX ctx2;				\
		memcpy(&ctx2, evpctx, sizeof(ctx2));		\
		EVP_CIPHER_CTX_set_padding(evpctx, 1);		\
		ores = EVP_DecryptUpdate(evpctx, out+olen, &olen1, out+ilen-16, 16);	\
		assert(ores); assert(!olen1);			\
		ores = EVP_DecryptFinal(evpctx, out+olen, &elen);		\
		if (!ores) {					\
			memcpy(evpctx, &ctx2, sizeof(ctx2));	\
			memcpy(out+olen, buf, 16);		\
			ores = EVP_DecryptUpdate(evpctx, out+olen, &olen1, out+ilen-16, 16);	\
			assert(ores); assert(olen1 == 16);	\
			olen += olen1;				\
			ores = EVP_DecryptFinal(evpctx, out+olen, &elen);	\
			assert(ores);				\
		}						\
		memset(&ctx2, 0, sizeof(ctx2));			\
		LFENCE;						\
	} else {						\
		ores = EVP_DecryptUpdate(evpctx, out, &olen, out, olen+elen);	\
		assert(ores);						\
		ores = EVP_DecryptFinal(evpctx, out+olen, &elen);	\
	}							\
	if (pad)						\
		*flen = olen+elen;				\
	else							\
		*flen = len;					\
	if (IV)							\
		memcpy(iv, evpctx->iv, 16);			\
	if (pad == PAD_ASNEEDED)				\
		return (elen? 16-elen: ILLEGAL_PADDING);	\
	return ores - 1;					\
}

void AES_OSSL_ReleaseX2(unsigned char *ctx, unsigned int rounds)
{
	EVP_CIPHER_CTX *evpctx = (EVP_CIPHER_CTX*)ctx;
	EVP_CIPHER_CTX_cleanup(evpctx+1);
	EVP_CIPHER_CTX_cleanup(evpctx);
}

AES_OSSL_KEY_EX2(128, AES_128_ROUNDS, ecb);
//AES_OSSL_KEY_EX2(128, AES_128_ROUNDS, cbc);
//AES_OSSL_KEY_EX2(128, AES_128_ROUNDS, ctr);

AES_OSSL_CRYPT2(128_ECB, 0);
//AES_OSSL_CRYPT2(128_CBC, 1);
//AES_OSSL_CRYPT2(128_CTR, 1);

AES_OSSL_KEY_EX2(192, AES_192_ROUNDS, ecb);
//AES_OSSL_KEY_EX2(192, AES_192_ROUNDS, cbc);
//AES_OSSL_KEY_EX2(192, AES_192_ROUNDS, ctr);

AES_OSSL_CRYPT2(192_ECB, 0);
//AES_OSSL_CRYPT2(192_CBC, 1);
//AES_OSSL_CRYPT2(192_CTR, 1);

AES_OSSL_KEY_EX2(256, AES_256_ROUNDS, ecb);
//AES_OSSL_KEY_EX2(256, AES_256_ROUNDS, cbc);
//AES_OSSL_KEY_EX2(256, AES_256_ROUNDS, ctr);

AES_OSSL_CRYPT2(256_ECB, 0);
//AES_OSSL_CRYPT2(256_CBC, 1);
//AES_OSSL_CRYPT2(256_CTR, 1);

void AES_OSSL_Blk_EncryptX2(const unsigned char *ctx, unsigned int rounds,
			    const unsigned char *in, unsigned char *out)			
{
	EVP_CIPHER_CTX *evpctx = (EVP_CIPHER_CTX*)ctx;
	int olen;
	uchar *blk = crypto->blkbuf1;
	EVP_EncryptUpdate(evpctx, blk, &olen, in, 16);
	EVP_EncryptUpdate(evpctx+1, out, &olen, blk, olen);
	memset(blk, 0, 16);
	LFENCE;
}
void AES_OSSL_Blk_DecryptX2(const unsigned char *ctx, unsigned int rounds,
			    const unsigned char *in, unsigned char *out)			
{
	EVP_CIPHER_CTX *evpctx = (EVP_CIPHER_CTX*)ctx;
	int olen;
	uchar *blk = crypto->blkbuf1;
	EVP_DecryptUpdate(evpctx+1, blk, &olen, in, 16);
	EVP_DecryptUpdate(evpctx, out, &olen, blk, olen);
	memset(blk, 0, 16);
	LFENCE;
}


#define AES_OSSL_DECL_CBC_X2(BITS)							\
int  AES_OSSL_##BITS##_CBC_EncryptX2(const unsigned char *ctx, unsigned int rounds,	\
				     unsigned char *iv, unsigned int pad,		\
				     const unsigned char* in, unsigned char *out,	\
	       			     ssize_t len, ssize_t *olen)			\
{											\
	EVP_CIPHER_CTX *evpctx = (EVP_CIPHER_CTX*)ctx;					\
	/* EVP_EncryptInit(evpctx, NULL, NULL, NULL);					\
	EVP_EncryptInit(evpctx+1, NULL, NULL, NULL); */					\
	EVP_CIPHER_CTX_set_padding(evpctx, 0);						\
	EVP_CIPHER_CTX_set_padding(evpctx+1, 0);					\
	return AES_Gen_CBC_Enc(AES_OSSL_Blk_EncryptX2, ctx, rounds, iv, pad, in, out, len, olen);	\
};											\
int  AES_OSSL_##BITS##_CBC_DecryptX2(const unsigned char *ctx, unsigned int rounds,	\
				     unsigned char *iv, unsigned int pad,		\
				     const unsigned char* in, unsigned char *out,	\
	       			     ssize_t len, ssize_t *olen)			\
{											\
	EVP_CIPHER_CTX *evpctx = (EVP_CIPHER_CTX*)ctx;					\
	/* EVP_DecryptInit(evpctx+1, NULL, NULL, NULL);					\
	EVP_DecryptInit(evpctx, NULL, NULL, NULL); */					\
	EVP_CIPHER_CTX_set_padding(evpctx+1, 0);					\
	EVP_CIPHER_CTX_set_padding(evpctx, 0);						\
	return AES_Gen_CBC_Dec(AES_OSSL_Blk_DecryptX2, ctx, rounds, iv, pad, in, out, len, olen);	\
}

AES_OSSL_DECL_CBC_X2(128);
AES_OSSL_DECL_CBC_X2(192);
AES_OSSL_DECL_CBC_X2(256);


#define AES_OSSL_DECL_CTR_X2(BITS)							\
int  AES_OSSL_##BITS##_CTR_CryptX2(const unsigned char *ctx, unsigned int rounds,	\
				     unsigned char *iv, unsigned int pad,		\
				     const unsigned char* in, unsigned char *out,	\
	       			     ssize_t len, ssize_t *olen)			\
{											\
	*olen = len;									\
	EVP_CIPHER_CTX *evpctx = (EVP_CIPHER_CTX*)ctx;					\
	/* EVP_EncryptInit(evpctx, NULL, NULL, NULL);					\
	EVP_EncryptInit(evpctx+1, NULL, NULL, NULL); */					\
	EVP_CIPHER_CTX_set_padding(evpctx, 0);						\
	EVP_CIPHER_CTX_set_padding(evpctx+1, 0);					\
	return AES_Gen_CTR_Crypt(AES_OSSL_Blk_EncryptX2, ctx, rounds, iv, in, out, len);\
}

AES_OSSL_DECL_CTR_X2(128);
AES_OSSL_DECL_CTR_X2(192);
AES_OSSL_DECL_CTR_X2(256);


#define EVP_CTX_SZ sizeof(EVP_CIPHER_CTX)
#define EVP_CTX_SZX2 2*sizeof(EVP_CIPHER_CTX)

ciph_desc_t AES_OSSL_Methods[] = {
		 {"AES128-ECB"  , 128, 10, 16, EVP_CTX_SZ, &aes_stream_ecb,
			AES_OSSL_128_EKey_Expand_ecb, AES_OSSL_128_DKey_Expand_ecb,
			AES_OSSL_128_ECB_Encrypt, AES_OSSL_128_ECB_Decrypt, AES_OSSL_Release,
			0, AES_OSSL_Recycle},
		 {"AES128-CBC"  , 128, 10, 16, EVP_CTX_SZ, &aes_stream_cbc,
			AES_OSSL_128_EKey_Expand_cbc, AES_OSSL_128_DKey_Expand_cbc,
			AES_OSSL_128_CBC_Encrypt, AES_OSSL_128_CBC_Decrypt, AES_OSSL_Release,
			0, AES_OSSL_Recycle},
		 {"AES128-CTR"  , 128, 10, 16, EVP_CTX_SZ, &aes_stream_ctr,
			AES_OSSL_128_EKey_Expand_ctr, AES_OSSL_128_EKey_Expand_ctr,
			AES_OSSL_128_CTR_Encrypt, AES_OSSL_128_CTR_Encrypt, AES_OSSL_Release,
			0, AES_OSSL_Recycle},
		 {"AES192-ECB"  , 192, 12, 16, EVP_CTX_SZ, &aes_stream_ecb,
			AES_OSSL_192_EKey_Expand_ecb, AES_OSSL_192_DKey_Expand_ecb,
			AES_OSSL_192_ECB_Encrypt, AES_OSSL_192_ECB_Decrypt, AES_OSSL_Release,
			0, AES_OSSL_Recycle},
		 {"AES192-CBC"  , 192, 12, 16, EVP_CTX_SZ, &aes_stream_cbc,
			AES_OSSL_192_EKey_Expand_cbc, AES_OSSL_192_DKey_Expand_cbc,
			AES_OSSL_192_CBC_Encrypt, AES_OSSL_192_CBC_Decrypt, AES_OSSL_Release,
			0, AES_OSSL_Recycle},
		 {"AES192-CTR"  , 192, 12, 16, EVP_CTX_SZ, &aes_stream_ctr,
			AES_OSSL_192_EKey_Expand_ctr, AES_OSSL_192_EKey_Expand_ctr,
			AES_OSSL_192_CTR_Encrypt, AES_OSSL_192_CTR_Encrypt, AES_OSSL_Release,
			0, AES_OSSL_Recycle},
		 {"AES256-ECB"  , 256, 14, 16, EVP_CTX_SZ, &aes_stream_ecb,
			AES_OSSL_256_EKey_Expand_ecb, AES_OSSL_256_DKey_Expand_ecb,
			AES_OSSL_256_ECB_Encrypt, AES_OSSL_256_ECB_Decrypt, AES_OSSL_Release,
			0, AES_OSSL_Recycle},
		 {"AES256-CBC"  , 256, 14, 16, EVP_CTX_SZ, &aes_stream_cbc,
			AES_OSSL_256_EKey_Expand_cbc, AES_OSSL_256_DKey_Expand_cbc,
			AES_OSSL_256_CBC_Encrypt, AES_OSSL_256_CBC_Decrypt, AES_OSSL_Release,
			0, AES_OSSL_Recycle},
		 {"AES256-CTR"  , 256, 14, 16, EVP_CTX_SZ, &aes_stream_ctr,
			AES_OSSL_256_EKey_Expand_ctr, AES_OSSL_256_EKey_Expand_ctr,
			AES_OSSL_256_CTR_Encrypt, AES_OSSL_256_CTR_Encrypt, AES_OSSL_Release,
			0, AES_OSSL_Recycle},
		 /* TODO: Plus methods non-trivial with openssl */
		 {"AES128x2-ECB", 128, 20, 16, EVP_CTX_SZX2, &aes_stream_ecb,
			AES_OSSL_128_EKey_ExpandX2_ecb, AES_OSSL_128_DKey_ExpandX2_ecb,
			AES_OSSL_128_ECB_EncryptX2, AES_OSSL_128_ECB_DecryptX2, AES_OSSL_ReleaseX2,
			0, AES_OSSL_RecycleX2},
		 {"AES128x2-CBC", 128, 20, 16, EVP_CTX_SZX2, &aes_stream_cbc,
			AES_OSSL_128_EKey_ExpandX2_ecb, AES_OSSL_128_DKey_ExpandX2_ecb,
			AES_OSSL_128_CBC_EncryptX2, AES_OSSL_128_CBC_DecryptX2, AES_OSSL_ReleaseX2,
			0, AES_OSSL_RecycleX2},
		 {"AES128x2-CTR", 128, 20, 16, EVP_CTX_SZX2, &aes_stream_ctr,
			AES_OSSL_128_EKey_ExpandX2_ecb, AES_OSSL_128_EKey_ExpandX2_ecb,
			AES_OSSL_128_CTR_CryptX2, AES_OSSL_128_CTR_CryptX2, AES_OSSL_ReleaseX2,
			0, AES_OSSL_RecycleX2},
		 {"AES192x2-ECB", 192, 24, 16, EVP_CTX_SZX2, &aes_stream_ecb,
			AES_OSSL_192_EKey_ExpandX2_ecb, AES_OSSL_192_DKey_ExpandX2_ecb,
			AES_OSSL_192_ECB_EncryptX2, AES_OSSL_192_ECB_DecryptX2, AES_OSSL_ReleaseX2,
			0, AES_OSSL_RecycleX2},
		 {"AES192x2-CBC", 192, 24, 16, EVP_CTX_SZX2, &aes_stream_cbc,
			AES_OSSL_192_EKey_ExpandX2_ecb, AES_OSSL_192_DKey_ExpandX2_ecb,
			AES_OSSL_192_CBC_EncryptX2, AES_OSSL_192_CBC_DecryptX2, AES_OSSL_ReleaseX2,
			0, AES_OSSL_RecycleX2},
		 {"AES192x2-CTR", 192, 24, 16, EVP_CTX_SZX2, &aes_stream_ctr,
			AES_OSSL_192_EKey_ExpandX2_ecb, AES_OSSL_192_EKey_ExpandX2_ecb,
			AES_OSSL_192_CTR_CryptX2, AES_OSSL_192_CTR_CryptX2, AES_OSSL_ReleaseX2,
			0, AES_OSSL_RecycleX2},
		 {"AES256x2-ECB", 256, 28, 16, EVP_CTX_SZX2, &aes_stream_ecb,
			AES_OSSL_256_EKey_ExpandX2_ecb, AES_OSSL_256_DKey_ExpandX2_ecb,
			AES_OSSL_256_ECB_EncryptX2, AES_OSSL_256_ECB_DecryptX2, AES_OSSL_ReleaseX2,
			0, AES_OSSL_RecycleX2},
		 {"AES256x2-CBC", 256, 28, 16, EVP_CTX_SZX2, &aes_stream_cbc,
			AES_OSSL_256_EKey_ExpandX2_ecb, AES_OSSL_256_DKey_ExpandX2_ecb,
			AES_OSSL_256_CBC_EncryptX2, AES_OSSL_256_CBC_DecryptX2, AES_OSSL_ReleaseX2,
			0, AES_OSSL_RecycleX2},
		 {"AES256x2-CTR", 256, 28, 16, EVP_CTX_SZX2, &aes_stream_ctr,
			AES_OSSL_256_EKey_ExpandX2_ecb, AES_OSSL_256_EKey_ExpandX2_ecb,
			AES_OSSL_256_CTR_CryptX2, AES_OSSL_256_CTR_CryptX2, AES_OSSL_ReleaseX2,
			0, AES_OSSL_RecycleX2},
		 {NULL, /* ... */}
};
