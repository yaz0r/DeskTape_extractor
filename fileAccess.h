#pragma once

#include <stdio.h>
#include <string>

uint8_t readU8(FILE* fHandle);
uint16_t readU16_BE(FILE* fHandle);
uint32_t readU32_BE(FILE* fHandle);
uint64_t readU64_BE(FILE* fHandle);
std::string readPascalFixedString(FILE* fHandle, int size);
std::string readPascalString(FILE* fHandle);
std::string readString(FILE* fHandle, int size);
