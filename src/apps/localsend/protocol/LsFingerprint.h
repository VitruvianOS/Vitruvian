/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _LS_FINGERPRINT_H
#define _LS_FINGERPRINT_H

#include <string>

namespace LocalSend {

// Fingerprint handling per protocol section 2. In HTTPS mode the
// fingerprint is the SHA-256 of the certificate; in HTTP mode it is a
// random string. Generation here covers the HTTP mode; verification
// covers both (case-insensitive hex compare of SHA-256 digests).

// Random lowercase hex string, 32 bytes of entropy (64 chars).
std::string LsGenerateFingerprint();

// True when both fingerprints identify the same device. Comparison is
// case-insensitive so peers that uppercase (like the reference TLS
// pinning) still match.
bool LsFingerprintsEqual(const std::string& a, const std::string& b);

// SHA-256 of raw certificate DER, lowercase hex; the HTTPS-mode
// fingerprint value.
std::string LsFingerprintFromCertificate(const void* der, size_t length);

} // namespace LocalSend

#endif // _LS_FINGERPRINT_H
