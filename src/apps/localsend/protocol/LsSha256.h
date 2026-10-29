/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _LS_SHA256_H
#define _LS_SHA256_H

#include <stdint.h>
#include <stddef.h>

namespace LocalSend {

// Streaming SHA-256 (FIPS 180-4). Used for file checksums and TLS
// certificate fingerprints; host-testable, no library dependency.

class LsSha256 {
public:
							LsSha256();

			void			Update(const void* data, size_t length);
			void			Final(uint8_t hash[32]);

			// Hex string, lowercase, 64 chars + NUL.
			void			FinalHex(char out[65]);

	static	void			Hash(const void* data, size_t length,
								uint8_t hash[32]);
	static	void			HashHex(const void* data, size_t length,
								char out[65]);

private:
			void			_ProcessBlock(const uint8_t* block);

			uint32_t		fState[8];
			uint64_t		fBitCount;
			uint8_t			fBuffer[64];
			size_t			fBufferUsed;
};

} // namespace LocalSend

#endif // _LS_SHA256_H
