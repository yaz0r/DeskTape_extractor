#include "fileAccess.h"
#include <assert.h>

uint8_t readU8(FILE* fHandle) {
	uint8_t value;
	int size = fread(&value, 1, 1, fHandle);
	assert(size == 1);
	return value;
}

uint16_t readU16_BE(FILE* fHandle) {
	uint16_t value;
	int size = fread(&value, 2, 1, fHandle);
	assert(size == 1);
	return _byteswap_ushort(value);
}

uint32_t readU32_BE(FILE* fHandle) {
	uint32_t value;
	int size = fread(&value, 4, 1, fHandle);
	assert(size == 1);
	return _byteswap_ulong(value);
}

uint64_t readU64_BE(FILE* fHandle) {
	uint64_t value;
	int size = fread(&value, 8, 1, fHandle);
	assert(size == 1);
	return _byteswap_uint64(value);
}

std::string readPascalFixedString(FILE* fHandle, int size) {
	std::string string = "";

	uint8_t realStringSize = readU8(fHandle);

	for (int i = 0; i < size; i++) {
		uint8_t character = readU8(fHandle);
		if (i < realStringSize) {
			string += character;
		}
		else {
			assert(character == 0);
		}
	}

	return string;
}

std::string readPascalString(FILE* fHandle) {
	std::string string = "";

	uint8_t realStringSize = readU8(fHandle);

	for (int i = 0; i < realStringSize; i++) {
		uint8_t character = readU8(fHandle);
		string += character;
	}

	return string;
}

std::string readString(FILE* fHandle, int size) {
	std::string string = "";

	uint32_t stringStart = ftell(fHandle);
	for (int i = 0; i < size; i++) {
		uint8_t character = readU8(fHandle);
		if (character) {
			string += character;
		}
		else {
			break;
		}

	}

	fseek(fHandle, stringStart + size, SEEK_SET);
	return string;
}