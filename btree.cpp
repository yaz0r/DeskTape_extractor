#define _CRT_SECURE_NO_WARNINGS

#include "btree.h"
#include "fileAccess.h"
#include <assert.h>
#include <filesystem>
#include <array>

#include "tapeFile.h"

// https://developer.apple.com/library/archive/technotes/tn/tn1150.html#BTrees
// https://github.com/libyal/libfshfs/blob/main/documentation/Hierarchical%20File%20System%20(HFS).asciidoc

uint32_t sLeafNode::getParentCNID() {
	uint32_t CNID = 0;
	for (int i = 0; i < 4; i++) {
		CNID += m_key[1 + i] * (1 << 8 * (3 - i));
	}

	return CNID;
}

std::string sLeafNode::getName() {
	std::string string;

	uint8_t length = m_key[5];
	for (int i = 0; i < length; i++) {
		string += m_key[6 + i];
	}

	return string;
}

void sNode::seekToRecord(tapeFile* fHandle, int recordIndex) {
	fHandle->seekToPosition(m_startPositionOnDisk + m_recordOffsets[recordIndex]);
}

void readHeaderNode(tapeFile* fHandle, sNode& newNode) {
	newNode.seekToRecord(fHandle, 0);

	sHeaderNode& bTreeHeaderRecord = newNode.m_headerNode;
	bTreeHeaderRecord.treeDepth = fHandle->readU16_BE();
	bTreeHeaderRecord.rootNode = fHandle->readU32_BE();
	bTreeHeaderRecord.leafRecords = fHandle->readU32_BE();
	bTreeHeaderRecord.firstLeafNode = fHandle->readU32_BE();
	bTreeHeaderRecord.lastLeafNode = fHandle->readU32_BE();
	bTreeHeaderRecord.nodeSize = fHandle->readU16_BE();
	bTreeHeaderRecord.maxKeyLength = fHandle->readU16_BE();
	bTreeHeaderRecord.totalNodes = fHandle->readU32_BE();
	bTreeHeaderRecord.freeNodes = fHandle->readU32_BE();
	bTreeHeaderRecord.reserved1 = fHandle->readU16_BE();
	bTreeHeaderRecord.clumpSize = fHandle->readU32_BE();
	bTreeHeaderRecord.btreeType = fHandle->readU8();
	bTreeHeaderRecord.reserved2 = fHandle->readU8();
	bTreeHeaderRecord.attributes = fHandle->readU32_BE();
}

void readLeafNode(tapeFile* fHandle, sNode& newNode) {
	newNode.m_leafNode.resize(newNode.m_numRecords);
	for (int i = 0; i < newNode.m_numRecords; i++) {
		newNode.seekToRecord(fHandle, i);

		sLeafNode& leafRecord = newNode.m_leafNode[i];

		uint8_t keySize = fHandle->readU8();
		leafRecord.m_key.resize(keySize);
		for (int i = 0; i < keySize; i++) {
			leafRecord.m_key[i] = fHandle->readU8();
		}

		// alignment
		while (fHandle->tellPosition() & 1) {
			fHandle->readU8();
		}

		leafRecord.m_type = fHandle->readU8();
		uint8_t padding = fHandle->readU8(); assert(padding == 0);
		switch (leafRecord.m_type) {
		case 1: // FolderRecord
			leafRecord.m_FolderRecord.m_flags = fHandle->readU16_BE();
			leafRecord.m_FolderRecord.m_numEntries = fHandle->readU16_BE();
			leafRecord.m_FolderRecord.m_id = fHandle->readU32_BE();
			leafRecord.m_FolderRecord.m_creationTime = fHandle->readU32_BE();
			leafRecord.m_FolderRecord.m_modificationTime = fHandle->readU32_BE();
			leafRecord.m_FolderRecord.m_backupTime = fHandle->readU32_BE();
			for (int i = 0; i < 16; i++) leafRecord.m_FolderRecord.m_folderInfo[i] = fHandle->readU8();
			for (int i = 0; i < 16; i++) leafRecord.m_FolderRecord.m_extendedFolderInfo[i] = fHandle->readU8();
			for (int i = 0; i < 4; i++) leafRecord.m_FolderRecord.m_reserved[i] = fHandle->readU32_BE();
			break;
		case 2: // FileRecord
			leafRecord.m_FileRecord.m_flags = fHandle->readU8();
			leafRecord.m_FileRecord.m_fileType = fHandle->readU8();
			for(int i=0; i<16; i++) leafRecord.m_FileRecord.m_fileInfo[i] = fHandle->readU8();
			leafRecord.m_FileRecord.m_id = fHandle->readU32_BE();
			leafRecord.m_FileRecord.m_dataForkBlockNumber = fHandle->readU16_BE();
			leafRecord.m_FileRecord.m_dataForkBlockSize = fHandle->readU32_BE();
			leafRecord.m_FileRecord.m_dataForkBlockAllocatedSize = fHandle->readU32_BE();
			leafRecord.m_FileRecord.m_resourceForkBlockNumber = fHandle->readU16_BE();
			leafRecord.m_FileRecord.m_resourceForkBlockSize = fHandle->readU32_BE();
			leafRecord.m_FileRecord.m_resourceForkBlockAllocatedSize = fHandle->readU32_BE();
			leafRecord.m_FileRecord.m_creationTime = fHandle->readU32_BE();
			leafRecord.m_FileRecord.m_modificationTime = fHandle->readU32_BE();
			leafRecord.m_FileRecord.m_backupTime = fHandle->readU32_BE();
			for (int i = 0; i < 16; i++) leafRecord.m_FileRecord.m_extendedFileInfo[i] = fHandle->readU8();
			leafRecord.m_FileRecord.m_clumpSize = fHandle->readU16_BE();
			for (int i = 0; i < 3; i++) leafRecord.m_FileRecord.m_firstDataForkExtents[i] = fHandle->readU32_BE();
			for (int i = 0; i < 3; i++) leafRecord.m_FileRecord.m_firstResourceForkExtents[i] = fHandle->readU32_BE();
			leafRecord.m_FileRecord.m_reserved = fHandle->readU32_BE();
			break;
		case 3: // FolderThread
		case 4: // FileThread
			fHandle->skip(8); // unknown
			leafRecord.m_FolderOrFileThread.m_parentCNID = fHandle->readU32_BE();
			leafRecord.m_FolderOrFileThread.m_name = fHandle->readPascalString();
			break;
		default:
			assert(0);
		}
	}
}

void readIndexNode(tapeFile* fHandle, sNode& newNode) {
	newNode.m_indexNode.resize(newNode.m_numRecords);
	for (int i = 0; i < newNode.m_numRecords; i++) {
		newNode.seekToRecord(fHandle, i);

		sIndexNode& indexNode = newNode.m_indexNode[i];

		uint8_t keySize = fHandle->readU8();
		indexNode.m_key.resize(keySize);
		for (int i = 0; i < keySize; i++) {
			indexNode.m_key[i] = fHandle->readU8();
		}
		indexNode.m_value = fHandle->readU32_BE();
	}
}

void readNode(tapeFile* fHandle, sNode& newNode) {
	newNode.m_startPositionOnDisk = fHandle->tellPosition();

	// node descriptor
	newNode.m_next = fHandle->readU32_BE();
	newNode.m_previous = fHandle->readU32_BE();
	newNode.m_type = fHandle->readU8();
	newNode.m_level = fHandle->readU8();
	newNode.m_numRecords = fHandle->readU16_BE();
	newNode.m_reserved = fHandle->readU16_BE();

	// record offsets are located at the end in reverse order
	newNode.m_recordOffsets.resize(newNode.m_numRecords + 1, -1); // because last offset is start of free space
	for (int i = 0; i < newNode.m_numRecords + 1; i++) {
		fHandle->seekToPosition(newNode.m_startPositionOnDisk + 0x200 - 2 * (i+1)); // this assume nodes are 512 bytes
		newNode.m_recordOffsets[i] = fHandle->readU16_BE();
	}

	switch (newNode.m_type) {
	case 0xFF:
		readLeafNode(fHandle, newNode);
		break;
	case 0x0:
		readIndexNode(fHandle, newNode);
		break;
	case 1:
		readHeaderNode(fHandle, newNode);
		break;
	default:
		assert(0);
	}
}

std::string normalizeFilename(std::string& name) {
	// Normalize name
	while (name[0] == ' ') {
		name = name.substr(1, name.size() - 1);
	}
	for (int i = 0; i < name.size(); i++) {
		if (name[i] == '/') {
			name[i] = '_';
		}
	}
	return name;
}

std::string bTree::getFolderPath(uint32_t CNID) {
	for (int i = 1; i < m_nodes[0].m_headerNode.totalNodes; i++) {
		sNode& currentNode = m_nodes[i];
		if (currentNode.m_type == 0xFF) {
			// leaf node
			for (int j = 0; j < currentNode.m_leafNode.size(); j++) {
				auto& leafNodeRecord = currentNode.m_leafNode[j];
				if (leafNodeRecord.m_type == 1) {
					if(leafNodeRecord.m_FolderRecord.m_id == CNID)
					{
						// Folder
						uint32_t parentCNID = leafNodeRecord.getParentCNID();
						std::string name = leafNodeRecord.getName();

						name = normalizeFilename(name);

						if (parentCNID == 1) {
							return name;
						}

						return getFolderPath(parentCNID) + "/" + name;
					}
				}
			}
		}
	}
}

bool bTree::read(tapeFile* fHandle) {
	uint32_t headerNodePosition = fHandle->tellPosition();

	m_nodes.resize(1);
	readNode(fHandle, m_nodes[0]);
	assert(m_nodes[0].m_type == 1);
	assert(m_nodes[0].m_headerNode.nodeSize == 0x200); // HFS is always 512, we assume it to be the case in various places

	m_nodes.resize(m_nodes[0].m_headerNode.totalNodes);

	for (int i = 1; i < m_nodes[0].m_headerNode.totalNodes; i++) {
		fHandle->seekToPosition(headerNodePosition + i * m_nodes[0].m_headerNode.nodeSize);
		readNode(fHandle, m_nodes[i]);
	}

	return true;
}

void bTree::dump(tapeFile* fHandle, const std::string& outputPath) {
	sNode& rootNode = m_nodes[m_nodes[0].m_headerNode.rootNode];

	for (int i = 1; i < m_nodes[0].m_headerNode.totalNodes; i++) {
		sNode& currentNode = m_nodes[i];
		if (currentNode.m_type == 0xFF) {
			// leaf node
			for (int j = 0; j < currentNode.m_leafNode.size(); j++) {
				auto& leafNodeRecord = currentNode.m_leafNode[j];
				if (leafNodeRecord.m_type == 2) {
					// File
					uint32_t parentCNID = leafNodeRecord.getParentCNID();
					std::string name = leafNodeRecord.getName();

					std::string gfolderPath = outputPath + getFolderPath(parentCNID);

					if (leafNodeRecord.m_FileRecord.m_dataForkBlockAllocatedSize) {
						std::filesystem::create_directories(gfolderPath.c_str());

						std::string outputFileName = gfolderPath + "/" + normalizeFilename(name);
						FILE* fOutput = fopen(outputFileName.c_str(), "wb+");
						if (fOutput) {
							uint32_t amountLeft = leafNodeRecord.m_FileRecord.m_dataForkBlockSize;
							for (int i = 0; i < 3; i++) {
								uint16_t extentStart = leafNodeRecord.m_FileRecord.m_firstDataForkExtents[i] >> 16;
								uint16_t extentSize = leafNodeRecord.m_FileRecord.m_firstDataForkExtents[i] & 0xFFFF;

								fHandle->seekToPosition((extentStart - 0x26) * 0x9800 + 0x1000);
								for (int j = 0; j < extentSize; j++) {
									std::array<uint8_t, 0x9800> buffer;
									uint32_t sizeToWrite = std::min<uint32_t>(amountLeft, 0x9800);
									fHandle->readBuffer(buffer.data(), sizeToWrite);
									fwrite(buffer.data(), 1, sizeToWrite, fOutput);

									amountLeft -= sizeToWrite;
								}
								if (amountLeft == 0) {
									break;
								}
							}
							//assert(amountLeft == 0);
							fclose(fOutput);
						}
					}

					printf("%s/%s 0x%08X/0x%08X\n", gfolderPath.c_str(), name.c_str(), leafNodeRecord.m_FileRecord.m_firstDataForkExtents[0], leafNodeRecord.m_FileRecord.m_firstResourceForkExtents[0]);
				}
			}
		}
	}
}

void bTree::dumpLeafNodes(const std::string& outputFileName) {

	if (FILE* fHandle = fopen(outputFileName.c_str(), "w+")) {
		sNode& rootNode = m_nodes[m_nodes[0].m_headerNode.rootNode];

		for (int i = 1; i < m_nodes[0].m_headerNode.totalNodes; i++) {
			sNode& currentNode = m_nodes[i];
			if (currentNode.m_type == 0xFF) {
				// leaf node
				for (int j = 0; j < currentNode.m_leafNode.size(); j++) {
					auto& leafNodeRecord = currentNode.m_leafNode[j];
					if (leafNodeRecord.m_type == 2) {
						// File
						uint32_t parentCNID = leafNodeRecord.getParentCNID();
						std::string name = leafNodeRecord.getName();

						fprintf(fHandle, "%s 0x%08X/0x%08X\n", name.c_str(), leafNodeRecord.m_FileRecord.m_firstDataForkExtents[0], leafNodeRecord.m_FileRecord.m_firstResourceForkExtents[0]);
					}
				}
			}
		}
	}
}