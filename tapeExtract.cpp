#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <array>
#include <regex>
#include <filesystem>

#include "btree.h"
#include "fileAccess.h"

struct sSession {
	uint32_t m_sessionStartSector;

	uint16_t m_magic; // always 0x524D 'RM'
	uint16_t m_sessionID;
	uint16_t m_sessionID2;
	uint16_t m_unk6;
	uint16_t m_unk8;
	uint16_t m_numSpans;
	uint32_t m_unkC;
	uint32_t m_unk10;
	uint32_t m_unk14;
	uint16_t m_unk18; // \ Those are related to the 4 bytes at start of archive offset 4
	uint16_t m_unk1A; // /
	uint32_t m_unk1C; // always 0x20000
	std::array<uint8_t, 8> m_TDVersionName;
	uint32_t m_previousSession;
	uint32_t m_currentSession;
	uint32_t m_numSystemSectors;
	uint32_t m_unk34;

	struct sPan {
		uint32_t m0;
		uint32_t m4;
	};
	std::vector<sPan> m_spans;

};

std::vector<uint8_t> getDTDiskInfo(int sessionIndex, std::vector<sSession>& sessions, tapeFile* fHandle) {
	fHandle->seekToPosition(sessions[sessionIndex].m_sessionStartSector * 0x200);
	uint64_t sessionStart = fHandle->tellPosition();
	if (fHandle->readU16_BE() == 0x524D) {
		fHandle->seekToPosition(sessionStart + 0x400);
		uint64_t partitionTableStart = fHandle->tellPosition() / 0x200;

		int partitionMapIndex = 0;
		while (true)
		{
			fHandle->seekToPosition((partitionTableStart + partitionMapIndex) * 0x200);
			uint16_t pmSig = fHandle->readU16_BE(); assert(pmSig == 0x504D); // signature 
			fHandle->readU16_BE(); // padding
			uint32_t pmMapBlkCnt = fHandle->readU32_BE(); /* partition blocks count */
			uint32_t pmPyPartStart = fHandle->readU32_BE(); /* physical block start of partition */
			uint32_t pmPartBlkCnt = fHandle->readU32_BE(); /* physical block count of partition */
			std::string pmPartName = fHandle->readString(32); // partition name
			std::string pmPartType = fHandle->readString(32); // partition type

			if (pmPartType == "Apple_Data") {
				std::vector<uint8_t> data;
				data.resize(pmPartBlkCnt * 0x200);
				fHandle->seekToSector(partitionTableStart - 1 + pmPyPartStart);
				fHandle->readBuffer(data.data(), data.size());
				return data;
			}
			partitionMapIndex++;
			if (partitionMapIndex >= pmMapBlkCnt) {
				return std::vector<uint8_t>();
			}
		}
	}

	return std::vector<uint8_t>();
}

int64_t getHFSStartSector(int sessionIndex, std::vector<sSession>& sessions, tapeFile* fHandle) {
	fHandle->seekToPosition(sessions[sessionIndex].m_sessionStartSector * 0x200);
	uint64_t sessionStart = fHandle->tellPosition();
	if (fHandle->readU16_BE() == 0x524D) {
		fHandle->seekToPosition(sessionStart + 0x400);
		uint64_t partitionTableStart = fHandle->tellPosition() / 0x200;

		int partitionMapIndex = 0;
		while (true)
		{
			fHandle->seekToPosition((partitionTableStart + partitionMapIndex) * 0x200);
			uint16_t pmSig = fHandle->readU16_BE(); assert(pmSig == 0x504D); // signature 
			fHandle->readU16_BE(); // padding
			uint32_t pmMapBlkCnt = fHandle->readU32_BE(); /* partition blocks count */
			uint32_t pmPyPartStart = fHandle->readU32_BE(); /* physical block start of partition */
			uint32_t pmPartBlkCnt = fHandle->readU32_BE(); /* physical block count of partition */
			std::string pmPartName = fHandle->readString(32); // partition name
			std::string pmPartType = fHandle->readString(32); // partition type

			if (pmPartType == "Apple_HFS") {
				return (partitionTableStart - 1 + pmPyPartStart);
			}
			partitionMapIndex++;
			if (partitionMapIndex >= pmMapBlkCnt) {
				return -1;
			}
		}
	}

	return -1;
}

std::optional<bTree> getCatalogSession(int sessionIndex, std::vector<sSession>& sessions, tapeFile* fHandle) {
	int64_t HFS_Start = getHFSStartSector(sessionIndex, sessions, fHandle);
	if(HFS_Start == -1)
		return std::optional<bTree>();

	fHandle->seekToSector(HFS_Start);

	// read HFS boot block
	uint64_t bootBlockPosition = fHandle->tellPosition();
	{
		uint16_t bootBlockSignature = fHandle->readU16_BE();

		// boot block can be null if disk is non-bootable (some tapes have been somehow set as bootable)
		if (bootBlockSignature == 0x4C4B)
		{
			uint32_t bootCodeEntryPoint = fHandle->readU32_BE(); assert(bootCodeEntryPoint == 0x60000086);
			uint16_t bootBlocksVersionNumber = fHandle->readU16_BE(); assert(bootBlocksVersionNumber == 0x4418);
			uint16_t pageFlags = fHandle->readU16_BE();
			std::string systemFilename = fHandle->readPascalFixedString(15);
			std::string finderFilename = fHandle->readPascalFixedString(15);
			std::string debugger1Filename = fHandle->readPascalFixedString(15);
			std::string debugger2Filename = fHandle->readPascalFixedString(15);
			std::string startupScreenFilename = fHandle->readPascalFixedString(15);
			std::string startupProgramFilename = fHandle->readPascalFixedString(15);
			std::string scrapFilename = fHandle->readPascalFixedString(15);

			uint16_t numAllocatedFileControlBlocks = fHandle->readU16_BE();
			uint16_t numMaxEventQueueElements = fHandle->readU16_BE();
			uint32_t systemHeap128K = fHandle->readU32_BE();
			uint32_t systemHeap256K = fHandle->readU32_BE();
			uint32_t systemHeapOther = fHandle->readU32_BE();
			fHandle->readU16_BE();
			uint32_t systemHeapSpace = fHandle->readU32_BE();
			uint32_t fractionHeapFree = fHandle->readU32_BE();
		}
	}
	// read the MDB (Master Directory Block)
	fHandle->seekToPosition(bootBlockPosition + 0x400);
	{
		uint16_t signature = fHandle->readU16_BE();
		assert(signature == 0x4244);
		{
			uint32_t volumeCreationTime = fHandle->readU32_BE();
			uint32_t volumeModificationTime = fHandle->readU32_BE();
			uint16_t volumeAttributeFlags = fHandle->readU16_BE();
			uint16_t numFilesInRoot = fHandle->readU16_BE();
			uint16_t volumeBitmapBlockNumber = fHandle->readU16_BE();
			uint16_t startOfNextAllocationSearch = fHandle->readU16_BE();
			uint16_t numAllocationBlocks = fHandle->readU16_BE();
			uint32_t allocationBlockSize = fHandle->readU32_BE();
			uint32_t defaultClump = fHandle->readU32_BE();
			uint16_t extentsStartBlockNumber = fHandle->readU16_BE();
			uint32_t nextAvailableCatalogNodeIdentifier = fHandle->readU32_BE();
			uint16_t numUnusedAllocationBlocks = fHandle->readU16_BE();
			std::string volumeName = fHandle->readPascalFixedString(27);
			uint32_t lastBackupTime = fHandle->readU32_BE();
			uint16_t backupSequenceNumber = fHandle->readU16_BE();
			uint32_t volumeWriteCount = fHandle->readU32_BE();
			uint32_t clumpSizeForExtentsFile = fHandle->readU32_BE();
			uint32_t clumpSizeForCatalogFile = fHandle->readU32_BE();
			uint16_t numSubDirInRoot = fHandle->readU16_BE();
			uint32_t totalNumberOfFiles = fHandle->readU32_BE();
			uint32_t totalNumberOfFolders = fHandle->readU32_BE();
			fHandle->skip(32); // skip the finder information
			uint16_t embeddedVolumeSignature = fHandle->readU16_BE();
			uint32_t embeddedVolumeDescriptor = fHandle->readU32_BE();
			uint32_t extentsFileSize = fHandle->readU32_BE();
			uint32_t extentsFileRecord0 = fHandle->readU32_BE();
			uint32_t extentsFileRecord1 = fHandle->readU32_BE();
			uint32_t extentsFileRecord2 = fHandle->readU32_BE();
			uint32_t catalogFileSize = fHandle->readU32_BE();
			uint32_t catalogFileRecord0 = fHandle->readU32_BE();
			uint32_t catalogFileRecord1 = fHandle->readU32_BE();
			uint32_t catalogFileRecord2 = fHandle->readU32_BE();

			// read the volume bitmap block
			{
				assert(volumeBitmapBlockNumber == 3);
				fHandle->seekToPosition(bootBlockPosition + 0x200 * volumeBitmapBlockNumber);

				int numBytes = (numAllocationBlocks / 8);
				if (numAllocationBlocks % 8) numBytes++;

				std::vector<uint8_t> volumeBitmap;
				volumeBitmap.resize(numBytes);

				fHandle->readBuffer(volumeBitmap.data(), numBytes);

				// For consistency
				int numSectors = numBytes / 512;
				if (numBytes % 512) numSectors++;
				assert(volumeBitmapBlockNumber + numSectors == extentsStartBlockNumber);
			}

			// Read extents
			{
				fHandle->seekToPosition(bootBlockPosition + 0x200 * extentsStartBlockNumber);
				// skip extends
				bTree extends;
				//extends.read(fHandle);
				//extends.dump(outputPath);
			}

			// Read catalog
			{
				// Seek over extends and to catalog
				fHandle->seekToPosition(bootBlockPosition + 0x200 * extentsStartBlockNumber);
				assert(((extentsFileRecord0 >> 16) & 0xFFFF) == 0);
				fHandle->skip(allocationBlockSize * (extentsFileRecord0 & 0xFFFF));

				// make sure there was no extra extents records
				assert((extentsFileRecord1 & 0xFFFF) == 0);
				assert((extentsFileRecord2 & 0xFFFF) == 0);

				bTree catalogFile;
				catalogFile.read(fHandle);
				//catalogFile.dump(outputPath);
				return catalogFile;
			}
		}

	}
}

std::vector<std::filesystem::path> FindFiles(const std::filesystem::path& root, const std::string& filePattern) {

	std::vector<std::filesystem::path> ret;

	if (std::filesystem::is_regular_file(filePattern)) {
		ret.push_back(filePattern);
		return ret;
	}

	std::filesystem::path seatchFullPattern = (std::filesystem::path(root) / filePattern).lexically_normal();

	std::string regexString = seatchFullPattern.relative_path().string();
	regexString = std::regex_replace(regexString, std::regex("\\\\"), "\\\\");
	regexString = std::regex_replace(regexString, std::regex("\\*"), ".*");
	std::regex pattern(regexString);

	std::filesystem::path searchPath = seatchFullPattern.parent_path();

	for (const auto& entry : std::filesystem::directory_iterator(searchPath)) {
		if (entry.is_regular_file()) {
			std::string entryFileName = entry.path().string();
			if (std::regex_match(entryFileName, pattern)) {
				ret.push_back(entry.path());
			}
		}
	}
	return ret;
}

int main(int argc, char** argv)
{
	if (argc < 2) {
		printf("Need input file or pattern");
		return -1;
	}
	const std::vector<std::filesystem::path> inputFiles = FindFiles("", argv[1]);
	for (int i = 0; i < inputFiles.size(); i++) {
		const std::filesystem::path inputFile = inputFiles[i];
		printf("Processing %s\n", inputFile.string().c_str());

		tapeFile* fHandle = nullptr;
		if (!_stricmp(inputFile.extension().string().c_str(), ".cptp")) {
			fHandle = new tapeFile_cptp();
		}
		else {
			fHandle = new tapeFile_raw();
		}
		if (!fHandle->open(inputFile.string().c_str())) {
			printf("Can't open file %s", inputFile);
			return -1;
		}

		std::string outputPath = "";
		if (argc > 2) {
			outputPath = argv[2];
		}
		if (outputPath.length() == 0) {
			outputPath = std::string("output\\") + inputFile.filename().string() + "\\";
		}
		std::filesystem::create_directories(outputPath);

		uint16_t deskTapeMagic = fHandle->readU16_BE();
		if (deskTapeMagic != 0x4454) {
			printf("Not a valid DeskTape");
			return -1;
		}
		uint32_t versionMagic = fHandle->readU32_BE();

		/*
		* Actually not useful, can compute that from the session header
		// Figure out where the data starts
		int32_t sectorOffset = 0;
		for (int i = 0; i < 0x20; i++) {
			fseek(fHandle, 0x200 * i, SEEK_SET);
			uint16_t magic = fHandle->readU16_BE();
			if (magic != deskTapeMagic) {
				sectorOffset = i - 0xA; // data should always start at 0x1400?
				break;
			}
		}
		*/

		// Look for last session
		uint32_t archiveSize = fHandle->getNumSectors() * 0x200;
		int32_t numSectors = archiveSize / 0x200;
		int32_t lastSessionSector = -1;
		for (uint32_t sector = numSectors - 1; sector >= 0; sector--) {
			fHandle->seekToPosition(sector * 0x200);
			if (fHandle->readU16_BE() == 0x524D) {
				lastSessionSector = sector;
				break;
			}
		}
		if (lastSessionSector == -1) {
			printf("Failed to find last session");
			return -1;
		}

		std::vector<sSession> sessions;

		// Find all sessions
		uint32_t currentSessionSector = lastSessionSector;
		while (true) {
			fHandle->seekToPosition(currentSessionSector * 0x200);
			sSession newSession;
			newSession.m_sessionStartSector = currentSessionSector;

			newSession.m_magic = fHandle->readU16_BE();
			if (newSession.m_magic != 0x524D)
				break;
			newSession.m_sessionID = fHandle->readU16_BE();
			newSession.m_sessionID2 = fHandle->readU16_BE();
			newSession.m_unk6 = fHandle->readU16_BE();
			newSession.m_unk8 = fHandle->readU16_BE(); // 1 in last session?
			newSession.m_numSpans = fHandle->readU16_BE();
			newSession.m_unkC = fHandle->readU32_BE(); // some sector number to something, looks more or less like the end of current session
			newSession.m_unk10 = fHandle->readU32_BE(); // the offset to remap file system sectors
			newSession.m_unk14 = fHandle->readU32_BE();
			newSession.m_unk18 = fHandle->readU16_BE();
			newSession.m_unk1A = fHandle->readU16_BE();
			newSession.m_unk1C = fHandle->readU32_BE();
			for (int i = 0; i < 8; i++) {
				newSession.m_TDVersionName[i] = fHandle->readU8();
			}
			newSession.m_previousSession = fHandle->readU32_BE();
			newSession.m_currentSession = fHandle->readU32_BE();
			newSession.m_numSystemSectors = fHandle->readU32_BE(); // directory size required for mounting
			newSession.m_unk34 = fHandle->readU32_BE();
			for (int i = 0; i < newSession.m_numSpans; i++) {
				auto& newSpan = newSession.m_spans.emplace_back();
				newSpan.m0 = fHandle->readU32_BE(); // in-disk offset (-0x60)
				newSpan.m4 = fHandle->readU32_BE(); // size in sectors
			}
			sessions.insert(sessions.begin(), newSession);

			if (newSession.m_previousSession == 0) {
				break;
			}
			currentSessionSector = newSession.m_previousSession - (newSession.m_currentSession - newSession.m_sessionStartSector);
		}

		// Dump sessions
		for (int i = 0; i < sessions.size(); i++)
		{
			printf("Session %i/%i\n", i, sessions.size());
			sSession& session = sessions[i];
			// Dump session data
			if (FILE* fOutput = fopen(std::format("{}/session_{}_info.txt", outputPath.c_str(), i).c_str(), "w+")) {
				fprintf(fOutput, "Session %d\n", i);
				fprintf(fOutput, "m_sessionID 0x%04X\n", session.m_sessionID);
				fprintf(fOutput, "m_sessionID2 0x%04X\n", session.m_sessionID2);
				fprintf(fOutput, "m_unk6 0x%04X\n", session.m_unk6);
				fprintf(fOutput, "m_unk8 0x%04X\n", session.m_unk8);
				fprintf(fOutput, "m_numSpans 0x%04X\n", session.m_numSpans);
				fprintf(fOutput, "m_unkC 0x%08X\n", session.m_unkC);
				fprintf(fOutput, "m_unk10 0x%08X\n", session.m_unk10);
				fprintf(fOutput, "m_unk14 0x%08X\n", session.m_unk14);
				fprintf(fOutput, "m_unk18 0x%04X\n", session.m_unk18);
				fprintf(fOutput, "m_unk1A 0x%04X\n", session.m_unk1A);
				fprintf(fOutput, "m_unk1C 0x%08X\n", session.m_unk1C);
				fprintf(fOutput, "m_TDVersionName "); for (int i = 0; i < 8; i++) { fprintf(fOutput, "%c", session.m_TDVersionName[i]); } fprintf(fOutput, "\n");
				fprintf(fOutput, "m_previousSession 0x%08X\n", session.m_previousSession);
				fprintf(fOutput, "m_currentSession 0x%08X\n", session.m_currentSession);
				fprintf(fOutput, "m_numSystemSectors 0x%08X\n", session.m_numSystemSectors);
				fprintf(fOutput, "m_unk34 0x%08X\n", session.m_unk34);
				assert(session.m_numSpans == session.m_spans.size());
				for (int j = 0; j < session.m_numSpans; j++) {
					fprintf(fOutput, "Span %d 0x%08X 0x%08X\n", j, session.m_spans[j].m0, session.m_spans[j].m4);
				}
				fclose(fOutput);
			}

			std::optional<bTree> catalogFileSession = getCatalogSession(i, sessions, fHandle);
			if (catalogFileSession.has_value()) {
				catalogFileSession->dumpLeafNodes(std::format("{}/session_{}_nodes.txt", outputPath.c_str(), i));
				//std::optional<bTree> catalogFileSessionNext = getCatalogSession(i+1, sessions, fHandle);
			}

			std::vector<uint8_t> systemSectors;
			systemSectors.resize(session.m_numSystemSectors * 0x200);

			int currentSector = session.m_sessionStartSector + 2;
			fHandle->seekToSector(currentSector);
			std::vector<uint8_t>::iterator systemSectorDestination = systemSectors.begin();
			/*
			for (int k = 0; k < session.m_numSystemSectors; k++) {
				std::array<uint8_t, 0x200> buffer;
				fHandle->readBuffer(&systemSectorDestination[0], 0x200);
				systemSectorDestination += 0x200;
			}
			*/
			for (int j = 0; j < session.m_spans.size(); j++) {
				std::vector<uint8_t>::iterator systemSectorDestination = systemSectors.begin() + session.m_spans[j].m0 * 0x200;
				for (int k = 0; k < session.m_spans[j].m4; k++) {
					std::array<uint8_t, 0x200> buffer;
					fHandle->readSector(currentSector++, buffer);
					memcpy(&(*systemSectorDestination), buffer.data(), 0x200);
					systemSectorDestination += 0x200;
				}
			}

			// Dump system sectors
			if (true) {
				std::string outputSessionSystemSectorsFileName = outputPath + "/" + "session_" + std::to_string(i) + "_system_sectors.bin";
				if (FILE* fOutputSessionSystemSectors = fopen(outputSessionSystemSectorsFileName.c_str(), "wb+")) {
					fwrite(systemSectors.data(), 1, systemSectors.size(), fOutputSessionSystemSectors);
					fclose(fOutputSessionSystemSectors);
				}
			}

			std::vector<uint8_t> DTDiskInfo = getDTDiskInfo(i, sessions, fHandle);
			uint32_t startOfData = _byteswap_ulong(*(uint32_t*)(DTDiskInfo.data() + 0x36)) + 0xA;

			// Dump the DT disk info partition
			if (FILE* fOutput = fopen(std::format("{}/session_{}_DT_diskInfo.bin", outputPath.c_str(), i).c_str(), "wb+")) {
				fwrite(DTDiskInfo.data(), 1, DTDiskInfo.size(), fOutput);
				fclose(fOutput);
			}

			// Dump the session as a .DSK
			if (i == 0)
			{
				int64_t HFSStartSector = getHFSStartSector(i, sessions, fHandle);
				if (HFSStartSector != -1) {
					HFSStartSector -= session.m_sessionStartSector + 2;
					std::string outputSessionFileName = outputPath + "/" + "session_" + std::to_string(i) + ".dsk";
					if (FILE* fOutputSession = fopen(outputSessionFileName.c_str(), "wb+")) {
						fwrite(systemSectors.data() + HFSStartSector * 0x200, 1, 0x100800, fOutputSession);

						int startOfDataSectors = startOfData;
						/*
						if (catalogFileSession.has_value()) {
							std::vector<bTree::sSortedEntry> sortedNodes = catalogFileSession->getSortedNodes();
							int firstFileSector = ((sortedNodes[0].m_startSector * 0x9800) + (1*0x9800)) / 0x200;
							startOfDataSectors = firstFileSector;
						}
						*/

						if (ftell(fOutputSession) / 0x200 <= startOfDataSectors) {
							while (ftell(fOutputSession) / 0x200 != startOfDataSectors) {
								std::array<uint8_t, 0x200> buffer;
								buffer.fill(0);
								fwrite(buffer.data(), 1, 0x200, fOutputSession);
							}

							std::array<uint8_t, 0x200> buffer;
							int startSector = (0xA - (session.m_currentSession - session.m_sessionStartSector));
							int endSector = startSector + session.m_currentSession;
							for (int k = startSector; k < endSector; k++) {
								fHandle->readSector(k, buffer);
								fwrite(buffer.data(), 1, 0x200, fOutputSession);
							}
						}

						fclose(fOutputSession);
					}
				}
			}

			/*

			fHandle->seekToPosition(session.m_sessionStartSector * 0x200 + 0x400);
			std::filesystem::create_directories(outputPath);
			std::string outputSession = outputPath + "/" + "session_" + std::to_string(i) + "_system_sectors.bin";
			if (FILE* fOutputSession = fopen(outputSession.c_str(), "wb+")) {
				// write the system sectors
				fHandle->seekToSector(session.m_sessionStartSector + 2);
				for (int j = 0; j < session.m_spans.size(); j++) {
					fseek(fOutputSession, session.m_spans[j].m0 * 0x200, SEEK_SET);
					for (int k = 0; k < session.m_spans[j].m4; k++) {
						std::array<uint8_t, 0x200> buffer;
						fHandle->readBuffer(buffer.data(), 0x200);
						fwrite(buffer.data(), 1, 0x200, fOutputSession);
					}
				}
				fclose(fOutputSession);
			}
			*/
		}


#if 0
		// Dump session as a HFS file
		for (int i = 0; i < 1; i++)
		{
			sSession& session = sessions[i];
			fHandle->seekToPosition(session.m_sessionStartSector * 0x200 + 0x400);
			std::filesystem::create_directories(outputPath);
			std::string outputSession = outputPath + "/" + "session_" + std::to_string(i) + ".HFS";
			if (FILE* fOutputSession = fopen(outputSession.c_str(), "wb+")) {
				// write the system sectors
				fHandle->seekToSector(session.m_sessionStartSector + 2);
				for (int j = 0; j < session.m_spans.size(); j++) {
					fseek(fOutputSession, session.m_spans[j].m0 * 0x200, SEEK_SET);
					for (int k = 0; k < session.m_spans[j].m4; k++) {
						std::array<uint8_t, 0x200> buffer;
						fHandle->readBuffer(buffer.data(), 0x200);
						fwrite(buffer.data(), 1, 0x200, fOutputSession);
					}
				}

				/*
				// Go to beginning of data
				fHandle->seekToPosition((0xA - (session.m_currentSession - session.m_sessionStartSector)) * 0x200);
				fseek(fOutputSession, 0xBB2 * 0x200, SEEK_SET);
				for (int k = 0; k < session.m_currentSession; k++) {
					std::array<uint8_t, 0x200> buffer;
					fHandle->readBuffer(buffer.data(), 0x200);
					fwrite(buffer.data(), 1, 0x200, fOutputSession);
				}
				*/


				fclose(fOutputSession);
			}
		}
#endif
	}

	return 0;
}
