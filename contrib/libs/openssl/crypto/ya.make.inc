
PEERDIR(
    contrib/libs/zlib
    library/cpp/sanitizer/include
)

ADDINCL(
    contrib/libs/openssl
    contrib/libs/openssl/crypto
    contrib/libs/openssl/crypto/ec/curve448
    contrib/libs/openssl/crypto/ec/curve448/arch_32
    contrib/libs/openssl/crypto/modes
    contrib/libs/openssl/include
)

CFLAGS(-DOPENSSL_BUILD=1)

IF (OS_LINUX)
    IF (ARCH_ARM64)
        SET(LINUX_ARM64 yes)
    ELSEIF (ARCH_ARM7)
        SET(LINUX_ARMV7 yes)
    ELSEIF (ARCH_X86_64)
        SET(LINUX_X86_64 yes)
    ENDIF()
ENDIF()

IF (OS_IOS)
    IF (ARCH_ARM64)
        SET(IOS_ARM64 yes)
    ELSEIF (ARCH_ARM7)
        SET(IOS_ARMV7 yes)
    ELSEIF (ARCH_X86_64)
        SET(IOS_X86_64 yes)
    ELSEIF (ARCH_I386)
        SET(IOS_I386 yes)
    ENDIF()
ENDIF()

IF (OS_ANDROID)
    IF (ARCH_ARM64)
        SET(ANDROID_ARM64 yes)
    ELSEIF (ARCH_ARM7)
        SET(ANDROID_ARMV7 yes)
    ELSEIF (ARCH_X86_64)
        SET(ANDROID_X86_64 yes)
    ELSEIF (ARCH_I686)
        SET(ANDROID_I686 yes)
    ENDIF()
ENDIF()

IF (OS_WINDOWS)
    IF (ARCH_X86_64)
        SET(WINDOWS_X86_64 yes)
    ELSEIF (ARCH_I686)
        SET(WINDOWS_I686 yes)
    ENDIF()
ENDIF()

IF (OS_DARWIN AND ARCH_ARM64)
    SET(DARWIN_ARM64 yes)
ENDIF()

NO_COMPILER_WARNINGS()

NO_RUNTIME()

SUPPRESSIONS(ubsan.supp)

IF (NOT OS_EMSCRIPTEN)
  CFLAGS(
      -DOPENSSL_BN_ASM_MONT
      -DOPENSSL_CPUID_OBJ
      -DSHA1_ASM
      -DSHA256_ASM
      -DSHA512_ASM
  )
ENDIF()

CFLAGS(
    -DZLIB
)

IF (NOT OS_IOS AND NOT DARWIN_ARM64)
    CFLAGS(
        -DDSO_NONE
        -DAESNI_ASM
    )
ENDIF()

IF (NOT WINDOWS_I686 AND NOT OS_EMSCRIPTEN)
    CFLAGS(
        -DECP_NISTZ256_ASM
        -DPOLY1305_ASM
    )
ENDIF()

IF (NOT IOS_I386 AND NOT ANDROID_I686 AND NOT WINDOWS_I686)
    CFLAGS(
        -DKECCAK1600_ASM
    )
ENDIF()

IF (NOT IOS_ARMV7 AND NOT ANDROID_ARMV7 AND NOT LINUX_ARMV7)
    CFLAGS(
        -DVPAES_ASM
    )
ENDIF()

IF (NOT OS_WINDOWS)
    CFLAGS(
        -DENGINESDIR=\"/usr/local/lib/engines-1.1\"
        -DOPENSSLDIR=\"/usr/local/ssl\"
    )
ENDIF()

IF (OS_DARWIN AND ARCH_X86_64 OR OS_LINUX AND ARCH_X86_64 OR OS_WINDOWS AND ARCH_X86_64)
    CFLAGS(
        -DGHASH_ASM
        -DL_ENDIAN
        -DMD5_ASM
        -DOPENSSL_BN_ASM_GF2m
        -DOPENSSL_BN_ASM_MONT5
        -DOPENSSL_IA32_SSE2
        -DRC4_ASM
        -DX25519_ASM
    )
ENDIF()

IF (OS_LINUX AND ARCH_AARCH64 OR OS_LINUX AND ARCH_X86_64)
    CFLAGS(
        -DOPENSSL_USE_NODELETE
    )
ENDIF()

IF (OS_DARWIN AND ARCH_ARM64)
    CFLAGS(
        -DL_ENDIAN
        -DOPENSSL_PIC
    )
ENDIF()

IF (OS_WINDOWS)
    IF (ARCH_X86_64)
        CFLAGS(
            -DENGINESDIR="\"C:\\\\Program\ Files\\\\OpenSSL\\\\lib\\\\engines-1_1\""
            -DOPENSSLDIR="\"C:\\\\Program\ Files\\\\Common\ Files\\\\SSL\""
        )
    ELSEIF (ARCH_I386)
        CFLAGS(
            -DENGINESDIR="\"C:\\\\Program\ Files\ \(x86\)\\\\OpenSSL\\\\lib\\\\engines-1_1\""
            -DOPENSSLDIR="\"C:\\\\Program\ Files\ \(x86\)\\\\Common\ Files\\\\SSL\""
        )
    ENDIF()
    CFLAGS(
        -DOPENSSL_SYS_WIN32
        /GF
    )
ENDIF()

IF (SANITIZER_TYPE == memory)
    CFLAGS(
        -DPURIFY
    )
ENDIF()

IF (MUSL)
    CFLAGS(
        -DOPENSSL_NO_ASYNC
    )
ENDIF()

IF (ARCH_TYPE_32)
    CFLAGS(
        -DOPENSSL_NO_EC_NISTP_64_GCC_128
    )
ENDIF()

IF (ARCH_X86_64 AND NOT MSVC)
    SET_APPEND(
        SFLAGS
        -mavx512bw
        -mavx512ifma
        -mavx512vl
    )
ENDIF()

SRCS(
    aes/aes_cbc.c
    aes/aes_cfb.c
    aes/aes_ecb.c
    aes/aes_ige.c
    aes/aes_misc.c
    aes/aes_ofb.c
    aes/aes_wrap.c
    aria/aria.c
    asn1/a_bitstr.c
    asn1/a_d2i_fp.c
    asn1/a_digest.c
    asn1/a_dup.c
    asn1/a_gentm.c
    asn1/a_i2d_fp.c
    asn1/a_int.c
    asn1/a_mbstr.c
    asn1/a_object.c
    asn1/a_octet.c
    asn1/a_print.c
    asn1/a_sign.c
    asn1/a_strex.c
    asn1/a_strnid.c
    asn1/a_time.c
    asn1/a_type.c
    asn1/a_utctm.c
    asn1/a_utf8.c
    asn1/a_verify.c
    asn1/ameth_lib.c
    asn1/asn1_err.c
    asn1/asn1_gen.c
    asn1/asn1_item_list.c
    asn1/asn1_lib.c
    asn1/asn1_par.c
    asn1/asn_mime.c
    asn1/asn_moid.c
    asn1/asn_mstbl.c
    asn1/asn_pack.c
    asn1/bio_asn1.c
    asn1/bio_ndef.c
    asn1/d2i_pr.c
    asn1/d2i_pu.c
    asn1/evp_asn1.c
    asn1/f_int.c
    asn1/f_string.c
    asn1/i2d_pr.c
    asn1/i2d_pu.c
    asn1/n_pkey.c
    asn1/nsseq.c
    asn1/p5_pbe.c
    asn1/p5_pbev2.c
    asn1/p5_scrypt.c
    asn1/p8_pkey.c
    asn1/t_bitst.c
    asn1/t_pkey.c
    asn1/t_spki.c
    asn1/tasn_dec.c
    asn1/tasn_enc.c
    asn1/tasn_fre.c
    asn1/tasn_new.c
    asn1/tasn_prn.c
    asn1/tasn_scn.c
    asn1/tasn_typ.c
    asn1/tasn_utl.c
    asn1/x_algor.c
    asn1/x_bignum.c
    asn1/x_info.c
    asn1/x_int64.c
    asn1/x_long.c
    asn1/x_pkey.c
    asn1/x_sig.c
    asn1/x_spki.c
    asn1/x_val.c
    async/arch/async_null.c
    async/arch/async_posix.c
    async/arch/async_win.c
    async/async.c
    async/async_err.c
    async/async_wait.c
    bf/bf_cfb64.c
    bf/bf_ecb.c
    bf/bf_ofb64.c
    bf/bf_skey.c
    bio/b_addr.c
    bio/b_dump.c
    bio/b_print.c
    bio/b_sock.c
    bio/b_sock2.c
    bio/bf_buff.c
    bio/bf_lbuf.c
    bio/bf_nbio.c
    bio/bf_null.c
    bio/bio_cb.c
    bio/bio_err.c
    bio/bio_lib.c
    bio/bio_meth.c
    bio/bss_acpt.c
    bio/bss_bio.c
    bio/bss_conn.c
    bio/bss_dgram.c
    bio/bss_fd.c
    bio/bss_file.c
    bio/bss_log.c
    bio/bss_mem.c
    bio/bss_null.c
    bio/bss_sock.c
    blake2/blake2b.c
    blake2/blake2s.c
    blake2/m_blake2b.c
    blake2/m_blake2s.c
    bn/bn_add.c
    bn/bn_blind.c
    bn/bn_const.c
    bn/bn_ctx.c
    bn/bn_depr.c
    bn/bn_dh.c
    bn/bn_div.c
    bn/bn_err.c
    bn/bn_exp.c
    bn/bn_exp2.c
    bn/bn_gcd.c
    bn/bn_gf2m.c
    bn/bn_intern.c
    bn/bn_kron.c
    bn/bn_lib.c
    bn/bn_mod.c
    bn/bn_mont.c
    bn/bn_mpi.c
    bn/bn_mul.c
    bn/bn_nist.c
    bn/bn_prime.c
    bn/bn_print.c
    bn/bn_rand.c
    bn/bn_recp.c
    bn/bn_shift.c
    bn/bn_sqr.c
    bn/bn_sqrt.c
    bn/bn_srp.c
    bn/bn_word.c
    bn/bn_x931p.c
    buffer/buf_err.c
    buffer/buffer.c
    camellia/cmll_cfb.c
    camellia/cmll_ctr.c
    camellia/cmll_ecb.c
    camellia/cmll_ofb.c
    cast/c_cfb64.c
    cast/c_ecb.c
    cast/c_enc.c
    cast/c_ofb64.c
    cast/c_skey.c
    cmac/cm_ameth.c
    cmac/cm_pmeth.c
    cmac/cmac.c
    cms/cms_asn1.c
    cms/cms_att.c
    cms/cms_cd.c
    cms/cms_dd.c
    cms/cms_enc.c
    cms/cms_env.c
    cms/cms_err.c
    cms/cms_ess.c
    cms/cms_io.c
    cms/cms_kari.c
    cms/cms_lib.c
    cms/cms_pwri.c
    cms/cms_sd.c
    cms/cms_smime.c
    comp/c_zlib.c
    comp/comp_err.c
    comp/comp_lib.c
    conf/conf_api.c
    conf/conf_def.c
    conf/conf_err.c
    conf/conf_lib.c
    conf/conf_mall.c
    conf/conf_mod.c
    conf/conf_sap.c
    conf/conf_ssl.c
    cpt_err.c
    cryptlib.c
    ct/ct_b64.c
    ct/ct_err.c
    ct/ct_log.c
    ct/ct_oct.c
    ct/ct_policy.c
    ct/ct_prn.c
    ct/ct_sct.c
    ct/ct_sct_ctx.c
    ct/ct_vfy.c
    ct/ct_x509v3.c
    ctype.c
    cversion.c
    des/cbc_cksm.c
    des/cbc_enc.c
    des/cfb64ede.c
    des/cfb64enc.c
    des/cfb_enc.c
    des/ecb3_enc.c
    des/ecb_enc.c
    des/fcrypt.c
    des/ofb64ede.c
    des/ofb64enc.c
    des/ofb_enc.c
    des/pcbc_enc.c
    des/qud_cksm.c
    des/rand_key.c
    des/set_key.c
    des/str2key.c
    des/xcbc_enc.c
    dh/dh_ameth.c
    dh/dh_asn1.c
    dh/dh_check.c
    dh/dh_depr.c
    dh/dh_err.c
    dh/dh_gen.c
    dh/dh_kdf.c
    dh/dh_key.c
    dh/dh_lib.c
    dh/dh_meth.c
    dh/dh_pmeth.c
    dh/dh_prn.c
    dh/dh_rfc5114.c
    dh/dh_rfc7919.c
    dsa/dsa_ameth.c
    dsa/dsa_asn1.c
    dsa/dsa_depr.c
    dsa/dsa_err.c
    dsa/dsa_gen.c
    dsa/dsa_key.c
    dsa/dsa_lib.c
    dsa/dsa_meth.c
    dsa/dsa_ossl.c
    dsa/dsa_pmeth.c
    dsa/dsa_prn.c
    dsa/dsa_sign.c
    dsa/dsa_vrf.c
    dso/dso_dl.c
    dso/dso_err.c
    dso/dso_lib.c
    dso/dso_openssl.c
    dso/dso_vms.c
    dso/dso_win32.c
    ebcdic.c
    ec/curve25519.c
    ec/curve448/arch_32/f_impl.c
    ec/curve448/curve448.c
    ec/curve448/curve448_tables.c
    ec/curve448/eddsa.c
    ec/curve448/f_generic.c
    ec/curve448/scalar.c
    ec/ec2_oct.c
    ec/ec2_smpl.c
    ec/ec_ameth.c
    ec/ec_asn1.c
    ec/ec_check.c
    ec/ec_curve.c
    ec/ec_cvt.c
    ec/ec_err.c
    ec/ec_key.c
    ec/ec_kmeth.c
    ec/ec_lib.c
    ec/ec_mult.c
    ec/ec_oct.c
    ec/ec_pmeth.c
    ec/ec_print.c
    ec/ecdh_kdf.c
    ec/ecdh_ossl.c
    ec/ecdsa_ossl.c
    ec/ecdsa_sign.c
    ec/ecdsa_vrf.c
    ec/eck_prn.c
    ec/ecp_mont.c
    ec/ecp_nist.c
    ec/ecp_nistp224.c
    ec/ecp_nistp256.c
    ec/ecp_nistp521.c
    ec/ecp_nistputil.c
    ec/ecp_oct.c
    ec/ecp_smpl.c
    ec/ecx_meth.c
    engine/eng_all.c
    engine/eng_cnf.c
    engine/eng_ctrl.c
    engine/eng_dyn.c
    engine/eng_err.c
    engine/eng_fat.c
    engine/eng_init.c
    engine/eng_lib.c
    engine/eng_list.c
    engine/eng_openssl.c
    engine/eng_pkey.c
    engine/eng_rdrand.c
    engine/eng_table.c
    engine/tb_asnmth.c
    engine/tb_cipher.c
    engine/tb_dh.c
    engine/tb_digest.c
    engine/tb_dsa.c
    engine/tb_eckey.c
    engine/tb_pkmeth.c
    engine/tb_rand.c
    engine/tb_rsa.c
    err/err.c
    err/err_all.c
    err/err_prn.c
    evp/bio_b64.c
    evp/bio_enc.c
    evp/bio_md.c
    evp/bio_ok.c
    evp/c_allc.c
    evp/c_alld.c
    evp/cmeth_lib.c
    evp/digest.c
    evp/e_aes.c
    evp/e_aes_cbc_hmac_sha1.c
    evp/e_aes_cbc_hmac_sha256.c
    evp/e_aria.c
    evp/e_bf.c
    evp/e_camellia.c
    evp/e_cast.c
    evp/e_chacha20_poly1305.c
    evp/e_des.c
    evp/e_des3.c
    evp/e_idea.c
    evp/e_null.c
    evp/e_old.c
    evp/e_rc2.c
    evp/e_rc4.c
    evp/e_rc4_hmac_md5.c
    evp/e_rc5.c
    evp/e_seed.c
    evp/e_sm4.c
    evp/e_xcbc_d.c
    evp/encode.c
    evp/evp_cnf.c
    evp/evp_enc.c
    evp/evp_err.c
    evp/evp_key.c
    evp/evp_lib.c
    evp/evp_pbe.c
    evp/evp_pkey.c
    evp/m_md2.c
    evp/m_md4.c
    evp/m_md5.c
    evp/m_md5_sha1.c
    evp/m_mdc2.c
    evp/m_null.c
    evp/m_ripemd.c
    evp/m_sha1.c
    evp/m_sha3.c
    evp/m_sigver.c
    evp/m_wp.c
    evp/names.c
    evp/p5_crpt.c
    evp/p5_crpt2.c
    evp/p_dec.c
    evp/p_enc.c
    evp/p_lib.c
    evp/p_open.c
    evp/p_seal.c
    evp/p_sign.c
    evp/p_verify.c
    evp/pbe_scrypt.c
    evp/pmeth_fn.c
    evp/pmeth_gn.c
    evp/pmeth_lib.c
    ex_data.c
    getenv.c
    hmac/hm_ameth.c
    hmac/hm_pmeth.c
    hmac/hmac.c
    idea/i_cbc.c
    idea/i_cfb64.c
    idea/i_ecb.c
    idea/i_ofb64.c
    idea/i_skey.c
    init.c
    kdf/hkdf.c
    kdf/kdf_err.c
    kdf/scrypt.c
    kdf/tls1_prf.c
    lhash/lh_stats.c
    lhash/lhash.c
    md4/md4_dgst.c
    md4/md4_one.c
    md5/md5_dgst.c
    md5/md5_one.c
    mdc2/mdc2_one.c
    mdc2/mdc2dgst.c
    mem.c
    mem_dbg.c
    mem_sec.c
    modes/cbc128.c
    modes/ccm128.c
    modes/cfb128.c
    modes/ctr128.c
    modes/cts128.c
    modes/gcm128.c
    modes/ocb128.c
    modes/ofb128.c
    modes/wrap128.c
    modes/xts128.c
    o_dir.c
    o_fips.c
    o_fopen.c
    o_init.c
    o_str.c
    o_time.c
    objects/o_names.c
    objects/obj_dat.c
    objects/obj_err.c
    objects/obj_lib.c
    objects/obj_xref.c
    ocsp/ocsp_asn.c
    ocsp/ocsp_cl.c
    ocsp/ocsp_err.c
    ocsp/ocsp_ext.c
    ocsp/ocsp_ht.c
    ocsp/ocsp_lib.c
    ocsp/ocsp_prn.c
    ocsp/ocsp_srv.c
    ocsp/ocsp_vfy.c
    ocsp/v3_ocsp.c
    pem/pem_all.c
    pem/pem_err.c
    pem/pem_info.c
    pem/pem_lib.c
    pem/pem_oth.c
    pem/pem_pk8.c
    pem/pem_pkey.c
    pem/pem_sign.c
    pem/pem_x509.c
    pem/pem_xaux.c
    pem/pvkfmt.c
    pkcs12/p12_add.c
    pkcs12/p12_asn.c
    pkcs12/p12_attr.c
    pkcs12/p12_crpt.c
    pkcs12/p12_crt.c
    pkcs12/p12_decr.c
    pkcs12/p12_init.c
    pkcs12/p12_key.c
    pkcs12/p12_kiss.c
    pkcs12/p12_mutl.c
    pkcs12/p12_npas.c
    pkcs12/p12_p8d.c
    pkcs12/p12_p8e.c
    pkcs12/p12_sbag.c
    pkcs12/p12_utl.c
    pkcs12/pk12err.c
    pkcs7/bio_pk7.c
    pkcs7/pk7_asn1.c
    pkcs7/pk7_attr.c
    pkcs7/pk7_doit.c
    pkcs7/pk7_lib.c
    pkcs7/pk7_mime.c
    pkcs7/pk7_smime.c
    pkcs7/pkcs7err.c
    poly1305/poly1305.c
    poly1305/poly1305_ameth.c
    poly1305/poly1305_pmeth.c
    rand/drbg_ctr.c
    rand/drbg_lib.c
    rand/rand_egd.c
    rand/rand_err.c
    rand/rand_lib.c
    rand/rand_unix.c
    rand/rand_win.c
    rand/randfile.c
    rc2/rc2_cbc.c
    rc2/rc2_ecb.c
    rc2/rc2_skey.c
    rc2/rc2cfb64.c
    rc2/rc2ofb64.c
    ripemd/rmd_dgst.c
    ripemd/rmd_one.c
    rsa/rsa_ameth.c
    rsa/rsa_asn1.c
    rsa/rsa_chk.c
    rsa/rsa_crpt.c
    rsa/rsa_depr.c
    rsa/rsa_err.c
    rsa/rsa_gen.c
    rsa/rsa_lib.c
    rsa/rsa_meth.c
    rsa/rsa_mp.c
    rsa/rsa_none.c
    rsa/rsa_oaep.c
    rsa/rsa_ossl.c
    rsa/rsa_pk1.c
    rsa/rsa_pmeth.c
    rsa/rsa_prn.c
    rsa/rsa_pss.c
    rsa/rsa_saos.c
    rsa/rsa_sign.c
    rsa/rsa_ssl.c
    rsa/rsa_x931.c
    rsa/rsa_x931g.c
    seed/seed.c
    seed/seed_cbc.c
    seed/seed_cfb.c
    seed/seed_ecb.c
    seed/seed_ofb.c
    sha/sha1_one.c
    sha/sha1dgst.c
    sha/sha256.c
    sha/sha512.c
    siphash/siphash.c
    siphash/siphash_ameth.c
    siphash/siphash_pmeth.c
    sm2/sm2_crypt.c
    sm2/sm2_err.c
    sm2/sm2_pmeth.c
    sm2/sm2_sign.c
    sm3/m_sm3.c
    sm3/sm3.c
    sm4/sm4.c
    srp/srp_lib.c
    srp/srp_vfy.c
    stack/stack.c
    store/loader_file.c
    store/store_err.c
    store/store_init.c
    store/store_lib.c
    store/store_register.c
    store/store_strings.c
    threads_none.c
    threads_pthread.c
    threads_win.c
    ts/ts_asn1.c
    ts/ts_conf.c
    ts/ts_err.c
    ts/ts_lib.c
    ts/ts_req_print.c
    ts/ts_req_utils.c
    ts/ts_rsp_print.c
    ts/ts_rsp_sign.c
    ts/ts_rsp_utils.c
    ts/ts_rsp_verify.c
    ts/ts_verify_ctx.c
    txt_db/txt_db.c
    ui/ui_err.c
    ui/ui_lib.c
    ui/ui_null.c
    ui/ui_openssl.c
    ui/ui_util.c
    uid.c
    whrlpool/wp_dgst.c
    x509/by_dir.c
    x509/by_file.c
    x509/t_crl.c
    x509/t_req.c
    x509/t_x509.c
    x509/x509_att.c
    x509/x509_cmp.c
    x509/x509_d2.c
    x509/x509_def.c
    x509/x509_err.c
    x509/x509_ext.c
    x509/x509_lu.c
    x509/x509_meth.c
    x509/x509_obj.c
    x509/x509_r2x.c
    x509/x509_req.c
    x509/x509_set.c
    x509/x509_trs.c
    x509/x509_txt.c
    x509/x509_v3.c
    x509/x509_vfy.c
    x509/x509_vpm.c
    x509/x509cset.c
    x509/x509name.c
    x509/x509rset.c
    x509/x509spki.c
    x509/x509type.c
    x509/x_all.c
    x509/x_attrib.c
    x509/x_crl.c
    x509/x_exten.c
    x509/x_name.c
    x509/x_pubkey.c
    x509/x_req.c
    x509/x_x509.c
    x509/x_x509a.c
    x509v3/pcy_cache.c
    x509v3/pcy_data.c
    x509v3/pcy_lib.c
    x509v3/pcy_map.c
    x509v3/pcy_node.c
    x509v3/pcy_tree.c
    x509v3/v3_addr.c
    x509v3/v3_admis.c
    x509v3/v3_akey.c
    x509v3/v3_akeya.c
    x509v3/v3_alt.c
    x509v3/v3_asid.c
    x509v3/v3_bcons.c
    x509v3/v3_bitst.c
    x509v3/v3_conf.c
    x509v3/v3_cpols.c
    x509v3/v3_crld.c
    x509v3/v3_enum.c
    x509v3/v3_extku.c
    x509v3/v3_genn.c
    x509v3/v3_ia5.c
    x509v3/v3_info.c
    x509v3/v3_int.c
    x509v3/v3_lib.c
    x509v3/v3_ncons.c
    x509v3/v3_pci.c
    x509v3/v3_pcia.c
    x509v3/v3_pcons.c
    x509v3/v3_pku.c
    x509v3/v3_pmaps.c
    x509v3/v3_prn.c
    x509v3/v3_purp.c
    x509v3/v3_skey.c
    x509v3/v3_sxnet.c
    x509v3/v3_tlsf.c
    x509v3/v3_utl.c
    x509v3/v3err.c
)

IF (NOT WINDOWS_I686)
    SRCS(
        ec/ecp_nistz256.c
    )
ENDIF()

IF (NOT IOS_ARMV7 AND NOT ANDROID_ARMV7 AND NOT LINUX_ARMV7)
    SRCS(
        aes/aes_core.c
    )
ENDIF()

IF (NOT IOS_I386 AND NOT ANDROID_I686 AND NOT WINDOWS_I686)
    SRCS(
        bf/bf_enc.c
        camellia/cmll_misc.c
        des/des_enc.c
        des/fcrypt_b.c
    )
ENDIF()

IF (OS_DARWIN AND ARCH_X86_64 OR OS_LINUX AND ARCH_X86_64 OR OS_WINDOWS AND ARCH_X86_64)
    SRCS(
        bn/rsaz_exp.c
    )
ENDIF()

IF (OS_DARWIN AND ARCH_X86_64 OR OS_LINUX AND ARCH_X86_64)
    SRCS(
        bn/asm/x86_64-gcc.c
    )
ENDIF()

IF (OS_LINUX AND ARCH_AARCH64 OR OS_WINDOWS AND ARCH_X86_64 OR OS_LINUX AND ARCH_PPC64LE OR OS_EMSCRIPTEN)
    SRCS(
        bn/bn_asm.c
    )
ENDIF()

IF (OS_EMSCRIPTEN)
    SRCS(
        camellia/camellia.c
        camellia/cmll_cbc.c
        crypto/chacha/chacha_enc.c
        mem_clr.c
        rc4/rc4_enc.c
        rc4/rc4_skey.c
        sha/keccak1600.c
        whrlpool/wp_block.c
    )
ENDIF()

IF (OS_DARWIN AND ARCH_X86_64)
    SRCS(
        ../asm/darwin/crypto/aes/aesni-mb-x86_64.s
        ../asm/darwin/crypto/aes/aesni-sha1-x86_64.s
        ../asm/darwin/crypto/aes/aesni-sha256-x86_64.s
        ../asm/darwin/crypto/aes/aesni-x86_64.s
        ../asm/darwin/crypto/aes/vpaes-x86_64.s
        ../asm/darwin/crypto/bn/rsaz-avx2.s
        ../asm/darwin/crypto/bn/rsaz-x86_64.s
        ../asm/darwin/crypto/bn/x86_64-gf2m.s
        ../asm/darwin/crypto/bn/x86_64-mont.s
        ../asm/darwin/crypto/bn/x86_64-mont5.s
        ../asm/darwin/crypto/camellia/cmll-x86_64.s
        ../asm/darwin/crypto/chacha/chacha-x86_64.s
        ../asm/darwin/crypto/ec/ecp_nistz256-x86_64.s
        ../asm/darwin/crypto/ec/x25519-x86_64.s
        ../asm/darwin/crypto/md5/md5-x86_64.s
        ../asm/darwin/crypto/modes/aesni-gcm-x86_64.s
        ../asm/darwin/crypto/modes/ghash-x86_64.s
        ../asm/darwin/crypto/poly1305/poly1305-x86_64.s
        ../asm/darwin/crypto/rc4/rc4-md5-x86_64.s
        ../asm/darwin/crypto/rc4/rc4-x86_64.s
        ../asm/darwin/crypto/sha/keccak1600-x86_64.s
        ../asm/darwin/crypto/sha/sha1-mb-x86_64.s
        ../asm/darwin/crypto/sha/sha1-x86_64.s
        ../asm/darwin/crypto/sha/sha256-mb-x86_64.s
        ../asm/darwin/crypto/sha/sha256-x86_64.s
        ../asm/darwin/crypto/sha/sha512-x86_64.s
        ../asm/darwin/crypto/whrlpool/wp-x86_64.s
        ../asm/darwin/crypto/x86_64cpuid.s
    )
ENDIF()

IF (OS_DARWIN AND ARCH_ARM64)
    SRCS(
        ../asm/darwin-arm64/crypto/sha/keccak1600-armv8.S
        ../asm/darwin-arm64/crypto/sha/sha512-armv8.S
        ../asm/darwin-arm64/crypto/sha/sha1-armv8.S
        ../asm/darwin-arm64/crypto/sha/sha256-armv8.S
        ../asm/darwin-arm64/crypto/poly1305/poly1305-armv8.S
        ../asm/darwin-arm64/crypto/ec/ecp_nistz256-armv8.S
        ../asm/darwin-arm64/crypto/chacha/chacha-armv8.S
        ../asm/darwin-arm64/crypto/bn/armv8-mont.S
        ../asm/darwin-arm64/crypto/arm64cpuid.S
        ../asm/darwin-arm64/crypto/aes/aesv8-armx.S
        ../asm/darwin-arm64/crypto/aes/vpaes-armv8.S
        ../asm/darwin-arm64/crypto/modes/ghashv8-armx.S
        armcap.c
        bn/bn_asm.c
        camellia/camellia.c
        camellia/cmll_cbc.c
        dso/dso_dlfcn.c
        rc4/rc4_enc.c
        rc4/rc4_skey.c
        whrlpool/wp_block.c
    )
ENDIF()

IF (OS_LINUX AND ARCH_ARM7)
    CFLAGS(
        -DOPENSSL_PIC
        -DOPENSSL_BN_ASM_GF2m
        -DAES_ASM
        -DBSAES_ASM
        -DGHASH_ASM
    )
    SRCS(
        ../asm/android/arm/crypto/ec/ecp_nistz256-armv4.S
        ../asm/android/arm/crypto/poly1305/poly1305-armv4.S
        ../asm/android/arm/crypto/armv4cpuid.S
        ../asm/android/arm/crypto/bn/armv4-mont.S
        ../asm/android/arm/crypto/bn/armv4-gf2m.S
        ../asm/android/arm/crypto/aes/aes-armv4.S
        ../asm/android/arm/crypto/aes/bsaes-armv7.S
        ../asm/android/arm/crypto/aes/aesv8-armx.S
        ../asm/android/arm/crypto/sha/keccak1600-armv4.S
        ../asm/android/arm/crypto/sha/sha256-armv4.S
        ../asm/android/arm/crypto/sha/sha512-armv4.S
        ../asm/android/arm/crypto/sha/sha1-armv4-large.S
        ../asm/android/arm/crypto/chacha/chacha-armv4.S
        ../asm/android/arm/crypto/modes/ghashv8-armx.S
        ../asm/android/arm/crypto/modes/ghash-armv4.S
        armcap.c
        bn/bn_asm.c
        camellia/camellia.c
        camellia/cmll_cbc.c
        dso/dso_dlfcn.c
        rand/rand_vms.c
        rc4/rc4_enc.c
        rc4/rc4_skey.c
        whrlpool/wp_block.c
    )
ENDIF()

IF (OS_LINUX AND ARCH_AARCH64)
    SRCS(
        ../asm/aarch64/crypto/aes/aesv8-armx.S
        ../asm/aarch64/crypto/aes/vpaes-armv8.S
        ../asm/aarch64/crypto/arm64cpuid.S
        ../asm/aarch64/crypto/bn/armv8-mont.S
        ../asm/aarch64/crypto/chacha/chacha-armv8.S
        ../asm/aarch64/crypto/ec/ecp_nistz256-armv8.S
        ../asm/aarch64/crypto/modes/ghashv8-armx.S
        ../asm/aarch64/crypto/poly1305/poly1305-armv8.S
        ../asm/aarch64/crypto/sha/keccak1600-armv8.S
        ../asm/aarch64/crypto/sha/sha1-armv8.S
        ../asm/aarch64/crypto/sha/sha256-armv8.S
        ../asm/aarch64/crypto/sha/sha512-armv8.S
        armcap.c
        camellia/camellia.c
        camellia/cmll_cbc.c
        rc4/rc4_enc.c
        rc4/rc4_skey.c
        whrlpool/wp_block.c
    )
ENDIF()

IF (OS_LINUX AND ARCH_PPC64LE)
    SRCS(
        ../asm/ppc64le/crypto/aes/aesp8-ppc.s
        ../asm/ppc64le/crypto/aes/vpaes-ppc.s
        ../asm/ppc64le/crypto/bn/bn-ppc.s
        ../asm/ppc64le/crypto/bn/ppc-mont.s
        ../asm/ppc64le/crypto/chacha/chacha-ppc.s
        ../asm/ppc64le/crypto/ec/ecp_nistz256-ppc64.s
        ../asm/ppc64le/crypto/ec/x25519-ppc64.s
        ../asm/ppc64le/crypto/modes/ghashp8-ppc.s
        ../asm/ppc64le/crypto/poly1305/poly1305-ppc.s
        ../asm/ppc64le/crypto/poly1305/poly1305-ppcfp.s
        ../asm/ppc64le/crypto/ppccpuid.s
        ../asm/ppc64le/crypto/sha/keccak1600-ppc64.s
        ../asm/ppc64le/crypto/sha/sha1-ppc.s
        ../asm/ppc64le/crypto/sha/sha256-ppc.s
        ../asm/ppc64le/crypto/sha/sha256p8-ppc.s
        ../asm/ppc64le/crypto/sha/sha512-ppc.s
        ../asm/ppc64le/crypto/sha/sha512p8-ppc.s
        ppccap.c
        camellia/camellia.c
        camellia/cmll_cbc.c
        rc4/rc4_enc.c
        rc4/rc4_skey.c
        whrlpool/wp_block.c
    )
ENDIF()

IF (OS_LINUX AND ARCH_X86_64)
    SRCS(
        ../asm/linux/crypto/aes/aesni-mb-x86_64.s
        ../asm/linux/crypto/aes/aesni-sha1-x86_64.s
        ../asm/linux/crypto/aes/aesni-sha256-x86_64.s
        ../asm/linux/crypto/aes/aesni-x86_64.s
        ../asm/linux/crypto/aes/vpaes-x86_64.s
        ../asm/linux/crypto/bn/rsaz-avx2.s
        ../asm/linux/crypto/bn/rsaz-x86_64.s
        ../asm/linux/crypto/bn/x86_64-gf2m.s
        ../asm/linux/crypto/bn/x86_64-mont.s
        ../asm/linux/crypto/bn/x86_64-mont5.s
        ../asm/linux/crypto/camellia/cmll-x86_64.s
        ../asm/linux/crypto/chacha/chacha-x86_64.s
        ../asm/linux/crypto/ec/ecp_nistz256-x86_64.s
        ../asm/linux/crypto/ec/x25519-x86_64.s
        ../asm/linux/crypto/md5/md5-x86_64.s
        ../asm/linux/crypto/modes/aesni-gcm-x86_64.s
        ../asm/linux/crypto/modes/ghash-x86_64.s
        ../asm/linux/crypto/poly1305/poly1305-x86_64.s
        ../asm/linux/crypto/rc4/rc4-md5-x86_64.s
        ../asm/linux/crypto/rc4/rc4-x86_64.s
        ../asm/linux/crypto/sha/keccak1600-x86_64.s
        ../asm/linux/crypto/sha/sha1-mb-x86_64.s
        ../asm/linux/crypto/sha/sha1-x86_64.s
        ../asm/linux/crypto/sha/sha256-mb-x86_64.s
        ../asm/linux/crypto/sha/sha256-x86_64.s
        ../asm/linux/crypto/sha/sha512-x86_64.s
        ../asm/linux/crypto/whrlpool/wp-x86_64.s
        ../asm/linux/crypto/x86_64cpuid.s
    )
ENDIF()

IF (OS_WINDOWS AND ARCH_X86_64)
    SRCS(
        ../asm/windows/crypto/aes/aesni-mb-x86_64.masm
        ../asm/windows/crypto/aes/aesni-sha1-x86_64.masm
        ../asm/windows/crypto/aes/aesni-sha256-x86_64.masm
        ../asm/windows/crypto/aes/aesni-x86_64.masm
        ../asm/windows/crypto/aes/vpaes-x86_64.masm
        ../asm/windows/crypto/bn/rsaz-avx2.masm
        ../asm/windows/crypto/bn/rsaz-x86_64.masm
        ../asm/windows/crypto/bn/x86_64-gf2m.masm
        ../asm/windows/crypto/bn/x86_64-mont.masm
        ../asm/windows/crypto/bn/x86_64-mont5.masm
        ../asm/windows/crypto/camellia/cmll-x86_64.masm
        ../asm/windows/crypto/chacha/chacha-x86_64.masm
        ../asm/windows/crypto/ec/ecp_nistz256-x86_64.masm
        ../asm/windows/crypto/ec/x25519-x86_64.masm
        ../asm/windows/crypto/md5/md5-x86_64.masm
        ../asm/windows/crypto/modes/aesni-gcm-x86_64.masm
        ../asm/windows/crypto/modes/ghash-x86_64.masm
        ../asm/windows/crypto/poly1305/poly1305-x86_64.masm
        ../asm/windows/crypto/rc4/rc4-md5-x86_64.masm
        ../asm/windows/crypto/rc4/rc4-x86_64.masm
        ../asm/windows/crypto/sha/keccak1600-x86_64.masm
        ../asm/windows/crypto/sha/sha1-mb-x86_64.masm
        ../asm/windows/crypto/sha/sha1-x86_64.masm
        ../asm/windows/crypto/sha/sha256-mb-x86_64.masm
        ../asm/windows/crypto/sha/sha256-x86_64.masm
        ../asm/windows/crypto/sha/sha512-x86_64.masm
        ../asm/windows/crypto/whrlpool/wp-x86_64.masm
        ../asm/windows/crypto/uplink-x86_64.masm
        ../asm/windows/crypto/x86_64cpuid.masm
    )
ENDIF()

IF (OS_WINDOWS AND ARCH_I386)
    CFLAGS(
        -DGHASH_ASM
        -DOPENSSL_BN_ASM_GF2m
        -DRC4_ASM
        -DMD5_ASM
    )
    MASMFLAGS(
        /safeseh
    )
    SRCS(
        ../asm/windows/crypto/aes/aesni-x86.masm
        ../asm/windows/crypto/aes/vpaes-x86.masm
        ../asm/windows/crypto/bn/x86-gf2m.masm
        ../asm/windows/crypto/bn/x86-mont.masm
        ../asm/windows/crypto/camellia/cmll-x86.masm
        ../asm/windows/crypto/chacha/chacha-x86.masm
        ../asm/windows/crypto/md5/md5-586.masm
        ../asm/windows/crypto/modes/ghash-x86.masm
        ../asm/windows/crypto/rc4/rc4-586.masm
        ../asm/windows/crypto/sha/sha1-586.masm
        ../asm/windows/crypto/sha/sha256-586.masm
        ../asm/windows/crypto/sha/sha512-586.masm
        ../asm/windows/crypto/x86cpuid.masm
        bf/bf_enc.c
        bn/bn_asm.c
        des/des_enc.c
        des/fcrypt_b.c
        sha/keccak1600.c
        whrlpool/wp_block.c
    )
ENDIF()

IF (OS_IOS AND ARCH_ARM64)
    CFLAGS(
        -DOPENSSL_PIC
        -D_REENTRANT
    )
    SRCS(
        ../asm/ios/arm64/crypto/aes/aesv8-armx.S
        ../asm/ios/arm64/crypto/aes/vpaes-armv8.S
        ../asm/ios/arm64/crypto/arm64cpuid.S
        ../asm/ios/arm64/crypto/bn/armv8-mont.S
        ../asm/ios/arm64/crypto/chacha/chacha-armv8.S
        ../asm/ios/arm64/crypto/ec/ecp_nistz256-armv8.S
        ../asm/ios/arm64/crypto/modes/ghashv8-armx.S
        ../asm/ios/arm64/crypto/poly1305/poly1305-armv8.S
        ../asm/ios/arm64/crypto/sha/keccak1600-armv8.S
        ../asm/ios/arm64/crypto/sha/sha1-armv8.S
        ../asm/ios/arm64/crypto/sha/sha256-armv8.S
        ../asm/ios/arm64/crypto/sha/sha512-armv8.S
        armcap.c
        bn/bn_asm.c
        camellia/camellia.c
        camellia/cmll_cbc.c
        dso/dso_dlfcn.c
        rand/rand_vms.c
        rc4/rc4_enc.c
        rc4/rc4_skey.c
        whrlpool/wp_block.c
    )
ENDIF()

IF (OS_IOS AND ARCH_X86_64)
    CFLAGS(
        -DL_ENDIAN
        -DOPENSSL_PIC
        -DOPENSSL_IA32_SSE2
        -DOPENSSL_BN_ASM_MONT5
        -DOPENSSL_BN_ASM_GF2m
        -DRC4_ASM
        -DMD5_ASM
        -DGHASH_ASM
        -DECP_NISTZ256_ASM
        -DX25519_ASM
        -D_REENTRANT
    )
    SRCS(
        ../asm/ios/x86_64/crypto/md5/md5-x86_64.s
        ../asm/ios/x86_64/crypto/rc4/rc4-md5-x86_64.s
        ../asm/ios/x86_64/crypto/rc4/rc4-x86_64.s
        ../asm/ios/x86_64/crypto/modes/ghash-x86_64.s
        ../asm/ios/x86_64/crypto/modes/aesni-gcm-x86_64.s
        ../asm/ios/x86_64/crypto/chacha/chacha-x86_64.s
        ../asm/ios/x86_64/crypto/ec/ecp_nistz256-x86_64.s
        ../asm/ios/x86_64/crypto/ec/x25519-x86_64.s
        ../asm/ios/x86_64/crypto/x86_64cpuid.s
        ../asm/ios/x86_64/crypto/poly1305/poly1305-x86_64.s
        ../asm/ios/x86_64/crypto/bn/rsaz-x86_64.s
        ../asm/ios/x86_64/crypto/bn/x86_64-mont.s
        ../asm/ios/x86_64/crypto/bn/x86_64-gf2m.s
        ../asm/ios/x86_64/crypto/bn/x86_64-mont5.s
        ../asm/ios/x86_64/crypto/bn/rsaz-avx2.s
        ../asm/ios/x86_64/crypto/sha/sha512-x86_64.s
        ../asm/ios/x86_64/crypto/sha/sha256-x86_64.s
        ../asm/ios/x86_64/crypto/sha/keccak1600-x86_64.s
        ../asm/ios/x86_64/crypto/sha/sha1-x86_64.s
        ../asm/ios/x86_64/crypto/sha/sha1-mb-x86_64.s
        ../asm/ios/x86_64/crypto/sha/sha256-mb-x86_64.s
        ../asm/ios/x86_64/crypto/camellia/cmll-x86_64.s
        ../asm/ios/x86_64/crypto/whrlpool/wp-x86_64.s
        ../asm/ios/x86_64/crypto/aes/vpaes-x86_64.s
        ../asm/ios/x86_64/crypto/aes/aesni-sha1-x86_64.s
        ../asm/ios/x86_64/crypto/aes/aesni-sha256-x86_64.s
        ../asm/ios/x86_64/crypto/aes/aesni-x86_64.s
        ../asm/ios/x86_64/crypto/aes/aesni-mb-x86_64.s
        bn/asm/x86_64-gcc.c
        bn/rsaz_exp.c
        dso/dso_dlfcn.c
        rand/rand_vms.c
    )
ENDIF()

IF (OS_ANDROID AND ARCH_X86_64)
    CFLAGS(
        -DOPENSSL_USE_NODELETE
        -DOPENSSL_PIC
        -DOPENSSL_IA32_SSE2
        -DOPENSSL_BN_ASM_MONT5
        -DOPENSSL_BN_ASM_GF2m
        -DRC4_ASM
        -DMD5_ASM
        -DGHASH_ASM
        -DX25519_ASM
    )
    SRCS(
        ../asm/android/x86_64/crypto/ec/x25519-x86_64.s
        ../asm/android/x86_64/crypto/ec/ecp_nistz256-x86_64.s
        ../asm/android/x86_64/crypto/md5/md5-x86_64.s
        ../asm/android/x86_64/crypto/rc4/rc4-x86_64.s
        ../asm/android/x86_64/crypto/rc4/rc4-md5-x86_64.s
        ../asm/android/x86_64/crypto/whrlpool/wp-x86_64.s
        ../asm/android/x86_64/crypto/poly1305/poly1305-x86_64.s
        ../asm/android/x86_64/crypto/x86_64cpuid.s
        ../asm/android/x86_64/crypto/camellia/cmll-x86_64.s
        ../asm/android/x86_64/crypto/bn/x86_64-mont5.s
        ../asm/android/x86_64/crypto/bn/rsaz-avx2.s
        ../asm/android/x86_64/crypto/bn/rsaz-x86_64.s
        ../asm/android/x86_64/crypto/bn/x86_64-mont.s
        ../asm/android/x86_64/crypto/bn/x86_64-gf2m.s
        ../asm/android/x86_64/crypto/aes/aesni-sha256-x86_64.s
        ../asm/android/x86_64/crypto/aes/aesni-mb-x86_64.s
        ../asm/android/x86_64/crypto/aes/aesni-x86_64.s
        ../asm/android/x86_64/crypto/aes/vpaes-x86_64.s
        ../asm/android/x86_64/crypto/aes/aesni-sha1-x86_64.s
        ../asm/android/x86_64/crypto/sha/sha256-x86_64.s
        ../asm/android/x86_64/crypto/sha/sha1-mb-x86_64.s
        ../asm/android/x86_64/crypto/sha/sha1-x86_64.s
        ../asm/android/x86_64/crypto/sha/sha256-mb-x86_64.s
        ../asm/android/x86_64/crypto/sha/sha512-x86_64.s
        ../asm/android/x86_64/crypto/sha/keccak1600-x86_64.s
        ../asm/android/x86_64/crypto/chacha/chacha-x86_64.s
        ../asm/android/x86_64/crypto/modes/ghash-x86_64.s
        ../asm/android/x86_64/crypto/modes/aesni-gcm-x86_64.s
        bn/asm/x86_64-gcc.c
        bn/rsaz_exp.c
        dso/dso_dlfcn.c
        rand/rand_vms.c
    )
ENDIF()

IF (OS_ANDROID AND ARCH_I686)
    CFLAGS(
        -DOPENSSL_PIC
        -DOPENSSL_BN_ASM_PART_WORDS
        -DOPENSSL_IA32_SSE2
        -DOPENSSL_BN_ASM_GF2m
        -DRC4_ASM
        -DMD5_ASM
        -DRMD160_ASM
        -DWHIRLPOOL_ASM
        -DGHASH_ASM
    )
    SRCS(
        ../asm/android/i686/crypto/ec/ecp_nistz256-x86.s
        ../asm/android/i686/crypto/bf/bf-586.s
        ../asm/android/i686/crypto/md5/md5-586.s
        ../asm/android/i686/crypto/rc4/rc4-586.s
        ../asm/android/i686/crypto/whrlpool/wp-mmx.s
        ../asm/android/i686/crypto/x86cpuid.s
        ../asm/android/i686/crypto/des/crypt586.s
        ../asm/android/i686/crypto/des/des-586.s
        ../asm/android/i686/crypto/poly1305/poly1305-x86.s
        ../asm/android/i686/crypto/ripemd/rmd-586.s
        ../asm/android/i686/crypto/camellia/cmll-x86.s
        ../asm/android/i686/crypto/bn/bn-586.s
        ../asm/android/i686/crypto/bn/co-586.s
        ../asm/android/i686/crypto/bn/x86-gf2m.s
        ../asm/android/i686/crypto/bn/x86-mont.s
        ../asm/android/i686/crypto/aes/aesni-x86.s
        ../asm/android/i686/crypto/aes/vpaes-x86.s
        ../asm/android/i686/crypto/sha/sha512-586.s
        ../asm/android/i686/crypto/sha/sha256-586.s
        ../asm/android/i686/crypto/sha/sha1-586.s
        ../asm/android/i686/crypto/chacha/chacha-x86.s
        ../asm/android/i686/crypto/modes/ghash-x86.s
        dso/dso_dlfcn.c
        rand/rand_vms.c
        sha/keccak1600.c
        whrlpool/wp_block.c
    )
ENDIF()

IF (OS_ANDROID AND ARCH_ARM7)
    IF (CLANG)
        # XXX: This is a workarond for 'out of range immediate fixup value'
        # error with clang integrated assembler:
        # https://github.com/openssl/openssl/issues/7878
        CFLAGS(
            -mno-thumb
        )
    ENDIF()
    CFLAGS(
        -DOPENSSL_PIC
        -DOPENSSL_BN_ASM_GF2m
        -DAES_ASM
        -DBSAES_ASM
        -DGHASH_ASM
    )
    SRCS(
        ../asm/android/arm/crypto/ec/ecp_nistz256-armv4.S
        ../asm/android/arm/crypto/poly1305/poly1305-armv4.S
        ../asm/android/arm/crypto/armv4cpuid.S
        ../asm/android/arm/crypto/bn/armv4-mont.S
        ../asm/android/arm/crypto/bn/armv4-gf2m.S
        ../asm/android/arm/crypto/aes/aes-armv4.S
        ../asm/android/arm/crypto/aes/bsaes-armv7.S
        ../asm/android/arm/crypto/aes/aesv8-armx.S
        ../asm/android/arm/crypto/sha/keccak1600-armv4.S
        ../asm/android/arm/crypto/sha/sha256-armv4.S
        ../asm/android/arm/crypto/sha/sha512-armv4.S
        ../asm/android/arm/crypto/sha/sha1-armv4-large.S
        ../asm/android/arm/crypto/chacha/chacha-armv4.S
        ../asm/android/arm/crypto/modes/ghashv8-armx.S
        ../asm/android/arm/crypto/modes/ghash-armv4.S
        armcap.c
        bn/bn_asm.c
        camellia/camellia.c
        camellia/cmll_cbc.c
        dso/dso_dlfcn.c
        rand/rand_vms.c
        rc4/rc4_enc.c
        rc4/rc4_skey.c
        whrlpool/wp_block.c
    )
ENDIF()

IF (OS_ANDROID AND ARCH_ARM64)
    CFLAGS(
        -DOPENSSL_PIC
    )
    SRCS(
        ../asm/android/arm64/crypto/ec/ecp_nistz256-armv8.S
        ../asm/android/arm64/crypto/poly1305/poly1305-armv8.S
        ../asm/android/arm64/crypto/bn/armv8-mont.S
        ../asm/android/arm64/crypto/aes/vpaes-armv8.S
        ../asm/android/arm64/crypto/aes/aesv8-armx.S
        ../asm/android/arm64/crypto/sha/sha512-armv8.S
        ../asm/android/arm64/crypto/sha/keccak1600-armv8.S
        ../asm/android/arm64/crypto/sha/sha1-armv8.S
        ../asm/android/arm64/crypto/sha/sha256-armv8.S
        ../asm/android/arm64/crypto/arm64cpuid.S
        ../asm/android/arm64/crypto/chacha/chacha-armv8.S
        ../asm/android/arm64/crypto/modes/ghashv8-armx.S
        armcap.c
        bn/bn_asm.c
        camellia/camellia.c
        camellia/cmll_cbc.c
        dso/dso_dlfcn.c
        rand/rand_vms.c
        rc4/rc4_enc.c
        rc4/rc4_skey.c
        whrlpool/wp_block.c
    )
ENDIF()

# mitigate SIGILL on some armv7 platforms
# https://github.com/openssl/openssl/issues/17009
IF (ARCADIA_OPENSSL_DISABLE_ARMV7_TICK)
    CFLAGS(
        -DARCADIA_OPENSSL_DISABLE_ARMV7_TICK
    )
ENDIF()
