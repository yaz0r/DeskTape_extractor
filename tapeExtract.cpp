#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>

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

	uint16_t deskTapeMagic = readU16_BE(fHandle);
	if (deskTapeMagic != 0x4454) {
		printf("Not a valid DeskTape");
		return -1;
	}
	uint32_t versionMagic = readU32_BE(fHandle);

	// Look for first session
	fseek(fHandle, 0, SEEK_END);
	uint32_t tapeSize = ftell(fHandle);
	fseek(fHandle, 0, SEEK_SET);
	while (ftell(fHandle) != tapeSize - 0x200) {
		uint32_t sessionStart = ftell(fHandle);
		if ((readU16_BE(fHandle) == 0x524D) && (readU16_BE(fHandle) == 1) && (readU16_BE(fHandle) == 1)) {
			fseek(fHandle, sessionStart + 0x2C, SEEK_SET);
			uint32_t partitionTableStart = readU32_BE(fHandle); // Doesn't seem to always work, to investigate
			partitionTableStart = sessionStart / 0x200 + 0x2;
			
			int partitionMapIndex = 0;
			while(true)
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
