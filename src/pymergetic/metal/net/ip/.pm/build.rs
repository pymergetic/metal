use std::env;
use std::path::PathBuf;

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let mod_dir = manifest.join("..").canonicalize().expect("net/ip dir");
    /* net/ip -> metal */
    let metal_dir = mod_dir.join("../..").canonicalize().expect("metal dir");
    /* net/ip -> packages/metal (five levels: ip/net/metal/pymergetic/src) */
    let metal_root = mod_dir
        .join("../../../../..")
        .canonicalize()
        .expect("package root");
    let mbedtls = metal_root.join("external/mbedtls");
    let lib = mbedtls.join("library");
    let include = mbedtls.join("include");
    let ssl_dir = mod_dir.join("ssl");
    let cfg = ssl_dir.join("cfg");
    let libc = metal_dir.join("libc");

    if !include.join("mbedtls/ssl.h").is_file() {
        panic!(
            "missing mbedtls: git submodule update --init external/mbedtls ({})",
            include.display()
        );
    }

    let target = env::var("TARGET").unwrap_or_default();
    let freestanding = target.contains("none") || target.contains("uefi");
    let uefi = target.contains("uefi");

    let sources = [
        "platform.c",
        "platform_util.c",
        "constant_time.c",
        "entropy.c",
        "entropy_poll.c",
        "ctr_drbg.c",
        "md.c",
        "sha1.c",
        "sha256.c",
        "sha512.c",
        "aes.c",
        "gcm.c",
        "cipher.c",
        "cipher_wrap.c",
        "block_cipher.c",
        "bignum.c",
        "bignum_core.c",
        "asn1parse.c",
        "asn1write.c",
        "oid.c",
        "base64.c",
        "pk.c",
        "pk_wrap.c",
        "pkparse.c",
        "pk_ecc.c",
        "pem.c",
        "rsa.c",
        "rsa_alt_helpers.c",
        "ecp.c",
        "ecp_curves.c",
        "ecdh.c",
        "ecdsa.c",
        "x509.c",
        "x509_crt.c",
        "ssl_tls.c",
        "ssl_msg.c",
        "ssl_ciphersuites.c",
        "ssl_client.c",
        "ssl_tls12_client.c",
        "ssl_tls12_server.c",
        "ssl_debug_helpers_generated.c",
        "error.c",
    ];

    let mut build = cc::Build::new();
    for s in sources {
        build.file(lib.join(s));
    }
    build.file(ssl_dir.join("_platform.c"));
    build.file(ssl_dir.join("_ssl.c"));
    build.include(&include);
    build.include(&cfg);
    build.include(&ssl_dir);
    build.define("MBEDTLS_CONFIG_FILE", "\"mbedtls_metal_config.h\"");
    build.warnings(false);
    if freestanding {
        build.flag("-nostdinc");
        build.flag("-ffreestanding");
        build.flag("-fno-stack-protector");
        build.include(&libc);
        /* mbedtls oid.c uses UINT_MAX without including limits.h */
        build.flag("-include");
        build.flag(libc.join("limits.h").to_str().unwrap());
    }
    if uefi {
        build.compiler("clang");
        build.flag("--target=x86_64-unknown-windows-gnu");
        build.flag("-fshort-wchar");
        build.flag("-mno-red-zone");
    }
    build.compile("metal_net_ip_ssl");

    println!("cargo:rerun-if-changed={}", ssl_dir.join("_platform.c").display());
    println!("cargo:rerun-if-changed={}", ssl_dir.join("_ssl.c").display());
    println!(
        "cargo:rerun-if-changed={}",
        cfg.join("mbedtls_metal_config.h").display()
    );
    println!("cargo:rerun-if-changed={}", lib.display());
}
