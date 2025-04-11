#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <array>

#include "btree.h"
#include "fileAccess.h"

int main(int argc, char** argv)
{
	if (argc < 2) {
		printf("Need input file");
		return -1;
	}
	const char* inputFile = argv[1];
	FILE* fHandle = fopen(inputFile, "rb");
	if (fHandle == nullptr) {
		printf("Can't open file %s", inputFile);
		return -1;
	}

	std::string outputPath = "";
	if (argc >= 2) {
		outputPath = argv[2];
	}

	uint16_t deskTapeMagic = readU16_BE(fHandle);
	if (deskTapeMagic != 0x4454) {
		printf("Not a valid DeskTape");
		return -1;
	}
	uint32_t versionMagic = readU32_BE(fHandle);

	/*
	* Actually not useful, can compute that from the session header
	// Figure out where the data starts
	int32_t sectorOffset = 0;
	for (int i = 0; i < 0x20; i++) {
		fseek(fHandle, 0x200 * i, SEEK_SET);
		uint16_t magic = readU16_BE(fHandle);
		if (magic != deskTapeMagic) {
			sectorOffset = i - 0xA; // data should always start at 0x1400?
			break;
		}
	}
	*/

	// Look for last session
	fseek(fHandle, 0, SEEK_END);
	uint32_t archiveSize = ftell(fHandle);
	int32_t numSectors = archiveSize / 0x200;
	int32_t lastSessionSector = -1;
	for (uint32_t sector = numSectors - 1; sector >= 0; sector--) {
		fseek(fHandle, sector * 0x200, SEEK_SET);
		if (readU16_BE(fHandle) == 0x524D) {
			lastSessionSector = sector;
			break;
		}
	}
	if (lastSessionSector == -1) {
		printf("Failed to find last session");
		return -1;
	}

	struct sSession {
		uint32_t m_sessionStartSector;

		uint16_t m_magic; // always 0x524D 'RM'
		uint16_t m_sessionID;
		uint16_t m_sessionID2;
		uint16_t m_unk6;
		uint32_t m_numSpans;
		uint32_t m_unkC;
		uint32_t m_unk10;
		uint32_t m_unk14;
		uint16_t m_unk18; // \ Those are related to the 4 bytes at start of archive offset 4
		uint16_t m_unk1A; // /
		uint32_t m_unk1C; // always 0x20000
		std::array<uint8_t, 8> m_TDVersionName;
		uint32_t m_previousSession;
		uint32_t m_currentSession;
		uint32_t m_unk30;
		uint32_t m_unk34;

		struct sPan {
			uint32_t m0;
			uint32_t m4;
		};
		std::vector<sPan> m_spans;

	};
	std::vector<sSession> sessions;

	// Find all sessions
	uint32_t currentSessionSector = lastSessionSector;
	while (true) {
		fseek(fHandle, currentSessionSector * 0x200, SEEK_SET);
		sSession newSession;
		newSession.m_sessionStartSector = currentSessionSector;

		newSession.m_magic = readU16_BE(fHandle);
		newSession.m_sessionID = readU16_BE(fHandle);
		newSession.m_sessionID2 = readU16_BE(fHandle);
		newSession.m_unk6 = readU16_BE(fHandle);
		newSession.m_numSpans = readU32_BE(fHandle);
		newSession.m_unkC = readU32_BE(fHandle); // some sector number to something
		newSession.m_unk10 = readU32_BE(fHandle); // the offset to remap file system sectors
		newSession.m_unk14 = readU32_BE(fHandle);
		newSession.m_unk18 = readU16_BE(fHandle);
		newSession.m_unk1A = readU16_BE(fHandle);
		newSession.m_unk1C = readU32_BE(fHandle);
		for (int i = 0; i < 8; i++) {
			newSession.m_TDVersionName[i] = readU8(fHandle);
		}
		newSession.m_previousSession = readU32_BE(fHandle);
		newSession.m_currentSession = readU32_BE(fHandle);
		newSession.m_unk30 = readU32_BE(fHandle);
		newSession.m_unk34 = readU32_BE(fHandle);
		for (int i = 0; i < newSession.m_numSpans; i++) {
			auto& newSpan = newSession.m_spans.emplace_back();
			newSpan.m0 = readU32_BE(fHandle); // in-disk offset (-0x60)
			newSpan.m4 = readU32_BE(fHandle); // size in sectors
		}
		sessions.insert(sessions.begin(), newSession);

		if (newSession.m_previousSession == 0) {
			break;
		}
		currentSessionSector = newSession.m_previousSession - (newSession.m_currentSession - newSession.m_sessionStartSector);
	}

	// Rewrite session
	for (int i = 0; i < sessions.size(); i++)
	{
		auto& session = sessions[i];
		fseek(fHandle, session.m_sessionStartSector * 0x200 + 0x400, SEEK_SET);
		std::string outputSession = outputPath + "/" + "session_" + std::to_string(i) + ".bin";
		if (FILE* fOutputSession = fopen(outputSession.c_str(), "wb+")) {

			for (int j = 0; j < session.m_spans.size(); j++) {
				fseek(fOutputSession, session.m_spans[j].m0 * 0x200, SEEK_SET);
				for (int k = 0; k < session.m_spans[j].m4; k++) {
					std::array<uint8_t, 0x200> buffer;
					fread(buffer.data(), 1, 0x200, fHandle);
					fwrite(buffer.data(), 1, 0x200, fOutputSession);
				}
			}

			fclose(fOutputSession);
		}
	}

	for (int i = 0; i < sessions.size(); i++)
	{
		fseek(fHandle, sessions[i].m_sessionStartSector * 0x200, SEEK_SET);
		uint32_t sessionStart = ftell(fHandle);
		if ((readU16_BE(fHandle) == 0x524D) && (readU16_BE(fHandle) == 1) && (readU16_BE(fHandle) == 1)) {
			fseek(fHandle, sessionStart + 0x400, SEEK_SET);
			uint32_t partitionTableStart = ftell(fHandle) / 0x200;

			int partitionMapIndex = 0;
			while (true)
			{
				fseek(fHandle, (partitionTableStart + partitionMapIndex) * 0x200, SEEK_SET);
				uint16_t pmSig = readU16_BE(fHandle); assert(pmSig == 0x504D); // signature 
				readU16_BE(fHandle); // padding
				uint32_t pmMapBlkCnt = readU32_BE(fHandle); /* partition blocks count */
				uint32_t pmPyPartStart = readU32_BE(fHandle); /* physical block start of partition */
				uint32_t pmPartBlkCnt = readU32_BE(fHandle); /* physical block count of partition */
				std::string pmPartName = readString(fHandle, 32); // partition name
				std::string pmPartType = readString(fHandle, 32); // partition type

				if (pmPartType == "Apple_HFS") {
					uint32_t HFS_Start = (partitionTableStart - 1 + pmPyPartStart) * 0x200;
					fseek(fHandle, HFS_Start, SEEK_SET);
					// read HFS boot block
					uint32_t bootBlockPosition = ftell(fHandle);
					{
						uint16_t bootBlockSignature = readU16_BE(fHandle); assert(bootBlockSignature == 0x4C4B);
						uint32_t bootCodeEntryPoint = readU32_BE(fHandle); assert(0x86000060);
						uint16_t bootBlocksVersionNumber = readU16_BE(fHandle); assert(0x4418);
						uint16_t pageFlags = readU16_BE(fHandle);
						std::string systemFilename = readPascalFixedString(fHandle, 15);
						std::string finderFilename = readPascalFixedString(fHandle, 15);
						std::string debugger1Filename = readPascalFixedString(fHandle, 15);
						std::string debugger2Filename = readPascalFixedString(fHandle, 15);
						std::string startupScreenFilename = readPascalFixedString(fHandle, 15);
						std::string startupProgramFilename = readPascalFixedString(fHandle, 15);
						std::string scrapFilename = readPascalFixedString(fHandle, 15);

						uint16_t numAllocatedFileControlBlocks = readU16_BE(fHandle);
						uint16_t numMaxEventQueueElements = readU16_BE(fHandle);
						uint32_t systemHeap128K = readU32_BE(fHandle);
						uint32_t systemHeap256K = readU32_BE(fHandle);
						uint32_t systemHeapOther = readU32_BE(fHandle);
						readU16_BE(fHandle);
						uint32_t systemHeapSpace = readU32_BE(fHandle);
						uint32_t fractionHeapFree = readU32_BE(fHandle);
					}
					// read the MDB (Master Directory Block)
					fseek(fHandle, bootBlockPosition + 0x400, SEEK_SET);
					{
						uint16_t signature = readU16_BE(fHandle);
						assert(signature == 0x4244);
						{
							uint32_t volumeCreationTime = readU32_BE(fHandle);
							uint32_t volumeModificationTime = readU32_BE(fHandle);
							uint16_t volumeAttributeFlags = readU16_BE(fHandle);
							uint16_t numFilesInRoot = readU16_BE(fHandle);
							uint16_t volumeBitmapBlockNumber = readU16_BE(fHandle);
							uint16_t startOfNextAllocationSearch = readU16_BE(fHandle);
							uint16_t numAllocationBlocks = readU16_BE(fHandle);
							uint32_t allocationBlockSize = readU32_BE(fHandle);
							uint32_t defaultClump = readU32_BE(fHandle);
							uint16_t extentsStartBlockNumber = readU16_BE(fHandle);
							uint32_t nextAvailableCatalogNodeIdentifier = readU32_BE(fHandle);
							uint16_t numUnusedAllocationBlocks = readU16_BE(fHandle);
							std::string volumeName = readPascalFixedString(fHandle, 27);
							uint32_t lastBackupTime = readU32_BE(fHandle);
							uint16_t backupSequenceNumber = readU16_BE(fHandle);
							uint32_t volumeWriteCount = readU32_BE(fHandle);
							uint32_t clumpSizeForExtentsFile = readU32_BE(fHandle);
							uint32_t clumpSizeForCatalogFile = readU32_BE(fHandle);
							uint16_t numSubDirInRoot = readU16_BE(fHandle);
							uint32_t totalNumberOfFiles = readU32_BE(fHandle);
							uint32_t totalNumberOfFolders = readU32_BE(fHandle);
							fseek(fHandle, 32, SEEK_CUR); // skip the finder information
							uint16_t embeddedVolumeSignature = readU16_BE(fHandle);
							uint32_t embeddedVolumeDescriptor = readU32_BE(fHandle);
							uint32_t extentsFileSize = readU32_BE(fHandle);
							uint32_t extentsFileRecord0 = readU32_BE(fHandle);
							uint32_t extentsFileRecord1 = readU32_BE(fHandle);
							uint32_t extentsFileRecord2 = readU32_BE(fHandle);
							uint32_t catalogFileSize = readU32_BE(fHandle);
							uint32_t catalogFileRecord0 = readU32_BE(fHandle);
							uint32_t catalogFileRecord1 = readU32_BE(fHandle);
							uint32_t catalogFileRecord2 = readU32_BE(fHandle);

							// read the volume bitmap block
							assert(volumeBitmapBlockNumber == 3);
							fseek(fHandle, bootBlockPosition + 0x200 * volumeBitmapBlockNumber, SEEK_SET);

							// Seek to catalog
							fseek(fHandle, 0xB800, SEEK_CUR); // 0xA200

							bTree catalogFile;
							catalogFile.read(fHandle);
							catalogFile.dump(outputPath);
						}

					}
				}

				partitionMapIndex++;
				if (partitionMapIndex >= pmMapBlkCnt) {
					return 0;
				}
			}

			return 0;
		}
		fseek(fHandle, sessionStart + 0x200, SEEK_SET);
	}



	fclose(fHandle);

	return 0;
}
