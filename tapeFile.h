#pragma once

#include <stdint.h>
#include <string>
#include <assert.h>

class tapeFile {
public:
	virtual ~tapeFile() {}
	virtual bool open(const char*) = 0;
	int getNumSectors() {
		return m_numSectors;
	}
	virtual void seekToSector(int) = 0;
	virtual int tellPosition() = 0;
	virtual void seekToPosition(int) = 0;
	virtual void skip(int amountToSkip) {
		seekToPosition(tellPosition() + amountToSkip);
	}
	virtual void readBuffer(uint8_t* output, int size) {
		for (int i = 0; i < size; i++) {
			output[i] = readU8();
		}
	}
	virtual uint8_t readU8() = 0;
	virtual uint16_t readU16_BE();
	virtual uint32_t readU32_BE();
	virtual uint64_t readU64_BE();
	virtual std::string readPascalFixedString(int size);
	virtual std::string readPascalString();
	virtual std::string readString(int size);

protected:
	int m_numSectors = 0;
};

class tapeFile_raw : public tapeFile {
public:
	virtual ~tapeFile_raw() {
		fclose(m_file);
	}
	bool open(const char* path) override {
		fopen_s(&m_file, path, "rb");
		if (m_file == nullptr) {
			return false;
		}
		fseek(m_file, 0, SEEK_END);
		int size = ftell(m_file);
		fseek(m_file, 0, SEEK_SET);
		m_numSectors = size / 0x200;
		assert(m_numSectors * 0x200 == size);
		return true;
	}
	virtual int tellPosition() override {
		return ftell(m_file);
	}
	virtual void seekToPosition(int position) override {
		fseek(m_file, position, SEEK_SET);
	}
	virtual void seekToSector(int sector) override {
		fseek(m_file, sector * 0x200, SEEK_SET);
	}
	virtual uint8_t readU8() override {
		uint8_t value;
		size_t numByteRead = fread(&value, 1, 1, m_file);
		assert(numByteRead == 1);
		return value;
	}
private:
	FILE* m_file = nullptr;
};

class tapeFile_cptp : public tapeFile {
public:
	virtual ~tapeFile_cptp() {
		fclose(m_file);
	}
	bool open(const char* path) {
		fopen_s(&m_file, path, "rb");
		if (m_file == nullptr) {
			return false;
		}
		fseek(m_file, 0, SEEK_END);
		int size = ftell(m_file);
		fseek(m_file, 0, SEEK_SET);
		m_numSectors = size / 0x211;
		assert(m_numSectors * 0x211 == size);
		return true;
	}
	virtual int tellPosition() override {
		int absolutePosition = ftell(m_file);
		int numSectors = absolutePosition / 0x211;
		return absolutePosition - numSectors * 0x11;
	}
	virtual void seekToPosition(int position) override {
		int numSectors = position / 0x200;
		fseek(m_file, position + numSectors * 0x11, SEEK_SET);
	}
	virtual void seekToSector(int sector) {
		fseek(m_file, sector * 0x211, SEEK_SET);
	}
	virtual uint8_t readU8() {
		if (distanceToEndOfSector() == 0) {
			fseek(m_file, 0x11, SEEK_CUR); // skip over inter-sector data
		}
		uint8_t value;
		size_t numByteRead = fread(&value, 1, 1, m_file);
		assert(numByteRead == 1);
		return value;
	}
private:
	int distanceToEndOfSector() {
		int position = ftell(m_file);
		int currentSector = position / 0x211;
		int endOfSectorPosition = currentSector * 0x211 + 0x210;
		int distance = endOfSectorPosition - position;
		assert(distance >= 0);
		return distance;
	}
	FILE* m_file = nullptr;
};