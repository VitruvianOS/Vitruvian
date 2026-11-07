/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "LsSha256.h"

#include <string.h>

namespace LocalSend {

static const uint32_t kRoundConstants[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};


static inline uint32_t
_RotateRight(uint32_t value, uint32_t bits)
{
	return (value >> bits) | (value << (32 - bits));
}


LsSha256::LsSha256()
	:
	fBitCount(0),
	fBufferUsed(0)
{
	fState[0] = 0x6a09e667;
	fState[1] = 0xbb67ae85;
	fState[2] = 0x3c6ef372;
	fState[3] = 0xa54ff53a;
	fState[4] = 0x510e527f;
	fState[5] = 0x9b05688c;
	fState[6] = 0x1f83d9ab;
	fState[7] = 0x5be0cd19;
}


void
LsSha256::Update(const void* data, size_t length)
{
	const uint8_t* bytes = static_cast<const uint8_t*>(data);
	fBitCount += static_cast<uint64_t>(length) * 8;

	while (length > 0) {
		size_t take = 64 - fBufferUsed;
		if (take > length)
			take = length;
		memcpy(fBuffer + fBufferUsed, bytes, take);
		fBufferUsed += take;
		bytes += take;
		length -= take;

		if (fBufferUsed == 64) {
			_ProcessBlock(fBuffer);
			fBufferUsed = 0;
		}
	}
}


void
LsSha256::Final(uint8_t hash[32])
{
	uint64_t bitCount = fBitCount;

	// Padding: 0x80, zeros, then the 64-bit big-endian bit length.
	uint8_t pad = 0x80;
	Update(&pad, 1);
	pad = 0x00;
	while (fBufferUsed != 56)
		Update(&pad, 1);

	uint8_t lengthBytes[8];
	for (int i = 0; i < 8; i++)
		lengthBytes[i] = static_cast<uint8_t>(bitCount >> (56 - i * 8));
	// Update() would count these too; write them directly into the tail.
	memcpy(fBuffer + 56, lengthBytes, 8);
	_ProcessBlock(fBuffer);
	fBufferUsed = 0;

	for (int i = 0; i < 8; i++) {
		hash[i * 4 + 0] = static_cast<uint8_t>(fState[i] >> 24);
		hash[i * 4 + 1] = static_cast<uint8_t>(fState[i] >> 16);
		hash[i * 4 + 2] = static_cast<uint8_t>(fState[i] >> 8);
		hash[i * 4 + 3] = static_cast<uint8_t>(fState[i]);
	}
}


void
LsSha256::FinalHex(char out[65])
{
	uint8_t hash[32];
	Final(hash);
	static const char* digits = "0123456789abcdef";
	for (int i = 0; i < 32; i++) {
		out[i * 2 + 0] = digits[hash[i] >> 4];
		out[i * 2 + 1] = digits[hash[i] & 0xf];
	}
	out[64] = '\0';
}


/* static */ void
LsSha256::Hash(const void* data, size_t length, uint8_t hash[32])
{
	LsSha256 sha;
	sha.Update(data, length);
	sha.Final(hash);
}


/* static */ void
LsSha256::HashHex(const void* data, size_t length, char out[65])
{
	LsSha256 sha;
	sha.Update(data, length);
	sha.FinalHex(out);
}


void
LsSha256::_ProcessBlock(const uint8_t* block)
{
	uint32_t w[64];
	for (int i = 0; i < 16; i++) {
		w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24)
			| (static_cast<uint32_t>(block[i * 4 + 1]) << 16)
			| (static_cast<uint32_t>(block[i * 4 + 2]) << 8)
			| static_cast<uint32_t>(block[i * 4 + 3]);
	}
	for (int i = 16; i < 64; i++) {
		uint32_t s0 = _RotateRight(w[i - 15], 7)
			^ _RotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = _RotateRight(w[i - 2], 17)
			^ _RotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	uint32_t a = fState[0];
	uint32_t b = fState[1];
	uint32_t c = fState[2];
	uint32_t d = fState[3];
	uint32_t e = fState[4];
	uint32_t f = fState[5];
	uint32_t g = fState[6];
	uint32_t h = fState[7];

	for (int i = 0; i < 64; i++) {
		uint32_t s1 = _RotateRight(e, 6) ^ _RotateRight(e, 11)
			^ _RotateRight(e, 25);
		uint32_t ch = (e & f) ^ (~e & g);
		uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
		uint32_t s0 = _RotateRight(a, 2) ^ _RotateRight(a, 13)
			^ _RotateRight(a, 22);
		uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
		uint32_t temp2 = s0 + maj;

		h = g;
		g = f;
		f = e;
		e = d + temp1;
		d = c;
		c = b;
		b = a;
		a = temp1 + temp2;
	}

	fState[0] += a;
	fState[1] += b;
	fState[2] += c;
	fState[3] += d;
	fState[4] += e;
	fState[5] += f;
	fState[6] += g;
	fState[7] += h;
}

} // namespace LocalSend
