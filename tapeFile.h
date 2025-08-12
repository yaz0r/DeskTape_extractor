#pragma once

#include <stdint.h>
#include <string>
#include <assert.h>
#include <array>

class tapeFile {
public:
	virtual ~tapeFile() {}
	virtual bool open(const char*) = 0;
	int getNumSectors() {
		return m_numSectors;
	}
	virtual void seekToSector(int) = 0;
	virtual uint64_t tellPosition() = 0;
	virtual void seekToPosition(uint64_t) = 0;
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

	virtual void readSector(int sectorIndex, std::array<uint8_t, 0x200>& output) = 0;

protected:
	uint32_t m_numSectors = 0;
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
	virtual uint64_t tellPosition() override {
		return _ftelli64(m_file);
	}
	virtual void seekToPosition(uint64_t position) override {
		_fseeki64(m_file, position, SEEK_SET);
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
	virtual void readSector(int sectorIndex, std::array<uint8_t, 0x200>& output) override {
		seekToSector(sectorIndex);
		fread(output.data(), 1, 0x200, m_file);
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
		int64_t size = _ftelli64(m_file);
		fseek(m_file, 0, SEEK_SET);
		m_numSectors = size / 0x211;
		assert(m_numSectors * 0x211 + 0x12 == size);
		m_currentPosition = 0;
		seekToSector(0);
		return true;
	}
	virtual uint64_t tellPosition() override {
		assert(_ftelli64(m_file) == m_currentPosition);
		int64_t absolutePosition = m_currentPosition;
		absolutePosition -= 0x10;
		int64_t numSectors = absolutePosition / 0x211;
		return absolutePosition % 0x211 + numSectors * 0x200;
	}
	virtual void seekToPosition(uint64_t position) override {
		assert(_ftelli64(m_file) == m_currentPosition);
		int64_t sector = position / 0x200;
		int positionInSector = position % 0x200;
		int64_t filePosition = 0x10;
		filePosition += sector * 0x211;
		filePosition += positionInSector;
		_fseeki64(m_file, filePosition, SEEK_SET);
		m_currentPosition = filePosition;
	}
	virtual void seekToSector(int sector) {
		assert(_ftelli64(m_file) == m_currentPosition);
		_fseeki64(m_file, (uint64_t)sector * 0x211 + 0x10, SEEK_SET);
		m_currentPosition = (uint64_t)sector * 0x211 + 0x10;
	}
	virtual uint8_t readU8() {
		if (distanceToEndOfSector() == 0) {
			assert(_ftelli64(m_file) == m_currentPosition);
			fseek(m_file, 0x11, SEEK_CUR); // skip over inter-sector data
			m_currentPosition += 0x11;
		}
		uint8_t value;
		size_t numByteRead = fread(&value, 1, 1, m_file);
		m_currentPosition++;
		assert(numByteRead == 1);
		return value;
	}

	virtual void readSector(int sectorIndex, std::array<uint8_t, 0x200>& output) override {
		assert(_ftelli64(m_file) == m_currentPosition);
		seekToSector(sectorIndex);
		assert(_ftelli64(m_file) == m_currentPosition);
		fread(output.data(), 1, 0x200, m_file);
		m_currentPosition += 0x200;
	}
private:
	int64_t distanceToEndOfSector() {
		assert(_ftelli64(m_file) == m_currentPosition);
		int64_t position = m_currentPosition;
		int64_t currentSector = position / 0x211;
		int64_t endOfSectorPosition = currentSector * 0x211 + 0x210;
		int64_t distance = endOfSectorPosition - position;
		assert(distance >= 0);
		return distance;
	}
	FILE* m_file = nullptr;
	int64_t m_currentPosition = 0;
};