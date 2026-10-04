AddOn(
	kdbx
	STATIC

	SOURCES
	KdbxCrypto.c
	KdbxFile.cpp

	LIBS
	z
	crypto
	argon2
)
