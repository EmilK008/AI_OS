#!/usr/bin/env python3
"""Generate kernel/trust_anchors.c from a folder of root CA PEM files.

Produces the exact shape expected by kernel/net_tls.c:

    extern const br_x509_trust_anchor ai_os_trust_anchors[];
    extern const unsigned int         ai_os_trust_anchors_num;

For each CA:
  * The Subject DN is emitted as its raw DER-encoded bytes (what BearSSL's
    X.509 "minimal" validator compares against the Issuer field of the
    issued cert).
  * RSA public keys are emitted as (modulus, exponent) big-endian byte
    strings. EC keys as (curve_id, public_point).

This script is idempotent - re-run it whenever you add a PEM. It prints a
short human-readable summary of the CAs it found so you can confirm the
bundle contents.
"""

from __future__ import annotations

import pathlib
import sys
from cryptography import x509
from cryptography.hazmat.primitives.asymmetric import rsa, ec

ROOT = pathlib.Path(__file__).resolve().parent
OUT  = ROOT.parent.parent / "kernel" / "trust_anchors.c"

# BearSSL's curve IDs (from bearssl_ec.h)
CURVE_IDS = {
    "secp256r1": 23,  # BR_EC_secp256r1 - aka NIST P-256 / prime256v1
    "secp384r1": 24,  # BR_EC_secp384r1 - NIST P-384
    "secp521r1": 25,  # BR_EC_secp521r1 - NIST P-521
}


def c_bytes(name: str, data: bytes) -> str:
    out = [f"static const unsigned char {name}[] = {{"]
    for i in range(0, len(data), 12):
        chunk = data[i : i + 12]
        out.append("\t" + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    out.append("};")
    return "\n".join(out)


def process(pem_path: pathlib.Path, idx: int) -> tuple[str, str, str]:
    der = pem_path.read_bytes()
    cert = x509.load_pem_x509_certificate(der)
    subject_rfc = cert.subject.rfc4514_string()

    # Raw DER of the Subject Name sequence - this is what BearSSL compares
    # against the Issuer of the issued certificate. cryptography exposes
    # it via public_bytes(); we grab the SEQUENCE at offset 0.
    subject_der = cert.subject.public_bytes()

    pk = cert.public_key()

    pieces = [f"// --- [{idx}] {subject_rfc} (from {pem_path.name}) ---"]
    pieces.append(c_bytes(f"TA{idx}_DN", subject_der))

    if isinstance(pk, rsa.RSAPublicKey):
        n = pk.public_numbers().n
        e = pk.public_numbers().e
        n_bytes = n.to_bytes((n.bit_length() + 7) // 8, "big")
        e_bytes = e.to_bytes((e.bit_length() + 7) // 8, "big")
        pieces.append(c_bytes(f"TA{idx}_RSA_N", n_bytes))
        pieces.append(c_bytes(f"TA{idx}_RSA_E", e_bytes))
        struct_init = (
            f"\t{{\n"
            f"\t\t{{ (unsigned char *)TA{idx}_DN, sizeof TA{idx}_DN }},\n"
            f"\t\tBR_X509_TA_CA,\n"
            f"\t\t{{ BR_KEYTYPE_RSA, {{ .rsa = {{\n"
            f"\t\t\t(unsigned char *)TA{idx}_RSA_N, sizeof TA{idx}_RSA_N,\n"
            f"\t\t\t(unsigned char *)TA{idx}_RSA_E, sizeof TA{idx}_RSA_E,\n"
            f"\t\t}} }} }}\n"
            f"\t}}"
        )
        kind = f"RSA-{n.bit_length()}"
    elif isinstance(pk, ec.EllipticCurvePublicKey):
        curve_name = pk.curve.name
        if curve_name not in CURVE_IDS:
            raise SystemExit(
                f"{pem_path}: unsupported EC curve {curve_name} "
                "(BearSSL supports secp256r1, secp384r1, secp521r1)"
            )
        curve_id = CURVE_IDS[curve_name]
        # Uncompressed point encoding: 0x04 || X || Y
        q_bytes = pk.public_bytes(
            encoding=__import__(
                "cryptography.hazmat.primitives.serialization",
                fromlist=["Encoding"],
            ).Encoding.X962,
            format=__import__(
                "cryptography.hazmat.primitives.serialization",
                fromlist=["PublicFormat"],
            ).PublicFormat.UncompressedPoint,
        )
        pieces.append(c_bytes(f"TA{idx}_EC_Q", q_bytes))
        struct_init = (
            f"\t{{\n"
            f"\t\t{{ (unsigned char *)TA{idx}_DN, sizeof TA{idx}_DN }},\n"
            f"\t\tBR_X509_TA_CA,\n"
            f"\t\t{{ BR_KEYTYPE_EC, {{ .ec = {{\n"
            f"\t\t\t{curve_id},\n"
            f"\t\t\t(unsigned char *)TA{idx}_EC_Q, sizeof TA{idx}_EC_Q,\n"
            f"\t\t}} }} }}\n"
            f"\t}}"
        )
        kind = f"EC-{curve_name}"
    else:
        raise SystemExit(f"{pem_path}: unsupported key type {type(pk).__name__}")

    return "\n".join(pieces), struct_init, f"  [{idx}] {kind:14}  {subject_rfc}"


def main() -> int:
    pems = sorted(ROOT.glob("*.pem"))
    if not pems:
        print("No .pem files found in", ROOT, file=sys.stderr)
        return 1

    blocks: list[str] = []
    struct_inits: list[str] = []
    summaries: list[str] = []
    for i, p in enumerate(pems):
        blk, init, summary = process(p, i)
        blocks.append(blk)
        struct_inits.append(init)
        summaries.append(summary)

    header = (
        "/* ==========================================================\n"
        " * Root CA trust anchor bundle for AI_OS - AUTO-GENERATED.\n"
        " *\n"
        " * Do not edit by hand. Regenerate by adding PEM files to\n"
        " * lib/roots/ and running:\n"
        " *\n"
        " *     python lib/roots/gen_trust_anchors.py\n"
        " *\n"
        " * Bundle contents:\n"
        + "".join(f" *   {s}\n" for s in summaries)
        + " * ========================================================== */\n"
        "\n"
        "#include \"../lib/bearssl/inc/bearssl_x509.h\"\n"
        "\n"
    )

    body  = "\n\n".join(blocks) + "\n\n"
    body += "const br_x509_trust_anchor ai_os_trust_anchors[] = {\n"
    body += ",\n".join(struct_inits) + "\n"
    body += "};\n\n"
    body += (
        "const unsigned int ai_os_trust_anchors_num ="
        " sizeof ai_os_trust_anchors / sizeof ai_os_trust_anchors[0];\n"
    )

    OUT.write_text(header + body, encoding="utf-8")
    print(f"Wrote {OUT} with {len(pems)} trust anchor(s):")
    for s in summaries:
        print(s)
    return 0


if __name__ == "__main__":
    sys.exit(main())
