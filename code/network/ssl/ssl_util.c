/**
 *	@file	sslutil.c
 *
 *	@brief	SSL utilities function, like get certificate information.
 *	
 *	@author	Forrest.zhang	
 *
 *	@date
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/engine.h>
#include <openssl/err.h>

#include "gcc_common.h"
#include "dbg_common.h"

#include "ssl_util.h"


#define	_SU_VRY_FLAGS		\
	(SSL_VERIFY_PEER|SSL_VERIFY_CLIENT_ONCE|SSL_VERIFY_FAIL_IF_NO_PEER_CERT)
#define	_SU_CRL_FLAGS		\
	(X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL)

/**
 *	The SSL private data stored in SSL
 */
typedef struct ssl_info {
	ssl_ctx_t	*ctx;		/* ssl context object */
	ssl_sd_e	side;		/* client/server side */
	int		renegotiate;	/* enable/disable peer renegotiate */
	int		in_handshake;	/* in handshake process */
	int		established;	/* SSL is established */
	unsigned long	error;		/* error number using ERR_GET_REASON() */
} ssl_info_t;

static ENGINE		*_su_engrand;		/* Intel RDRAND engine */

/* the lock for openssl thread-safe */
static pthread_mutex_t	*_su_locks;

/**
 *	Send Alert message to peer. used in renegotiate.
 */
extern int ssl3_send_alert(SSL *s,int level, int desc);
extern SSL_CIPHER *ssl3_choose_cipher(SSL *ssl,STACK_OF(SSL_CIPHER) *clnt,
		                      STACK_OF(SSL_CIPHER) *srvr);

#ifndef OPENSSL_NO_DH

#define	_SU_DH_PRIME_MAX	((8192/8)*2 + 1)

typedef struct dh_gp_t {
	int		bits;
	const char	generator[8];
	const char	prime[_SU_DH_PRIME_MAX];
} dh_gp_t;

static const dh_gp_t _su_dhgps[] = 
{
	{
		512, "2",
		"9FDB8B8A" "004544F0" "045F1737" "D0BA2E0B" "274CDF1A" "9F588218"
		"FB435316" "A16E3741" "71FD19D8" "D8F37C39" "BF863FD6" "0E3E3006"
		"80A3030C" "6E4C3757" "D08F70E6" "AA871033"
	},

	{
		1024, "2",
		"FFFFFFFF" "FFFFFFFF" "C90FDAA2" "2168C234" "C4C6628B" "80DC1CD1"
		"29024E08" "8A67CC74" "020BBEA6" "3B139B22" "514A0879" "8E3404DD"
		"EF9519B3" "CD3A431B" "302B0A6D" "F25F1437" "4FE1356D" "6D51C245"
		"E485B576" "625E7EC6" "F44C42E9" "A637ED6B" "0BFF5CB6" "F406B7ED"
		"EE386BFB" "5A899FA5" "AE9F2411" "7C4B1FE6" "49286651" "ECE65381"
		"FFFFFFFF" "FFFFFFFF"
	},

	{
		1536, "2",
		"FFFFFFFF" "FFFFFFFF" "C90FDAA2" "2168C234" "C4C6628B" "80DC1CD1"
		"29024E08" "8A67CC74" "020BBEA6" "3B139B22" "514A0879" "8E3404DD"
		"EF9519B3" "CD3A431B" "302B0A6D" "F25F1437" "4FE1356D" "6D51C245"
		"E485B576" "625E7EC6" "F44C42E9" "A637ED6B" "0BFF5CB6" "F406B7ED"
		"EE386BFB" "5A899FA5" "AE9F2411" "7C4B1FE6" "49286651" "ECE45B3D"
		"C2007CB8" "A163BF05" "98DA4836" "1C55D39A" "69163FA8" "FD24CF5F"
		"83655D23" "DCA3AD96" "1C62F356" "208552BB" "9ED52907" "7096966D"
		"670C354E" "4ABC9804" "F1746C08" "CA237327" "FFFFFFFF" "FFFFFFFF"
	},

	{
		2048, "2",
		"FFFFFFFF" "FFFFFFFF" "C90FDAA2" "2168C234" "C4C6628B" "80DC1CD1"
		"29024E08" "8A67CC74" "020BBEA6" "3B139B22" "514A0879" "8E3404DD"
		"EF9519B3" "CD3A431B" "302B0A6D" "F25F1437" "4FE1356D" "6D51C245"
		"E485B576" "625E7EC6" "F44C42E9" "A637ED6B" "0BFF5CB6" "F406B7ED"
		"EE386BFB" "5A899FA5" "AE9F2411" "7C4B1FE6" "49286651" "ECE45B3D"
		"C2007CB8" "A163BF05" "98DA4836" "1C55D39A" "69163FA8" "FD24CF5F"
		"83655D23" "DCA3AD96" "1C62F356" "208552BB" "9ED52907" "7096966D"
		"670C354E" "4ABC9804" "F1746C08" "CA18217C" "32905E46" "2E36CE3B"
		"E39E772C" "180E8603" "9B2783A2" "EC07A28F" "B5C55DF0" "6F4C52C9"
		"DE2BCBF6" "95581718" "3995497C" "EA956AE5" "15D22618" "98FA0510"
		"15728E5A" "8AACAA68" "FFFFFFFF" "FFFFFFFF"
	},

	{
		3072, "2",
		"FFFFFFFF" "FFFFFFFF" "C90FDAA2" "2168C234" "C4C6628B" "80DC1CD1"
		"29024E08" "8A67CC74" "020BBEA6" "3B139B22" "514A0879" "8E3404DD"
		"EF9519B3" "CD3A431B" "302B0A6D" "F25F1437" "4FE1356D" "6D51C245"
		"E485B576" "625E7EC6" "F44C42E9" "A637ED6B" "0BFF5CB6" "F406B7ED"
		"EE386BFB" "5A899FA5" "AE9F2411" "7C4B1FE6" "49286651" "ECE45B3D"
		"C2007CB8" "A163BF05" "98DA4836" "1C55D39A" "69163FA8" "FD24CF5F"
		"83655D23" "DCA3AD96" "1C62F356" "208552BB" "9ED52907" "7096966D"
		"670C354E" "4ABC9804" "F1746C08" "CA18217C" "32905E46" "2E36CE3B"
		"E39E772C" "180E8603" "9B2783A2" "EC07A28F" "B5C55DF0" "6F4C52C9"
		"DE2BCBF6" "95581718" "3995497C" "EA956AE5" "15D22618" "98FA0510"
		"15728E5A" "8AAAC42D" "AD33170D" "04507A33" "A85521AB" "DF1CBA64"
		"ECFB8504" "58DBEF0A" "8AEA7157" "5D060C7D" "B3970F85" "A6E1E4C7"
		"ABF5AE8C" "DB0933D7" "1E8C94E0" "4A25619D" "CEE3D226" "1AD2EE6B"
		"F12FFA06" "D98A0864" "D8760273" "3EC86A64" "521F2B18" "177B200C"
		"BBE11757" "7A615D6C" "770988C0" "BAD946E2" "08E24FA0" "74E5AB31"
		"43DB5BFC" "E0FD108E" "4B82D120" "A93AD2CA" "FFFFFFFF" "FFFFFFFF"
	},

	{
		4096, "2",
		"FFFFFFFF" "FFFFFFFF" "C90FDAA2" "2168C234" "C4C6628B" "80DC1CD1"
		"29024E08" "8A67CC74" "020BBEA6" "3B139B22" "514A0879" "8E3404DD"
		"EF9519B3" "CD3A431B" "302B0A6D" "F25F1437" "4FE1356D" "6D51C245"
		"E485B576" "625E7EC6" "F44C42E9" "A637ED6B" "0BFF5CB6" "F406B7ED"
		"EE386BFB" "5A899FA5" "AE9F2411" "7C4B1FE6" "49286651" "ECE45B3D"
		"C2007CB8" "A163BF05" "98DA4836" "1C55D39A" "69163FA8" "FD24CF5F"
		"83655D23" "DCA3AD96" "1C62F356" "208552BB" "9ED52907" "7096966D"
		"670C354E" "4ABC9804" "F1746C08" "CA18217C" "32905E46" "2E36CE3B"
		"E39E772C" "180E8603" "9B2783A2" "EC07A28F" "B5C55DF0" "6F4C52C9"
		"DE2BCBF6" "95581718" "3995497C" "EA956AE5" "15D22618" "98FA0510"
		"15728E5A" "8AAAC42D" "AD33170D" "04507A33" "A85521AB" "DF1CBA64"
		"ECFB8504" "58DBEF0A" "8AEA7157" "5D060C7D" "B3970F85" "A6E1E4C7"
		"ABF5AE8C" "DB0933D7" "1E8C94E0" "4A25619D" "CEE3D226" "1AD2EE6B"
		"F12FFA06" "D98A0864" "D8760273" "3EC86A64" "521F2B18" "177B200C"
		"BBE11757" "7A615D6C" "770988C0" "BAD946E2" "08E24FA0" "74E5AB31"
		"43DB5BFC" "E0FD108E" "4B82D120" "A9210801" "1A723C12" "A787E6D7"
		"88719A10" "BDBA5B26" "99C32718" "6AF4E23C" "1A946834" "B6150BDA"
		"2583E9CA" "2AD44CE8" "DBBBC2DB" "04DE8EF9" "2E8EFC14" "1FBECAA6"
		"287C5947" "4E6BC05D" "99B2964F" "A090C3A2" "233BA186" "515BE7ED"
		"1F612970" "CEE2D7AF" "B81BDD76" "2170481C" "D0069127" "D5B05AA9"
		"93B4EA98" "8D8FDDC1" "86FFB7DC" "90A6C08F" "4DF435C9" "34063199"
		"FFFFFFFF" "FFFFFFFF"
	},

	{
		6144, "2",
		"FFFFFFFF" "FFFFFFFF" "C90FDAA2" "2168C234" "C4C6628B" "80DC1CD1" 
		"29024E08" "8A67CC74" "020BBEA6" "3B139B22" "514A0879" "8E3404DD" 
		"EF9519B3" "CD3A431B" "302B0A6D" "F25F1437" "4FE1356D" "6D51C245" 
		"E485B576" "625E7EC6" "F44C42E9" "A637ED6B" "0BFF5CB6" "F406B7ED" 
		"EE386BFB" "5A899FA5" "AE9F2411" "7C4B1FE6" "49286651" "ECE45B3D" 
		"C2007CB8" "A163BF05" "98DA4836" "1C55D39A" "69163FA8" "FD24CF5F"
		"83655D23" "DCA3AD96" "1C62F356" "208552BB" "9ED52907" "7096966D"
		"670C354E" "4ABC9804" "F1746C08" "CA18217C" "32905E46" "2E36CE3B" 
		"E39E772C" "180E8603" "9B2783A2" "EC07A28F" "B5C55DF0" "6F4C52C9" 
		"DE2BCBF6" "95581718" "3995497C" "EA956AE5" "15D22618" "98FA0510" 
		"15728E5A" "8AAAC42D" "AD33170D" "04507A33" "A85521AB" "DF1CBA64" 
		"ECFB8504" "58DBEF0A" "8AEA7157" "5D060C7D" "B3970F85" "A6E1E4C7" 
		"ABF5AE8C" "DB0933D7" "1E8C94E0" "4A25619D" "CEE3D226" "1AD2EE6B" 
		"F12FFA06" "D98A0864" "D8760273" "3EC86A64" "521F2B18" "177B200C"
		"BBE11757" "7A615D6C" "770988C0" "BAD946E2" "08E24FA0" "74E5AB31" 
		"43DB5BFC" "E0FD108E" "4B82D120" "A9210801" "1A723C12" "A787E6D7" 
		"88719A10" "BDBA5B26" "99C32718" "6AF4E23C" "1A946834" "B6150BDA" 
		"2583E9CA" "2AD44CE8" "DBBBC2DB" "04DE8EF9" "2E8EFC14" "1FBECAA6" 
		"287C5947" "4E6BC05D" "99B2964F" "A090C3A2" "233BA186" "515BE7ED" 
		"1F612970" "CEE2D7AF" "B81BDD76" "2170481C" "D0069127" "D5B05AA9" 
		"93B4EA98" "8D8FDDC1" "86FFB7DC" "90A6C08F" "4DF435C9" "34028492"
		"36C3FAB4" "D27C7026" "C1D4DCB2" "602646DE" "C9751E76" "3DBA37BD" 
		"F8FF9406" "AD9E530E" "E5DB382F" "413001AE" "B06A53ED" "9027D831" 
		"179727B0" "865A8918" "DA3EDBEB" "CF9B14ED" "44CE6CBA" "CED4BB1B" 
		"DB7F1447" "E6CC254B" "33205151" "2BD7AF42" "6FB8F401" "378CD2BF" 
		"5983CA01" "C64B92EC" "F032EA15" "D1721D03" "F482D7CE" "6E74FEF6" 
		"D55E702F" "46980C82" "B5A84031" "900B1C9E" "59E7C97F" "BEC7E8F3" 
		"23A97A7E" "36CC88BE" "0F1D45B7" "FF585AC5" "4BD407B2" "2B4154AA"
		"CC8F6D7E" "BF48E1D8" "14CC5ED2" "0F8037E0" "A79715EE" "F29BE328" 
		"06A1D58B" "B7C5DA76" "F550AA3D" "8A1FBFF0" "EB19CCB1" "A313D55C" 
		"DA56C9EC" "2EF29632" "387FE8D7" "6E3C0468" "043E8F66" "3F4860EE" 
		"12BF2D5B" "0B7474D6" "E694F91E" "6DCC4024" "FFFFFFFF" "FFFFFFFF"
	},

	{
		8192, "2",
		"FFFFFFFF" "FFFFFFFF" "C90FDAA2" "2168C234" "C4C6628B" "80DC1CD1"
		"29024E08" "8A67CC74" "020BBEA6" "3B139B22" "514A0879" "8E3404DD"
		"EF9519B3" "CD3A431B" "302B0A6D" "F25F1437" "4FE1356D" "6D51C245"
		"E485B576" "625E7EC6" "F44C42E9" "A637ED6B" "0BFF5CB6" "F406B7ED"
		"EE386BFB" "5A899FA5" "AE9F2411" "7C4B1FE6" "49286651" "ECE45B3D"
		"C2007CB8" "A163BF05" "98DA4836" "1C55D39A" "69163FA8" "FD24CF5F"
		"83655D23" "DCA3AD96" "1C62F356" "208552BB" "9ED52907" "7096966D"
		"670C354E" "4ABC9804" "F1746C08" "CA18217C" "32905E46" "2E36CE3B"
		"E39E772C" "180E8603" "9B2783A2" "EC07A28F" "B5C55DF0" "6F4C52C9"
		"DE2BCBF6" "95581718" "3995497C" "EA956AE5" "15D22618" "98FA0510"
		"15728E5A" "8AAAC42D" "AD33170D" "04507A33" "A85521AB" "DF1CBA64"
		"ECFB8504" "58DBEF0A" "8AEA7157" "5D060C7D" "B3970F85" "A6E1E4C7"
		"ABF5AE8C" "DB0933D7" "1E8C94E0" "4A25619D" "CEE3D226" "1AD2EE6B"
		"F12FFA06" "D98A0864" "D8760273" "3EC86A64" "521F2B18" "177B200C"
		"BBE11757" "7A615D6C" "770988C0" "BAD946E2" "08E24FA0" "74E5AB31"
		"43DB5BFC" "E0FD108E" "4B82D120" "A9210801" "1A723C12" "A787E6D7"
		"88719A10" "BDBA5B26" "99C32718" "6AF4E23C" "1A946834" "B6150BDA"
		"2583E9CA" "2AD44CE8" "DBBBC2DB" "04DE8EF9" "2E8EFC14" "1FBECAA6"
		"287C5947" "4E6BC05D" "99B2964F" "A090C3A2" "233BA186" "515BE7ED"
		"1F612970" "CEE2D7AF" "B81BDD76" "2170481C" "D0069127" "D5B05AA9"
		"93B4EA98" "8D8FDDC1" "86FFB7DC" "90A6C08F" "4DF435C9" "34028492"
		"36C3FAB4" "D27C7026" "C1D4DCB2" "602646DE" "C9751E76" "3DBA37BD"
		"F8FF9406" "AD9E530E" "E5DB382F" "413001AE" "B06A53ED" "9027D831"
		"179727B0" "865A8918" "DA3EDBEB" "CF9B14ED" "44CE6CBA" "CED4BB1B"
		"DB7F1447" "E6CC254B" "33205151" "2BD7AF42" "6FB8F401" "378CD2BF"
		"5983CA01" "C64B92EC" "F032EA15" "D1721D03" "F482D7CE" "6E74FEF6"
		"D55E702F" "46980C82" "B5A84031" "900B1C9E" "59E7C97F" "BEC7E8F3"
		"23A97A7E" "36CC88BE" "0F1D45B7" "FF585AC5" "4BD407B2" "2B4154AA"
		"CC8F6D7E" "BF48E1D8" "14CC5ED2" "0F8037E0" "A79715EE" "F29BE328"
		"06A1D58B" "B7C5DA76" "F550AA3D" "8A1FBFF0" "EB19CCB1" "A313D55C"
		"DA56C9EC" "2EF29632" "387FE8D7" "6E3C0468" "043E8F66" "3F4860EE"
		"12BF2D5B" "0B7474D6" "E694F91E" "6DBE1159" "74A3926F" "12FEE5E4"
		"38777CB6" "A932DF8C" "D8BEC4D0" "73B931BA" "3BC832B6" "8D9DD300"
		"741FA7BF" "8AFC47ED" "2576F693" "6BA42466" "3AAB639C" "5AE4F568"
		"3423B474" "2BF1C978" "238F16CB" "E39D652D" "E3FDB8BE" "FC848AD9"
		"22222E04" "A4037C07" "13EB57A8" "1A23F0C7" "3473FC64" "6CEA306B"
		"4BCBC886" "2F8385DD" "FA9D4B7F" "A2C087E8" "79683303" "ED5BDD3A"
		"062B3CF5" "B3A278A6" "6D2A13F8" "3F44F82D" "DF310EE0" "74AB6A36"
		"4597E899" "A0255DC1" "64F31CC5" "0846851D" "F9AB4819" "5DED7EA1"
		"B1D510BD" "7EE74D73" "FAF36BC3" "1ECFA268" "359046F4" "EB879F92"
		"4009438B" "481C6CD7" "889A002E" "D5EE382B" "C9190DA6" "FC026E47"
		"9558E447" "5677E9AA" "9E3050E2" "765694DF" "C81F56E8" "80B96E71"
		"60C980DD" "98EDD3DF" "FFFFFFFF" "FFFFFFFF"
	}
};
#define	_SU_DH_MAX	(sizeof(_su_dhgps) / sizeof(_su_dhgps[0]))

static DH	*_su_tmpdhs[_SU_DH_MAX];/* temp DH parameters */

/**
 *	Alloc temp DH key for DH key exchange.
 */
static DH * 
_su_tmpdh_alloc(int index)
{
	DH *dh = NULL;
	
	if (unlikely(index < 0 || index >= _SU_DH_MAX))
		ERR_RET(NULL, "invalid index %d\n", index);

	dh = DH_new();
	if (unlikely(!dh))
		ERR_RET(NULL, "DH_new failed\n");

	if (BN_hex2bn(&dh->p, _su_dhgps[index].prime) == 0) {
		DH_free(dh);
		ERR_RET(NULL, "set DH prime failed\n");
	}

	if (BN_hex2bn(&dh->g, _su_dhgps[index].generator) == 0) {
		DH_free(dh);
		ERR_RET(NULL, "set DH generator failed\n");
	}

	if (unlikely(DH_generate_key(dh) != 1)) {
		DH_free(dh);
		ERR_RET(NULL, "DH_generate_key failed\n");
	}

	return dh;
}

static void 
_su_tmpdh_init(void)
{
	int i;

	for (i = 0; i < _SU_DH_MAX; i++) {
		_su_tmpdhs[i] = _su_tmpdh_alloc(i);
	}
}

static void
_su_tmpdh_free(void)
{
	int i;

	for (i = 0; i < _SU_DH_MAX; i++) {
		if (_su_tmpdhs[i])
			DH_free(_su_tmpdhs[i]);
	}
}

/**
 *	Generate a temp DH key, prime length is @keylen.
 *
 * 	Return DH key if success, NULL on error.
 */
static DH * 
_su_tmpdh_cb(SSL *ssl, int is_export, int keylen)
{
	DH *dh = NULL;

	switch (keylen) {

	case 1024:
		dh = _su_tmpdhs[0];
		break;

	default:
		dh = _su_tmpdhs[0];
	};

	return dh;
}

#endif	/* end of !OPENSSL_NO_DH */


#ifndef	OPENSSL_NO_ECDH

static EC_KEY	*_su_tmpecdh;		/* temp ECDH function */

static EC_KEY * 
_su_tmpec_alloc(int oid)
{
	EC_KEY *ec;
	EC_GROUP *group;

	ec = EC_KEY_new();
	if (ec == NULL)
		ERR_RET(NULL, "EC_KEY_new failed\n");

	group = EC_GROUP_new_by_curve_name(oid);
	if (group == NULL) {
		EC_KEY_free(ec);
		ERR_RET(NULL, "EC_GROUP_new_by_curve_name failed\n");
	}

	EC_GROUP_set_asn1_flag(group, OPENSSL_EC_NAMED_CURVE);
	EC_GROUP_set_point_conversion_form(group, POINT_CONVERSION_UNCOMPRESSED);

	if (EC_KEY_set_group(ec, group) == 0) {
		EC_GROUP_free(group);
		EC_KEY_free(ec);
		ERR_RET(NULL, "EC_KEY_set_group failed\n");
	}

	if (!EC_KEY_generate_key(ec)) {
		EC_GROUP_free(group);
		EC_KEY_free(ec);
		ERR_RET(NULL, "EC_KEY_generate_key failed\n");
	}

	EC_GROUP_free(group);

	return ec;
}

/**
 *	Generate a temp EC key, nid @nid.
 *
 * 	Return EC key if success, NULL on error.
 */
static EC_KEY * 
_su_tmpec_cb(SSL *ssl, int is_export, int keylen)
{
	long opt;

	if (unlikely(!ssl))
		return NULL;

	opt = SSL_get_options(ssl);
	if (opt & SSL_OP_SINGLE_ECDH_USE)
		return _su_tmpec_alloc(NID_X9_62_prime256v1);
	else
		return _su_tmpecdh;
}

#endif	/* end of !OPENSSL_NO_ECDH */

static  RSA*            _su_tmprsas[SSL_KEY_MAX];

static void
_su_tmprsa_init(void)
{
	RSA *rsa;

	rsa = RSA_generate_key(512, RSA_F4, NULL, NULL);
	if (!rsa) {
		ERR("generate 512bit RSA failed\n");
	}
	_su_tmprsas[SSL_KEY_512] = rsa;

	rsa = RSA_generate_key(1024, RSA_F4, NULL, NULL);
	if (!rsa) {
		ERR("generate 1024bit RSA failed\n");
	}
	_su_tmprsas[SSL_KEY_1024] = rsa;
}

static void
_su_tmprsa_free(void)
{
	int i;

	for (i = 0; i < SSL_KEY_MAX; i++) {
		if (_su_tmprsas[i])
			RSA_free(_su_tmprsas[i]);
	}
}

/**
 *	Generate a temp RSA key, prime length is @keylen.
 *
 * 	Return RSA key if success, NULL on error.
 */
static RSA * 
_su_tmprsa_cb(SSL *ssl, int is_export, int keylen)
{
	RSA *rsa = NULL;

	rsa = _su_tmprsas[SSL_KEY_1024];

	switch (keylen) {

	case 512:
		rsa = _su_tmprsas[SSL_KEY_512];
		break;

	case 1024:
		rsa = _su_tmprsas[SSL_KEY_1024];
		break;
	}

	return rsa;
}

static int  
_su_set_rdrand(void)
{
	int rc = 0;

	//OPENSSL_cpuid_setup();
	ENGINE_load_rdrand();

	_su_engrand = ENGINE_by_id("rdrand");
	if (_su_engrand == NULL) 
		return 0;

	rc = ENGINE_init(_su_engrand);
	if (rc != 1) 
		ERR_RET(-1, "Engine rdrand init failed\n");
	
	rc = ENGINE_set_default(_su_engrand, ENGINE_METHOD_RAND);
	if (rc != 1)
		ERR_RET(-1, "Engine rdrand set default failed\n");

	return 0;
}

/**
 *	Thread ID for Openssl thread safe
 */
unsigned long
_su_thread_id(void)
{
	pthread_t id;

	id = pthread_self();

	return (unsigned long)id;
}

/**
 *	Lock for Openssl thread safe
 */
void
_su_lock_callback(int mode, int n, const char *file, int line)
{
	if (mode & CRYPTO_LOCK) 
		pthread_mutex_lock(&(_su_locks[n]));
	else 
		pthread_mutex_unlock(&(_su_locks[n]));
}

/**
 *	PEM format cert/privatekey file password callback function
 *
 *	Return password length if success, 0 if no password.
 */
static int 
_su_pem_password_cb(char *buf, int size, int rwflag, void *u)
{
	char *passwd;
	size_t plen;

	passwd = (char *)u;
	if (!passwd)
		return 0;

	plen = strlen(passwd);
	if (plen < 1 || plen > size)
                return 0;

        memcpy(buf, passwd, plen);

        return plen;
}

/**
 *	Verify peer certificate.
 *
 *	Return 1 if success, other value for verify failed.
 */
static int 
_su_vry_cb(int result, X509_STORE_CTX *xs)
{
	switch (xs->error) {

	/* no CRL file found */
	case X509_V_ERR_UNABLE_TO_GET_CRL:
		return 1;

	default:
		break;
	}

	return result;
}

/**
 *	Verify peer certificate but local have not CA and CRL.
 *
 *	Always return 1.
 */
int 
_su_novry_cb(int result, X509_STORE_CTX *xs)
{
	return 1;
}

static void 
_su_choose_sslv3_ciphers(ssl_ctx_t *sc, ssl_info_t *si, SSL *s)
{
	STACK_OF(SSL_CIPHER) *sk;
	char ciphers[SSL_CIPHER_MAX];

	if (unlikely(!sc || !si || !s))
		return;

	/* disable ECDH */
	snprintf(ciphers, sizeof(ciphers) - 1,
			"%s:!ECDH", sc->ciphers);
	SSL_set_cipher_list(s, ciphers);
	sk = SSL_get_ciphers(s);
	s->s3->tmp.new_cipher =
		ssl3_choose_cipher(s, s->session->ciphers, sk);

	/* if not cipher choosed, set one */
	if(s->s3->tmp.new_cipher == NULL) {

		/* record error into @s */
		SSLerr(SSL_F_SSL3_GET_CLIENT_HELLO, SSL_R_NO_SHARED_CIPHER);

		/* save error now avoid clear by Openssl API */
		si->error = ERR_peek_error();

		/* send alert message */
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_HANDSHAKE_FAILURE);

		/* Set write fd to -1 disable send Server-Hello */
		SSL_set_wfd(s, -1);

		/* set cipher make ssl can continue */
		SSL_set_cipher_list(s, "AES256-SHA");
		sk = SSL_get_ciphers(s);
		s->s3->tmp.new_cipher = sk_SSL_CIPHER_value(sk, 0);
	}
}

static void 
_su_disable_renegotiate(ssl_ctx_t *sc, ssl_info_t *si, SSL *s)
{

	if (unlikely(!sc || !si || !s))
		return;

	/* record error into @s */
	SSLerr(SSL_F_SSL3_GET_CLIENT_HELLO, SSL_R_NO_RENEGOTIATION);

	/* save error now avoid clear by Openssl API */
	si->error = ERR_peek_error();

	/* send alert message */
	ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_NO_RENEGOTIATION);

	/* Set write fd to -1 disable send Server-Hello */
	SSL_set_wfd(s, -1);
}

/**
 *	Print SSL info callback flow in SSL handshake.
 *
 *	No return.
 */
void 
_su_info_cb_print(const SSL *ssl, int type, int val)
{
	ssl_ctx_t *sc;
	ssl_info_t *si;
	const char *str;
	int side = type & ~SSL_ST_MASK;

	if (unlikely(!ssl)) {
		ERR("no SSL object\n");
		return;
	}
	
	/* check ssl_info object */
	si = SSL_get_app_data(ssl);
	if (unlikely(!si)) {
		ERR("no ssl_info object\n");
		return;
	}

	/* check ssl_ctx object */
	sc = si->ctx;
	if (unlikely(!sc)) {
		ERR("no ssl_ctx object\n");
		return;
	}

	if (side & SSL_ST_CONNECT) {
		str = "SSL_connect: ";
	}
	else if (side & SSL_ST_ACCEPT) {
		str = "SSL_accept: ";
	}
	else {
		str = "unknowed side: ";
	}
	
	if (type & SSL_CB_LOOP) {
		printf("LOOP:%s:%s\n", str, SSL_state_string_long(ssl));

		/* need disable ECDH when do SSLv3 handshake */
		if (ssl->version == SSL3_VERSION &&
		    (ssl->state == SSL3_ST_SR_CLNT_HELLO_A ||
		     ssl->state == SSL3_ST_SR_CLNT_HELLO_B ||
		     ssl->state == SSL3_ST_SR_CLNT_HELLO_C))
		{
			_su_choose_sslv3_ciphers(sc, si, (SSL *)ssl);
		}

	}
	else if (type & SSL_CB_ALERT) {
		str = (type & SSL_CB_READ) ? "read" : "write";
		printf("ALERT:%s:%s:%s\n", str, 
		       SSL_alert_type_string_long(val),
		       SSL_alert_desc_string_long(val));
	}
	else if (type & SSL_CB_EXIT) {
		if (val == 0) {
			printf("EXIT(%d):%s:failed in %s\n", val, str, 
			       SSL_state_string_long(ssl));
		}
		else if (val < 0) {
			printf("EXIT(%d):%s:error in %s\n", val, str, 
			       SSL_state_string_long(ssl));
		}
		else {
			printf("EXIT(%d):%s:state in %s\n", val, str, 
			       SSL_state_string_long(ssl));
		}
	}
	else if (type & SSL_CB_HANDSHAKE_START) {
		printf("====> HANDSHAKE_START\n");
		if (si->established && si->renegotiate == 0)
			_su_disable_renegotiate(sc, si, (SSL *)ssl);
		si->in_handshake = 1;
	}
	else if (type & SSL_CB_HANDSHAKE_DONE) {
		printf("<==== HANDSHAKE_DONE\n");
		si->established = 1;
		si->in_handshake = 0;
		si->renegotiate = sc->renegotiate;
	}
	else if (type & SSL_CB_READ) {
		printf("READ(%d):%s:%s\n", val, str, 
		       SSL_state_string_long(ssl));
	}
	else if (type & SSL_CB_WRITE) {
		printf("WRITE(%d):%s:%s\n", val, str, 
		       SSL_state_string_long(ssl));
	}
	else {
		return;
	}
}

/**
 *	SSL info callback for handle renegotiate check.
 *
 *	No return.
 */
void 
_su_info_cb(const SSL *ssl, int type, int val)
{
	ssl_ctx_t *sc;
	ssl_info_t *si;

	/* check ssl */
	if (unlikely(!ssl)) {
		ERR("no SSL object\n");
		return;
	}
	
	/* check ssl_info */
	si = SSL_get_app_data(ssl);
	if (unlikely(!si)) {
		ERR("no ssl_info object\n");
		return;
	}

	/* check ssl_ctx */
	sc = si->ctx;
	if (unlikely(!sc)) {
		ERR("no ssl_ctx object\n");
		return;
	}

	if (type & SSL_CB_LOOP) {
		/* need disable ECDH when do SSLv3 handshake */
		if (ssl->version == SSL3_VERSION &&
		    (ssl->state == SSL3_ST_SR_CLNT_HELLO_A ||
		     ssl->state == SSL3_ST_SR_CLNT_HELLO_B ||
		     ssl->state == SSL3_ST_SR_CLNT_HELLO_C))
		{
			_su_choose_sslv3_ciphers(sc, si, (SSL *)ssl);
		}
	}

	if (type & SSL_CB_HANDSHAKE_START) {
		if (si->established && si->renegotiate == 0) {
			_su_disable_renegotiate(sc, si, (SSL *)ssl);
		}

		si->in_handshake = 1;
	}
	else if (type & SSL_CB_HANDSHAKE_DONE) {
		si->established = 1;
		si->in_handshake = 0;
		/* reset renegotiate */
		si->renegotiate = sc->renegotiate;
	}
}

/**
 *	SSL SNI(server domain name indiction) callback.
 *
 *	Always return SSL_TLSEXT_ERR_OK.
 */
static int 
_su_sni_cb(SSL *ssl, int *ad, void *arg)
{
	ssl_ctx_t *sc;
	ssl_ctx_t *sni;
	ssl_info_t *si;
	const char *sname;

	if (unlikely(!ssl)) 
		ERR_RET(SSL_TLSEXT_ERR_NOACK, "not found ssl\n");
	
	si = SSL_get_app_data(ssl);
	if (unlikely(!si)) 
		ERR_RET(SSL_TLSEXT_ERR_NOACK, "not found ssl_info\n");
	
	sc = si->ctx;
	if (unlikely(!sc)) 
		ERR_RET(SSL_TLSEXT_ERR_NOACK, "not found ssl_ctx\n");

	sname = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
	if (!sname) {
		return SSL_TLSEXT_ERR_OK;
	}

	/* list SNI find matched domain */
	CBLIST_FOR_EACH(&sc->list, sni, list) {
		if (sni->domain[0] && strcmp(sni->domain, sname) == 0) {
			SSL_set_SSL_CTX(ssl, sni->ctx);
			if (sni->flags & SSL_VRY_ALL) {
				SSL_set_verify(ssl, _SU_VRY_FLAGS, _su_vry_cb);
			}
			else if (sni->flags & SSL_VRY_NONE) {
				SSL_set_verify(ssl, _SU_VRY_FLAGS, _su_novry_cb);
			}
			else {
				SSL_set_verify(ssl, SSL_VERIFY_NONE, NULL);
			}
			SSL_set_accept_state(ssl);
			break;
		}
	}

	return SSL_TLSEXT_ERR_OK;
}

/**
 *	Load X509 object from @certfile and return it.
 *
 *	Return X509 object if success, NULL on error.
 */
static X509 * 
_su_load_x509(const char *certfile)
{
	X509 *x509 = NULL;
	FILE *fp = NULL;
	
	if (unlikely(!certfile))
		ERR_RET(NULL, "invalid argument\n");

	fp = fopen(certfile, "r");
	if (unlikely(!fp))
		ERR_RET(NULL, "open certfile %s failed: %s\n", 
			    certfile, ERRSTR);

	/* try to decode PEM format certificate */
	x509 = PEM_read_X509(fp, NULL, NULL, NULL);
	if (x509) {
		fclose(fp);
		return x509;
	}
	fseek(fp, 0L, SEEK_SET);

	/* the DER format X509 certificate file */
	x509 = d2i_X509_fp(fp, NULL);

	fclose(fp);
	return x509;
}

/**
 *	Load a EVP_PKEY object from private key file @keyfile, if key file 
 *	is PEM format and encrypted, the password is stored in @password
 *
 *	Return EVP_PKEY object if success, NULL on error.
 */
static EVP_PKEY * 
_su_load_pkey(const char *keyfile, const char *password)
{
	EVP_PKEY *pkey;
	FILE *fp;
	pem_password_cb *cb = NULL;
	
	if (unlikely(!keyfile))
		ERR_RET(NULL, "invalid argument @keyfile\n");

	if (password)
		cb = _su_pem_password_cb;

	fp = fopen(keyfile, "r");
	if (unlikely(!fp))
		ERR_RET(NULL, "open keyfile %s failed: %s\n", 
			    keyfile, ERRSTR);

	/* try to read PEM format private key file */
	pkey = PEM_read_PrivateKey(fp, NULL, cb, (char *)password);
	if (pkey) {
		fclose(fp);
		return pkey;
	}
	fseek(fp, 0L, SEEK_SET);

	/* try to read DER format private key file */
	pkey = d2i_PrivateKey_fp(fp, NULL);

	fclose(fp);
	return pkey;
}

/**
 *	Load a X509_CRL object from CRL file @crlfile.
 *
 *	Return X509_CRL object if success, NULL on error.
 */
static X509_CRL * 
_su_load_x509_crl(const char *crlfile)
{
	X509_CRL *crl;
	FILE *fp = NULL;

	if (unlikely(!crlfile))
		ERR_RET(NULL, "invalid argument @crlfile\n");

	fp = fopen(crlfile, "r");
	if (unlikely(!fp))
		ERR_RET(NULL, "open crlfile %s failed: %s\n", 
			    crlfile, ERRSTR);

	crl = PEM_read_X509_CRL(fp, NULL, NULL, NULL);
	if (crl) {
		fclose(fp);
		return crl;
	}
	fseek(fp, 0L, SEEK_SET);
	
	crl = d2i_X509_CRL_fp(fp, NULL);
	fclose(fp);
	
	return crl;
}


ssl_ctx_t * 
ssl_ctx_alloc(ssl_sd_e side)
{
	size_t len;
	ssl_ctx_t *sc;
	unsigned char *sid;
	const SSL_METHOD *mt;

	if (unlikely(_su_locks == NULL))
		ERR_RET(NULL, "init pthread safe lock failed\n");

	sc = calloc(1, sizeof(ssl_ctx_t));
	if (unlikely(!sc))
		ERR_RET(NULL, "calloc failed: %s\n", ERRSTR);

	if (side == SSL_SD_CLIENT)
		mt = SSLv23_client_method();
	else if (side == SSL_SD_SERVER)
		mt = SSLv23_server_method();
	else {
		free(sc);
		ERR_RET(NULL, "invalid @side value %d\n", side);
	}
	
	sc->ctx = SSL_CTX_new(mt);
	if (unlikely(!sc->ctx)) {
		free(sc);
		ERR_RET(NULL, "SSL_CTX_new failed\n");
	}
	
	/* set auto-reply for renegoatiate */
	SSL_CTX_set_mode(sc->ctx, SSL_MODE_AUTO_RETRY);

	/* set all SSL bug options */
	SSL_CTX_set_options(sc->ctx, SSL_OP_ALL);

	/* set context id */
	sid = (unsigned char *)&sc;
	len = sizeof(sc);
	if (SSL_CTX_set_session_id_context(sc->ctx, sid, len) != 1) {
		SSL_CTX_free(sc->ctx);
		free(sc);
		ERR_RET(NULL, "SSL_CTX_set_session_id_context failed\n");
	}

	if (side == SSL_SD_SERVER) {

#ifndef	OPENSSL_NO_DH
		/* set tmp DH for support DH key exchange */
		SSL_CTX_set_tmp_dh_callback(sc->ctx, _su_tmpdh_cb);
		SSL_CTX_clear_options(sc->ctx, SSL_OP_SINGLE_DH_USE);
#endif

#ifndef	OPENSSL_NO_ECDH
		/* set tmp ECDH for support ECDH key exchange */
		SSL_CTX_set_tmp_ecdh_callback(sc->ctx, _su_tmpec_cb);
		SSL_CTX_clear_options(sc->ctx, SSL_OP_SINGLE_ECDH_USE);
#endif

		SSL_CTX_set_tmp_rsa_callback(sc->ctx, _su_tmprsa_cb);
	}

	/* set callback function for SNI */
	if (side == SSL_SD_SERVER) 
		SSL_CTX_set_tlsext_servername_callback(sc->ctx, _su_sni_cb);

	/* set info callback */
	SSL_CTX_set_info_callback(sc->ctx, _su_info_cb);

	SSL_CTX_set_cipher_list(sc->ctx, "DEFAULT");

	/* set side */
	sc->side = side;
	strncpy(sc->ciphers, "DEFAULT", SSL_CIPHER_MAX);

	/* enable renegotiate by default */
	sc->renegotiate = 0;

	CBLIST_INIT(&sc->list);

	return sc;
}

int 
ssl_ctx_set_protocol(ssl_ctx_t *sc, int protocol)
{
	long opt;
	long oldopt;

	if (unlikely(!sc || !sc->ctx))
		ERR_RET(-1, "invalid argument\n");

	/* get current options */
	opt = oldopt = SSL_CTX_get_options(sc->ctx);

	if ( !(protocol & SSL_V2)) {
		//printf("no v2\n");
		opt |= SSL_OP_NO_SSLv2;
	}

	if ( !(protocol & SSL_V3)) {
		//printf("no v3\n");
		opt |= SSL_OP_NO_SSLv3;
	}

	if ( !(protocol & SSL_V10)) {
		//printf("no 1.0\n");
		opt |= SSL_OP_NO_TLSv1;
	}

	if ( !(protocol & SSL_V11)) {
		//printf("no 1.1\n");
		opt |= SSL_OP_NO_TLSv1_1;
	}

	if ( !(protocol & SSL_V12)) {
		//printf("no 1.2\n");
		opt |= SSL_OP_NO_TLSv1_2;
	}

	if (opt != oldopt)
		SSL_CTX_set_options(sc->ctx, opt);


	if ((protocol & SSL_V1X) == 0)
		SSL_CTX_set_tmp_ecdh(sc->ctx, NULL);

	return 0;
}

int 
ssl_ctx_set_ciphers(ssl_ctx_t *sc, const char *ciphers)
{
	int ret;

	if (unlikely(!sc || !sc->ctx || !ciphers))
		ERR_RET(-1, "invalid argument\n");

	ret = SSL_CTX_set_cipher_list(sc->ctx, ciphers);
	if (unlikely(ret != 1))
		ERR_RET(-1, "set cipher list %s failed\n", ciphers);

	strncpy(sc->ciphers, ciphers, SSL_CIPHER_MAX);

	return 0;
}

int 
ssl_ctx_set_renegotiate(ssl_ctx_t *sc, int renegotiate)
{
	if (unlikely(!sc || !sc->ctx || renegotiate < 0)) 
		ERR_RET(-1, "invalid argument\n");

	sc->renegotiate = renegotiate;

	return 0;
}

int
ssl_ctx_set_pfs(ssl_ctx_t *sc, int pfs)
{
	if (unlikely(!sc || !sc->ctx))
		ERR_RET(-1, "invalid argument");
	
	if (sc->side != SSL_SD_SERVER)
		ERR_RET(-1, "only server side support PFS\n");

	if (pfs) {

#ifndef	OPENSSL_NO_DH
		/* set DH for DH key exchange */
		SSL_CTX_set_options(sc->ctx, SSL_OP_SINGLE_DH_USE);
#endif

#ifndef	OPENSSL_NO_ECDH
		/* set temp ECDH for ECDH key exchange */
		SSL_CTX_set_options(sc->ctx, SSL_OP_SINGLE_ECDH_USE);
#endif
	}
	else {
#ifndef	OPENSSL_NO_DH
		/* clear PFS support in DH key exchage */
		SSL_CTX_clear_options(sc->ctx, SSL_OP_SINGLE_DH_USE);
#endif

#ifndef	OPENSSL_NO_ECDH
		/* clear PFS support in ECDH key exchange */
		SSL_CTX_clear_options(sc->ctx, SSL_OP_SINGLE_ECDH_USE);
#endif
	}

	return 0;
}

int 
ssl_ctx_set_domain(ssl_ctx_t *sc, const char *domain)
{
	if (unlikely(!sc || !domain))
		ERR_RET(-1, "invalid argument\n");

	strncpy(sc->domain, domain, sizeof(sc->domain) - 1);

	return 0;
}

int 
ssl_ctx_add_sni(ssl_ctx_t *sc, ssl_ctx_t *sni)
{
	if (unlikely(!sc || !sni))
		ERR_RET(-1, "invalid argument\n");

	if (!sni->domain[0])
		ERR_RET(-1, "sni didn't have domain\n");

	CBLIST_ADD_TAIL(&sc->list, &sni->list);

	return 0;
}

int 
ssl_ctx_load_cert(ssl_ctx_t *sc, const char *certfile, 
		  const char *keyfile, const char *password)
{
	int ret;
	X509 *cert;
	EVP_PKEY *pkey;

	if (unlikely(!sc || !sc->ctx || !certfile || !keyfile))
		return -1;
	
	cert = _su_load_x509(certfile);
	if (unlikely(!cert))
		return -1;

	ret = SSL_CTX_use_certificate(sc->ctx, cert);
	if (unlikely(ret != 1)) {
		X509_free(cert);
		return -1;
	}

	pkey = _su_load_pkey(keyfile, password);
	if (unlikely(!pkey)) {
		X509_free(cert);
		return -1;
	}

	ret = SSL_CTX_use_PrivateKey(sc->ctx, pkey);
	if (unlikely(ret != 1)) {
		X509_free(cert);
		EVP_PKEY_free(pkey);
		return -1;
	}

	ret = SSL_CTX_check_private_key(sc->ctx);
	if (unlikely(ret != 1)) {
		X509_free(cert);
		EVP_PKEY_free(pkey);
		return -1;
	}

	X509_free(cert);
	EVP_PKEY_free(pkey);
	return 0;
}

int 
ssl_ctx_load_intmedcert(ssl_ctx_t *sc, const char *certfile)
{
	X509 *x509=NULL;

	if (!sc || !sc->ctx || !certfile)
		return -1;

	x509 = _su_load_x509(certfile);
	if (!x509) 
		return -1;

	if (SSL_CTX_add_extra_chain_cert(sc->ctx, x509) != 1) {
		X509_free(x509);
		ERR_RET(-1, "load intermedia certificate %s failed\n", 
			    certfile);
	}

	return 0;
}

int 
ssl_ctx_load_cacert(ssl_ctx_t *sc, const char *cafile)
{
	int ret;

	if (unlikely(!sc || !sc->ctx || !cafile))
		ERR_RET(-1, "invalid argument\n");
	
	ret = SSL_CTX_load_verify_locations(sc->ctx, (char *)cafile, NULL);
	if (unlikely(ret != 1))
		ERR_RET(-1, "load CA certificate %s failed\n", cafile);

	if (!(sc->flags & SSL_VRY_CA)) {
		SSL_CTX_set_verify_depth(sc->ctx, 100);
		sc->flags |= SSL_VRY_CA;
	}

	if (sc->flags & SSL_VRY_ALL) {
		SSL_CTX_set_verify(sc->ctx, _SU_VRY_FLAGS, _su_vry_cb);
		SSL_CTX_set_verify_depth(sc->ctx, 100);
	}

	return 0;
}

int 
ssl_ctx_load_crl(ssl_ctx_t *sc, const char *crlfile)
{
	int ret;
	X509_CRL *crl;
	X509_STORE *st;

	if (!sc || !sc->ctx || !crlfile)
		ERR_RET(-1, "invalid argument\n");
	
	crl = _su_load_x509_crl(crlfile);
	if (!crl)
		return -1;
	
	st = SSL_CTX_get_cert_store(sc->ctx);
	if (!st) {
		X509_CRL_free(crl);
		return -1;
	}

	ret = X509_STORE_add_crl(st, crl);
	if (unlikely(ret != 1)) {
		X509_CRL_free(crl);
		ERR_RET(-1, "load CRL certificate %s failed\n", 
			    crlfile);
	}
	
	if (!(sc->flags & SSL_VRY_CRL)) {
		sc->flags |= SSL_VRY_CRL;
		X509_STORE_set_flags(st, _SU_CRL_FLAGS);
	}

	if (!(sc->flags & SSL_VRY_ALL))
		SSL_CTX_set_verify(sc->ctx, _SU_VRY_FLAGS, _su_vry_cb);

	X509_CRL_free(crl);
	return 0;
}

int  
ssl_ctx_free(ssl_ctx_t *sc)
{
	int refcnt;
	ssl_ctx_t *sc1, *bak;

	if (unlikely(!sc)) 
		ERR_RET(-1, "invalid argument");

	refcnt = __sync_fetch_and_sub(&sc->refcnt, 1);

	/* only free when reference count is 0 */
	if (refcnt == 0) {
		CBLIST_FOR_EACH_SAFE(&sc->list, sc1, bak, list) { 
			CBLIST_DEL(&sc1->list);
			if (sc1->ctx)
				SSL_CTX_free(sc1->ctx);
			free(sc1);
		}
		if (sc->ctx)
			SSL_CTX_free(sc->ctx);

		free(sc);
	}

	return 0;
}

SSL * 
ssl_alloc(ssl_ctx_t *sc)
{
	int ret;
	SSL *ssl;
	ssl_info_t *si;

	if (unlikely(!sc || !sc->ctx))
		ERR_RET(NULL, "invalid argument\n");

	/* alloc new SSL object */
	ssl = SSL_new(sc->ctx);
	if (unlikely(!ssl))
		ERR_RET(NULL, "SSL_new failed\n");

	/* set domain name on client side */
	if (sc->side == SSL_SD_CLIENT && sc->domain[0]) {
		ret = SSL_set_tlsext_host_name(ssl, sc->domain);
		if (unlikely(ret != 1)) {
			SSL_free(ssl);
			ERR_RET(NULL, "SSL_set_tlsext_host_name failed\n");
		}
	}

	/* set ssl handsake state */
	if (sc->side == SSL_SD_CLIENT)
		SSL_set_connect_state(ssl);
	else
		SSL_set_accept_state(ssl);

	si = OPENSSL_malloc(sizeof(ssl_info_t));
	if (unlikely(!si)) {
		SSL_free(ssl);
		ERR_RET(NULL, "OPENSSL_malloc failed\n");
	}

	/* set ssl_info value */
	si->ctx = sc;
	si->side = sc->side;
	si->renegotiate = sc->renegotiate;
	si->established = 0;
	si->in_handshake = 0;
	si->error = 0;

	ret = SSL_set_app_data(ssl, si);
	if (unlikely(ret != 1)) {
		SSL_free(ssl);
		OPENSSL_free(si);
		ERR_RET(NULL, "SSL_set_app_data failed\n");
	}

	__sync_fetch_and_add(&sc->refcnt, 1);

	return ssl;
}

int 
ssl_set_fd(SSL *ssl, int fd)
{
	int ret;
	int oldfd;

	if (unlikely(!ssl || fd < 0))
		ERR_RET(-1, "invalid argument\n");

	oldfd = SSL_get_fd(ssl);
	if (unlikely(oldfd >= 0))
		ERR_RET(-1, "already set fd %d\n", oldfd);

	ret = SSL_set_fd(ssl, fd);
	if (unlikely(ret != 1))
		return -1;

	return 0;
}

int 
ssl_accept(SSL *ssl, int fd, ssl_wt_e *wait)
{
	int ret;
	int err;
	ssl_ctx_t *sc;
	ssl_info_t *si;

	if (unlikely(!ssl || fd < 0 || !wait))
		ERR_RET(-1, "invalid argument\n");;

	*wait = SSL_WT_NONE;

	si = SSL_get_app_data(ssl);
	if (unlikely(!si))
		ERR_RET(-1, "get app data failed\n");

	sc = si->ctx;
	if (unlikely(!sc))
		ERR_RET(-1, "get ssl_ctx failed\n");

	ret = ssl_set_fd(ssl, fd);
	if (unlikely(ret))
		ERR_RET(-1, "ssl_set_fd failed\n");

	ret = SSL_accept(ssl);
	
	if (ret == 1) {
		SSL_set_options(ssl, SSL_OP_NO_QUERY_MTU);
		return 0;
	}

	if (unlikely(ret == 0))
		ERR_RET(-1, "SSL accept IO failed\n");
	
	/* check error */
	err = SSL_get_error(ssl, ret);
	if (err == SSL_ERROR_WANT_READ)
		*wait = SSL_WT_READ;
	else if (err == SSL_ERROR_WANT_WRITE) 
		*wait = SSL_WT_WRITE;
	else {
		if (si->error == 0)
			si->error = ERR_peek_error();

		ERR_RET(-1, "SSL accept failed: %d(%s)\n", 
			    ERR_GET_REASON(si->error), 
			    ERR_reason_error_string(si->error));
	}

	return 0;
}

int 
ssl_connect(SSL *ssl, int fd, ssl_wt_e *wait)
{
	int ret;
	int err;
	ssl_ctx_t *sc;
	ssl_info_t *si;

	if (unlikely(!ssl || fd < 0 || !wait))
		ERR_RET(-1, "invalid argument\n");

	*wait = SSL_WT_NONE;

	si = SSL_get_app_data(ssl);
	if (!si)
		ERR_RET(-1, "get app data failed\n");

	sc = si->ctx;
	if (!sc)
		ERR_RET(-1, "get ssl_ctx failed\n");

	ret = ssl_set_fd(ssl, fd);
	if (unlikely(ret))
		ERR_RET(-1, "ssl_set_fd failed\n");

	ret = SSL_connect(ssl);

	/* connect success */
	if (ret == 1) {
		SSL_set_options(ssl, SSL_OP_NO_QUERY_MTU);
		return 0;
	}

	/* low-layer I/O closed or SSL shutdowned */
	if (unlikely(ret == 0))
		ERR_RET(-1, "SSL connect IO failed\n");

	/* check error */
	err = SSL_get_error(ssl, ret);
	if (err == SSL_ERROR_WANT_READ)
		*wait = SSL_WT_READ;
	else if (err == SSL_ERROR_WANT_WRITE)
		*wait = SSL_WT_WRITE;
	else {
		if (si->error == 0)
			si->error = ERR_peek_error();

		ERR_RET(-1, "SSL connect failed: %d(%s)\n", 
			    ERR_GET_REASON(si->error), 
			    ERR_reason_error_string(si->error));
	}

	return 0;
}

int 
ssl_handshake(SSL *ssl, ssl_wt_e *wait)
{
	int ret;
	int err;
	ssl_ctx_t *sc;
	ssl_info_t *si;

	if (unlikely(!ssl || !wait)) 
		ERR_RET(-1, "invalid argument\n");

	*wait = SSL_WT_NONE;

	si = SSL_get_app_data(ssl);
	if (unlikely(!si))
		ERR_RET(-1, "get app data failed\n");

	sc = si->ctx;
	if (unlikely(!sc))
		ERR_RET(-1, "get ssl_ctx failed\n");

	ret = SSL_do_handshake(ssl);
	
	if (ret == 1) {
		/* server side in renegotiate */
		if (si->in_handshake &&
		    si->renegotiate && 
		    si->side == SSL_SD_SERVER) 
		{
			printf("server side call ssl_handshake\n");
			if (!(ssl->state & SSL_ST_ACCEPT)) {
				ssl->state = SSL_ST_ACCEPT;
				return ssl_handshake(ssl, wait);
			}
		}

		SSL_set_options(ssl, SSL_OP_NO_QUERY_MTU);
		SSL_set_mtu(ssl, 3000);
		return 0;
	}

	if (unlikely(ret == 0)) 
		ERR_RET(-1, "SSL handshake IO failed\n");

	/* check error */
	err = SSL_get_error(ssl, ret);
	if (err == SSL_ERROR_WANT_READ)
		*wait = SSL_WT_READ;
	else if (err == SSL_ERROR_WANT_WRITE)
		*wait = SSL_WT_WRITE;
	else {
		if (si->error == 0)
			si->error = ERR_peek_error();

		ERR_RET(-1, "SSL handshake failed: %d(%s)\n", 
			    ERR_GET_REASON(si->error), 
			    ERR_reason_error_string(si->error));
	}

	return 0;
}

int 
ssl_renegotiate(SSL *ssl, ssl_wt_e *wait)
{
	int ret;
	ssl_ctx_t *sc;
	ssl_info_t *si;

	if (unlikely(!ssl || !wait)) 
		ERR_RET(-1, "invalid argument\n");

	*wait = SSL_WT_NONE;

	si = SSL_get_app_data(ssl);
	if (unlikely(!si))
		ERR_RET(-1, "get app data failed\n");

	sc = si->ctx;
	if (unlikely(!sc))
		ERR_RET(-1, "get ssl_ctx failed\n");
	
	/* enable renegotiate caused by self */
	si->renegotiate = 1;

	/* do SSL renegotiate */
	ret = SSL_renegotiate(ssl);
	if (unlikely(ret < 1)) 
		ERR_RET(-1, "SSL renegotiate failed\n");

	/* call SSL handshake finished work */
	return ssl_handshake(ssl, wait);
}

int 
ssl_recv(SSL *ssl, void *buf, int len, int *closed, int *handshake)
{
	int n;
	int err;
	ssl_ctx_t *sc;
	ssl_info_t *si;

	if (unlikely(!ssl || !buf || len < 1 || !closed || !handshake))
		ERR_RET(-1, "invalid argument");

	*closed = 0;
	*handshake = 0;

	/* check renegotiate */
	si = SSL_get_app_data(ssl);
	if (unlikely(!si)) {
		ERR_RET(-1, "get app data failed\n");
	}
	sc = si->ctx;
	if (unlikely(!sc)) {
		ERR_RET(-1, "get ssl_ctx failed\n");
	}

	n = SSL_read(ssl, buf, len);

	if (si->in_handshake) {
		if (sc->renegotiate == 0 && si->renegotiate == 0)
			ERR_RET(-1, "peer renegotiate not enabled\n");
		*handshake = 1;
		return 0;
	}
	
	/* recv success */
	if (n > 0)
		return n;

	if (n == 0) {
		/* low-layer I/O failed, or something I don't know */
		*closed = 1;
		return 0;
	}

	/* check blocked by read */
	err = SSL_get_error(ssl, n);
	if (err != SSL_ERROR_WANT_READ) {
		ERR_RET(-1, "SSL recv error: %d\n", err);
	}

	return n;
}

int 
ssl_send(SSL *ssl, const void *buf, int len)
{
	int n;
	int err;
	int ret;
	ssl_ctx_t *sc;
	ssl_info_t *si;

	if (unlikely(!ssl || !buf || len < 0))
		ERR_RET(-1, "invalid argument\n");

	/* check renegotiate */
	si = SSL_get_app_data(ssl);
	if (unlikely(!si)) 
		ERR_RET(-1, "get app data failed\n");
	
	sc = si->ctx;
	if (unlikely(!sc)) 
		ERR_RET(-1, "get ssl_ctx failed\n");

	/* not enable renegotiate but renegotiate occured */
	if (sc->renegotiate == 0 && 
	    si->renegotiate == 0 && 
	    si->in_handshake) 
	{
		ERR_RET(-1, "renegotiate not enabled\n");
	}

	n = SSL_write(ssl, buf, len);
	if (n == 0) {
		/* the SSL connection is closed */
		ret = SSL_get_shutdown(ssl);
		if (ret & (SSL_RECEIVED_SHUTDOWN | SSL_SENT_SHUTDOWN)) {
			return -1;
		}

		return -1;
	}
	else if (n < 0) {
		/* the SSL record or finished */
		err = SSL_get_error(ssl, n);
		if (err == SSL_ERROR_WANT_READ) {
			return 0;
		}
		/* the SSL renegotiate not finished */
		else if (err == SSL_ERROR_WANT_WRITE) {
			return 0;
		}
		
		return -1;
	}

	return n;
}

int 
ssl_shutdown(SSL *ssl, ssl_wt_e *wait)
{
	int ret;
	int err;

	if (unlikely(!ssl || !wait))
		ERR_RET(-1, "invalid argument\n");

	*wait = SSL_WT_NONE;

	ret = SSL_shutdown(ssl);

	if (ret == 1)
		return 0;

	if (ret == 0) {
		*wait = SSL_WT_WRITE;
		return 0;
	}

	err = SSL_get_error(ssl, ret);
	if (err == SSL_ERROR_WANT_READ)
		*wait = SSL_WT_READ;
	else if (err == SSL_ERROR_WANT_WRITE)
		*wait = SSL_WT_WRITE;
	else 
		ERR_RET(-1, "SSL shutdown failed\n");

	return 0;
}

int 
ssl_free(SSL *ssl)
{
	int refcnt;
	ssl_ctx_t *sc, *sni, *bak;
	ssl_info_t *si;

	if (unlikely(!ssl))
		ERR_RET(-1, "invalid argument\n");

	/* check renegotiate */
	si = SSL_get_app_data(ssl);
	if (unlikely(!si))
		ERR_RET(-1, "get app data failed\n");
	
	sc = si->ctx;
	if (unlikely(!sc))
		ERR_RET(-1, "get ssl_ctx failed\n");

	OPENSSL_free(si);
	SSL_free(ssl);

	refcnt = __sync_fetch_and_sub(&sc->refcnt, 1);
	if (refcnt == 0) {
		/* free SNI first */
		CBLIST_FOR_EACH_SAFE(&sc->list, sni, bak, list) { 
			CBLIST_DEL(&sni->list);
			if (sni->ctx)
				SSL_CTX_free(sni->ctx);
			free(sni);
		}
		
		/* free SSL context */
		if (sc->ctx)
			SSL_CTX_free(sc->ctx);

		/* free ssl_ctx */
		free(sc);
	}

	return 0;
}

unsigned long  
ssl_get_error(const SSL *ssl)
{
	int vryerr;
	ssl_ctx_t *sc;
	ssl_info_t *si;

	if (unlikely(!ssl))
		ERR_RET(-1, "invalid argument\n");
	
	/* get ssl_info object */
	si = SSL_get_app_data(ssl);
	if (unlikely(!si))
		ERR_RET(-1, "get app data failed\n");
	
	/* get ssl_ctx object */
	sc = si->ctx;
	if (unlikely(!sc))
		ERR_RET(-1, "get ssl_ctx failed\n");

	/* get verify result */
	vryerr = SSL_get_verify_result(ssl);
	if (vryerr != X509_V_OK)
		return vryerr;
	else 
		return si->error;
}

const char * 
ssl_get_errstr(unsigned long error)
{
	/* verify error */
	if (error < 0xfff) 
		return X509_verify_cert_error_string((long)error);
	else
		return ERR_reason_error_string(error);
}

static void __attribute__((constructor)) 
_ssl_global_init(void)
{
	int i;
	int nlock;

	CRYPTO_malloc_init();
	SSL_library_init();
	ERR_load_BIO_strings();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();

	/* alloc locks for thread safe */
	nlock = CRYPTO_num_locks();
	_su_locks = malloc(nlock * sizeof(pthread_mutex_t));
	if (_su_locks) {
		for (i = 0; i < nlock; i++) 
			pthread_mutex_init(&(_su_locks[i]), NULL);
	}

	CRYPTO_set_id_callback(_su_thread_id);
	CRYPTO_set_locking_callback(_su_lock_callback);

	/* set Intel RDRAND engine to improve RAND performance */
	if (_su_set_rdrand()) {
		/* Generate rand seed for PKEY */
		srand(time(NULL));
	}

#ifndef	OPENSSL_NO_DH
	/* Alloc temp DH key */
	_su_tmpdh_init();
#endif

#ifndef	OPENSSL_NO_ECDH
	/* ALloc tmpe ECDH key */
	_su_tmpecdh = _su_tmpec_alloc(NID_X9_62_prime256v1);
#endif

	_su_tmprsa_init();
}

static void __attribute__((destructor))
_ssl_global_free(void)
{
	_su_tmpdh_free();

	_su_tmprsa_free();

	if (_su_tmpecdh)
		EC_KEY_free(_su_tmpecdh);

	if (_su_engrand) {
		ENGINE_finish(_su_engrand);
		ENGINE_free(_su_engrand);
		ENGINE_cleanup();
	}

	if (_su_locks) 
		free(_su_locks);

	EVP_cleanup();
	ERR_free_strings();
}

