/* Strict supported standard CMS profile validation only, using OpenSSL ASN.1 schemas/APIs.
 * No key establishment, KDF, key wrap, AEAD or envelope generation lives here.
 * Schema: RFC5083/RFC9629/RFC9936 restricted to one key-id ML-KEM-768 recipient,
 * HKDF-SHA256, AES256-WRAP and AES256-GCM with a 12-byte nonce/16-byte tag.
 * Optional originator, attributes and UKM are outside this deliberately narrow profile. */
#include "apx_pqc_profile.h"
#include <limits.h>
#include <string.h>
#include <openssl/asn1t.h>
#include <openssl/cms.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

typedef struct {
    ASN1_INTEGER *version;
    ASN1_OCTET_STRING *key_id;
    X509_ALGOR *kem;
    ASN1_OCTET_STRING *kem_ciphertext;
    X509_ALGOR *kdf;
    ASN1_INTEGER *kek_length;
    X509_ALGOR *wrap;
    ASN1_OCTET_STRING *wrapped_key;
} ApxCMSKEM;
ASN1_SEQUENCE(ApxCMSKEM) = {
    ASN1_SIMPLE(ApxCMSKEM, version, ASN1_INTEGER),
    ASN1_IMP(ApxCMSKEM, key_id, ASN1_OCTET_STRING, 0),
    ASN1_SIMPLE(ApxCMSKEM, kem, X509_ALGOR),
    ASN1_SIMPLE(ApxCMSKEM, kem_ciphertext, ASN1_OCTET_STRING),
    ASN1_SIMPLE(ApxCMSKEM, kdf, X509_ALGOR),
    ASN1_SIMPLE(ApxCMSKEM, kek_length, ASN1_INTEGER),
    ASN1_SIMPLE(ApxCMSKEM, wrap, X509_ALGOR),
    ASN1_SIMPLE(ApxCMSKEM, wrapped_key, ASN1_OCTET_STRING)
} ASN1_SEQUENCE_END(ApxCMSKEM)

typedef struct { ASN1_OBJECT *type; ApxCMSKEM *kem; } ApxCMSORI;
ASN1_SEQUENCE(ApxCMSORI) = {
    ASN1_SIMPLE(ApxCMSORI, type, ASN1_OBJECT),
    ASN1_SIMPLE(ApxCMSORI, kem, ApxCMSKEM)
} ASN1_SEQUENCE_END(ApxCMSORI)

typedef ApxCMSORI ApxCMSRecipient;
ASN1_ITEM_TEMPLATE(ApxCMSRecipient) =
    ASN1_EX_TEMPLATE_TYPE(ASN1_TFLG_IMPLICIT, 4, ApxCMSRecipient, ApxCMSORI)
ASN1_ITEM_TEMPLATE_END(ApxCMSRecipient)
DEFINE_STACK_OF(ApxCMSRecipient)

typedef struct {
    ASN1_OBJECT *type;
    X509_ALGOR *algorithm;
    ASN1_OCTET_STRING *ciphertext;
} ApxCMSEncrypted;
ASN1_SEQUENCE(ApxCMSEncrypted) = {
    ASN1_SIMPLE(ApxCMSEncrypted, type, ASN1_OBJECT),
    ASN1_SIMPLE(ApxCMSEncrypted, algorithm, X509_ALGOR),
    ASN1_IMP(ApxCMSEncrypted, ciphertext, ASN1_OCTET_STRING, 0)
} ASN1_SEQUENCE_END(ApxCMSEncrypted)

typedef struct {
    ASN1_INTEGER *version;
    STACK_OF(ApxCMSRecipient) *recipients;
    ApxCMSEncrypted *encrypted;
    ASN1_OCTET_STRING *tag;
} ApxCMSAuthEnv;
ASN1_SEQUENCE(ApxCMSAuthEnv) = {
    ASN1_SIMPLE(ApxCMSAuthEnv, version, ASN1_INTEGER),
    ASN1_SET_OF(ApxCMSAuthEnv, recipients, ApxCMSRecipient),
    ASN1_SIMPLE(ApxCMSAuthEnv, encrypted, ApxCMSEncrypted),
    ASN1_SIMPLE(ApxCMSAuthEnv, tag, ASN1_OCTET_STRING)
} ASN1_SEQUENCE_END(ApxCMSAuthEnv)

typedef struct { ASN1_OBJECT *type; ApxCMSAuthEnv *envelope; } ApxCMSEnvelope;
ASN1_SEQUENCE(ApxCMSEnvelope) = {
    ASN1_SIMPLE(ApxCMSEnvelope, type, ASN1_OBJECT),
    ASN1_EXP(ApxCMSEnvelope, envelope, ApxCMSAuthEnv, 0)
} ASN1_SEQUENCE_END(ApxCMSEnvelope)
IMPLEMENT_ASN1_FUNCTIONS(ApxCMSEnvelope)

typedef struct { ASN1_OCTET_STRING *nonce; ASN1_INTEGER *tag_length; } ApxCMSGCM;
ASN1_SEQUENCE(ApxCMSGCM) = {
    ASN1_SIMPLE(ApxCMSGCM, nonce, ASN1_OCTET_STRING),
    ASN1_SIMPLE(ApxCMSGCM, tag_length, ASN1_INTEGER)
} ASN1_SEQUENCE_END(ApxCMSGCM)
IMPLEMENT_ASN1_FUNCTIONS(ApxCMSGCM)

static int algorithm_is(X509_ALGOR *algorithm, int nid) {
    const ASN1_OBJECT *oid = NULL; int parameter = -1;
    if (!algorithm) return 0;
    X509_ALGOR_get0(&oid, &parameter, NULL, algorithm);
    return oid && OBJ_obj2nid(oid) == nid && parameter == V_ASN1_UNDEF;
}
int apx_pqc_profile(const unsigned char *der, size_t length,
                           const unsigned char key_id[32], size_t maximum) {
    const unsigned char *cursor = der;
    ApxCMSEnvelope *envelope = NULL; ApxCMSGCM *gcm = NULL;
    unsigned char *canonical = NULL, *canonical_gcm = NULL;
    int ok = 0;
    if (!der || !key_id || maximum > 1048576u ||
        length > LONG_MAX) return 0;
    if (length > maximum + 4096) return -1;
    envelope = d2i_ApxCMSEnvelope(NULL, &cursor, (long)length);
    if (!envelope || cursor != der + length ||
        OBJ_obj2nid(envelope->type) != NID_id_smime_ct_authEnvelopedData ||
        ASN1_INTEGER_get(envelope->envelope->version) != 0 ||
        sk_ApxCMSRecipient_num(envelope->envelope->recipients) != 1 ||
        ASN1_STRING_get_length(envelope->envelope->tag) != 16) goto done;
    ApxCMSORI *ori = sk_ApxCMSRecipient_value(envelope->envelope->recipients, 0);
    ApxCMSKEM *kem = ori->kem; ApxCMSEncrypted *encrypted = envelope->envelope->encrypted;
    if (OBJ_obj2nid(ori->type) != NID_id_smime_ori_kem ||
        ASN1_INTEGER_get(kem->version) != 0 ||
        ASN1_STRING_get_length(kem->key_id) != 32 ||
        CRYPTO_memcmp(ASN1_STRING_get0_data(kem->key_id), key_id, 32) ||
        !algorithm_is(kem->kem, NID_ML_KEM_768) ||
        ASN1_STRING_get_length(kem->kem_ciphertext) != 1088 ||
        !algorithm_is(kem->kdf, NID_HKDF_SHA256) ||
        ASN1_INTEGER_get(kem->kek_length) != 32 ||
        !algorithm_is(kem->wrap, NID_id_aes256_wrap) ||
        ASN1_STRING_get_length(kem->wrapped_key) != 40 ||
        OBJ_obj2nid(encrypted->type) != NID_pkcs7_data) goto done;
    const ASN1_OBJECT *oid = NULL; int parameter = -1; const void *parameters = NULL;
    X509_ALGOR_get0(&oid, &parameter, &parameters, encrypted->algorithm);
    if (!oid || OBJ_obj2nid(oid) != NID_aes_256_gcm ||
        parameter != V_ASN1_SEQUENCE || !parameters) goto done;
    const ASN1_STRING *sequence = parameters;
    const unsigned char *start = ASN1_STRING_get0_data(sequence); cursor = start;
    size_t parameter_length = ASN1_STRING_get_length(sequence);
    if (parameter_length > LONG_MAX) goto done;
    gcm = d2i_ApxCMSGCM(NULL, &cursor, (long)parameter_length);
    if (!gcm || cursor != start + parameter_length ||
        ASN1_STRING_get_length(gcm->nonce) != 12 || ASN1_INTEGER_get(gcm->tag_length) != 16) goto done;
    int gcm_length = i2d_ApxCMSGCM(gcm, &canonical_gcm);
    if (gcm_length <= 0 || (size_t)gcm_length != parameter_length ||
        memcmp(canonical_gcm, start, (size_t)parameter_length)) goto done;
    int canonical_length = i2d_ApxCMSEnvelope(envelope, &canonical);
    if (canonical_length <= 0 || (size_t)canonical_length != length ||
        memcmp(canonical, der, length)) goto done;
    ok = (size_t)ASN1_STRING_get_length(encrypted->ciphertext) <= maximum ? 1 : -1;
done:
    OPENSSL_free(canonical_gcm); OPENSSL_free(canonical);
    ApxCMSGCM_free(gcm); ApxCMSEnvelope_free(envelope);
    return ok;
}
