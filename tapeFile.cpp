#include "tapeFile.h"

uint16_t tapeFile::readU16_BE() {
	union {
		uint16_t value;
		uint8_t data[2];
	};
	for (int i = sizeof(data) - 1; i >= 0; i--) {
		data[i] = readU8();
	}
	return value;
}

uint32_t tapeFile::readU32_BE() {
	union {
		uint32_t value;
		uint8_t data[4];
	};
	for (int i = sizeof(data) - 1; i >= 0; i--) {
		data[i] = readU8();
	}
	return value;
}

uint64_t tapeFile::readU64_BE() {
	union {
		uint32_t value;
		uint8_t data[8];
	};
	for (int i = sizeof(data) - 1; i >= 0; i--) {
		data[i] = readU8();
	}
	return value;
}

std::string tapeFile::readPascalFixedString(int size) {
	std::string string = "";

	uint8_t realStringSize = readU8();

	for (int i = 0; i < size; i++) {
		uint8_t character = readU8();
		if (i < realStringSize) {
			string += character;
		}
		else {
			//assert(character == 0);
		}
	}

	return string;
}

std::string tapeFile::readPascalString() {
	std::string string = "";

	uint8_t realStringSize = readU8();

	for (int i = 0; i < realStringSize; i++) {
		uint8_t character = readU8();
		string += character;
	}

	return string;
}

std::string tapeFile::readString(int size) {
	std::string string = "";

	for (int i = 0; i < size; i++) {
		uint8_t character = readU8();
		if (character) {
			string += character;
		}
	}
	return string;
}