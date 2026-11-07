/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "LsFingerprint.h"

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "LsSha256.h"

namespace LocalSend {

std::string
LsGenerateFingerprint()
{
	uint8_t entropy[32];
	memset(entropy, 0, sizeof(entropy));
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd >= 0) {
		ssize_t total = 0;
		while (total < 32) {
			ssize_t n = read(fd, entropy + total, 32 - total);
			if (n <= 0)
				break;
			total += n;
		}
		close(fd);
		if (total != 32) {
			// Fall through to the weak fallback below.
		}
	} else {
		// Weak but non-repeating fallback; fingerprint is only an
		// anti-loopback id in HTTP mode, not an authenticator.
		for (int i = 0; i < 32; i++)
			entropy[i] = static_cast<uint8_t>(random() ^ (getpid() << i));
	}

	static const char* digits = "0123456789abcdef";
	std::string out;
	out.reserve(64);
	for (int i = 0; i < 32; i++) {
		out += digits[entropy[i] >> 4];
		out += digits[entropy[i] & 0xf];
	}
	return out;
}


bool
LsFingerprintsEqual(const std::string& a, const std::string& b)
{
	if (a.size() != b.size() || a.empty())
		return false;
	for (size_t i = 0; i < a.size(); i++) {
		char ca = a[i];
		char cb = b[i];
		if (ca >= 'A' && ca <= 'Z')
			ca += 'a' - 'A';
		if (cb >= 'A' && cb <= 'Z')
			cb += 'a' - 'A';
		if (ca != cb)
			return false;
	}
	return true;
}


std::string
LsFingerprintFromCertificate(const void* der, size_t length)
{
	char hex[65];
	LsSha256::HashHex(der, length, hex);
	return std::string(hex);
}

} // namespace LocalSend
