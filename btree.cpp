#define _CRT_SECURE_NO_WARNINGS

#include "btree.h"
#include "fileAccess.h"
#include <assert.h>
#include <filesystem>
#include <array>

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

void sNode::seekToRecord(FILE* fHandle, int recordIndex) {
	fseek(fHandle, m_startPositionOnDisk + m_recordOffsets[recordIndex], SEEK_SET);
}

void readHeaderNode(FILE* fHandle, sNode& newNode) {
	newNode.seekToRecord(fHandle, 0);

	sHeaderNode& bTreeHeaderRecord = newNode.m_headerNode;
	bTreeHeaderRecord.treeDepth = readU16_BE(fHandle);
	bTreeHeaderRecord.rootNode = readU32_BE(fHandle);
	bTreeHeaderRecord.leafRecords = readU32_BE(fHandle);
	bTreeHeaderRecord.firstLeafNode = readU32_BE(fHandle);
	bTreeHeaderRecord.lastLeafNode = readU32_BE(fHandle);
	bTreeHeaderRecord.nodeSize = readU16_BE(fHandle);
	bTreeHeaderRecord.maxKeyLength = readU16_BE(fHandle);
	bTreeHeaderRecord.totalNodes = readU32_BE(fHandle);
	bTreeHeaderRecord.freeNodes = readU32_BE(fHandle);
	bTreeHeaderRecord.reserved1 = readU16_BE(fHandle);
	bTreeHeaderRecord.clumpSize = readU32_BE(fHandle);
	bTreeHeaderRecord.btreeType = readU8(fHandle);
	bTreeHeaderRecord.reserved2 = readU8(fHandle);
	bTreeHeaderRecord.attributes = readU32_BE(fHandle);
}

void readLeafNode(FILE* fHandle, sNode& newNode) {
	newNode.m_leafNode.resize(newNode.m_numRecords);
	for (int i = 0; i < newNode.m_numRecords; i++) {
		newNode.seekToRecord(fHandle, i);

		sLeafNode& leafRecord = newNode.m_leafNode[i];

		uint8_t keySize = readU8(fHandle);
		leafRecord.m_key.resize(keySize);
		for (int i = 0; i < keySize; i++) {
			leafRecord.m_key[i] = readU8(fHandle);
		}

		// alignment
		while (ftell(fHandle) & 1) {
			readU8(fHandle);
		}

		leafRecord.m_type = readU8(fHandle);
		uint8_t padding = readU8(fHandle); assert(padding == 0);
		switch (leafRecord.m_type) {
		case 1: // FolderRecord
			leafRecord.m_FolderRecord.m_flags = readU16_BE(fHandle);
			leafRecord.m_FolderRecord.m_numEntries = readU16_BE(fHandle);
			leafRecord.m_FolderRecord.m_id = readU32_BE(fHandle);
			leafRecord.m_FolderRecord.m_creationTime = readU32_BE(fHandle);
			leafRecord.m_FolderRecord.m_modificationTime = readU32_BE(fHandle);
			leafRecord.m_FolderRecord.m_backupTime = readU32_BE(fHandle);
			for (int i = 0; i < 16; i++) leafRecord.m_FolderRecord.m_folderInfo[i] = readU8(fHandle);
			for (int i = 0; i < 16; i++) leafRecord.m_FolderRecord.m_extendedFolderInfo[i] = readU8(fHandle);
			for (int i = 0; i < 4; i++) leafRecord.m_FolderRecord.m_reserved[i] = readU32_BE(fHandle);
			break;
		case 2: // FileRecord
			leafRecord.m_FileRecord.m_flags = readU8(fHandle);
			leafRecord.m_FileRecord.m_fileType = readU8(fHandle);
			for(int i=0; i<16; i++) leafRecord.m_FileRecord.m_fileInfo[i] = readU8(fHandle);
			leafRecord.m_FileRecord.m_id = readU32_BE(fHandle);
			leafRecord.m_FileRecord.m_dataForkBlockNumber = readU16_BE(fHandle);
			leafRecord.m_FileRecord.m_dataForkBlockSize = readU32_BE(fHandle);
			leafRecord.m_FileRecord.m_dataForkBlockAllocatedSize = readU32_BE(fHandle);
			leafRecord.m_FileRecord.m_resourceForkBlockNumber = readU16_BE(fHandle);
			leafRecord.m_FileRecord.m_resourceForkBlockSize = readU32_BE(fHandle);
			leafRecord.m_FileRecord.m_resourceForkBlockAllocatedSize = readU32_BE(fHandle);
			leafRecord.m_FileRecord.m_creationTime = readU32_BE(fHandle);
			leafRecord.m_FileRecord.m_modificationTime = readU32_BE(fHandle);
			leafRecord.m_FileRecord.m_backupTime = readU32_BE(fHandle);
			for (int i = 0; i < 16; i++) leafRecord.m_FileRecord.m_extendedFileInfo[i] = readU8(fHandle);
			leafRecord.m_FileRecord.m_clumpSize = readU16_BE(fHandle);
			for (int i = 0; i < 3; i++) leafRecord.m_FileRecord.m_firstDataForkExtents[i] = readU32_BE(fHandle);
			for (int i = 0; i < 3; i++) leafRecord.m_FileRecord.m_firstResourceForkExtents[i] = readU32_BE(fHandle);
			leafRecord.m_FileRecord.m_reserved = readU32_BE(fHandle);
			break;
		case 3: // FolderThread
			fseek(fHandle, 8, SEEK_CUR); // unknown
			leafRecord.m_FolderThread.m_parentCNID = readU32_BE(fHandle);
			leafRecord.m_FolderThread.m_name = readPascalString(fHandle);
			break;
		default:
			assert(0);
		}
	}
}

void readIndexNode(FILE* fHandle, sNode& newNode) {
	newNode.m_indexNode.resize(newNode.m_numRecords);
	for (int i = 0; i < newNode.m_numRecords; i++) {
		newNode.seekToRecord(fHandle, i);

		sIndexNode& indexNode = newNode.m_indexNode[i];

		uint8_t keySize = readU8(fHandle);
		indexNode.m_key.resize(keySize);
		for (int i = 0; i < keySize; i++) {
			indexNode.m_key[i] = readU8(fHandle);
		}
		indexNode.m_value = readU32_BE(fHandle);
	}
}

void readNode(FILE* fHandle, sNode& newNode) {
	newNode.m_startPositionOnDisk = ftell(fHandle);

	// node descriptor
	newNode.m_next = readU32_BE(fHandle);
	newNode.m_previous = readU32_BE(fHandle);
	newNode.m_type = readU8(fHandle);
	newNode.m_level = readU8(fHandle);
	newNode.m_numRecords = readU16_BE(fHandle);
	newNode.m_reserved = readU16_BE(fHandle);

	// record offsets are located at the end in reverse order
	newNode.m_recordOffsets.resize(newNode.m_numRecords + 1, -1); // because last offset is start of free space
	for (int i = 0; i < newNode.m_numRecords + 1; i++) {
		fseek(fHandle, newNode.m_startPositionOnDisk + 0x200 - 2 * (i+1), SEEK_SET); // this assume nodes are 512 bytes
		newNode.m_recordOffsets[i] = readU16_BE(fHandle);
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

bool bTree::read(FILE* fHandle) {
	uint32_t headerNodePosition = ftell(fHandle);

	m_nodes.resize(1);
	readNode(fHandle, m_nodes[0]);
	assert(m_nodes[0].m_type == 1);
	assert(m_nodes[0].m_headerNode.nodeSize == 0x200); // HFS is always 512, we assume it to be the case in various places

	m_nodes.resize(m_nodes[0].m_headerNode.totalNodes);

	for (int i = 1; i < m_nodes[0].m_headerNode.totalNodes; i++) {
		fseek(fHandle, headerNodePosition + i * m_nodes[0].m_headerNode.nodeSize, SEEK_SET);
		readNode(fHandle, m_nodes[i]);
	}

	m_fHandle = fHandle;

	return true;
}

void bTree::dump(const std::string& outputPath) {
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

								fseek(m_fHandle, (extentStart - 0x26) * 0x9800 + 0x1000, SEEK_SET);
								for (int j = 0; j < extentSize; j++) {
									std::array<uint8_t, 0x9800> buffer;
									uint32_t sizeToWrite = std::min<uint32_t>(amountLeft, 0x9800);
									fread(buffer.data(), 1, sizeToWrite, m_fHandle);
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