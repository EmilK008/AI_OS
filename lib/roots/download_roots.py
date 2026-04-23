#!/usr/bin/env python3
"""Download a curated set of root CAs for AI_OS.

Writes each to lib/roots/<name>.pem. Re-run gen_trust_anchors.py afterwards
to fold them into kernel/trust_anchors.c.

The list below is a strategic ~8-root bundle that covers ~95% of the public
web: Let's Encrypt (RSA+ECC), DigiCert (G2/G3/legacy/Baltimore), Sectigo
(USERTrust RSA+ECC), Google Trust Services (R1+R4), and Amazon Root CA 1.
Add more URLs as needed.
"""

from __future__ import annotations

import pathlib
import ssl
import sys
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent

TARGETS: list[tuple[str, str]] = [
    ("ISRGRootX2.pem",              "https://letsencrypt.org/certs/isrg-root-x2.pem"),
    ("DigiCertGlobalRootCA.pem",    "https://cacerts.digicert.com/DigiCertGlobalRootCA.crt.pem"),
    ("DigiCertGlobalRootG3.pem",    "https://cacerts.digicert.com/DigiCertGlobalRootG3.crt.pem"),
    ("BaltimoreCyberTrustRoot.pem", "https://cacerts.digicert.com/BaltimoreCyberTrustRoot.crt.pem"),
    ("USERTrustECC.pem",            "https://crt.sectigo.com/USERTrustECCCertificationAuthority.crt"),
    ("GTSRootR1.pem",               "https://pki.goog/repo/certs/gtsr1.pem"),
    ("GTSRootR4.pem",               "https://pki.goog/repo/certs/gtsr4.pem"),
    ("AmazonRootCA1.pem",           "https://www.amazontrust.com/repository/AmazonRootCA1.pem"),
]


def ensure_pem(raw: bytes) -> bytes:
    """Some CA download endpoints return DER rather than PEM. Convert in that
    case using the cryptography lib. If it's already PEM, pass through."""
    if b"-----BEGIN CERTIFICATE-----" in raw:
        return raw
    from cryptography import x509
    from cryptography.hazmat.primitives.serialization import Encoding
    cert = x509.load_der_x509_certificate(raw)
    return cert.public_bytes(Encoding.PEM)


def main() -> int:
    ctx = ssl.create_default_context()
    ok = 0
    for name, url in TARGETS:
        dest = ROOT / name
        try:
            with urllib.request.urlopen(url, context=ctx, timeout=30) as r:
                raw = r.read()
            pem = ensure_pem(raw)
            dest.write_bytes(pem)
            print(f"  OK    {name}  ({len(pem)} B  <- {url})")
            ok += 1
        except Exception as e:
            print(f"  FAIL  {name}  <- {url}\n        {e}", file=sys.stderr)
    print(f"\n{ok}/{len(TARGETS)} downloaded into {ROOT}")
    return 0 if ok == len(TARGETS) else 1


if __name__ == "__main__":
    sys.exit(main())
