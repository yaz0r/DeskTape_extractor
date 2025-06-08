#pragma once

#include "stdio.h"
#include <stdint.h>
#include <vector>
#include <string>

struct sLeafNode {
	std::vector<uint8_t> m_key;
	uint8_t m_type;

	uint32_t getParentCNID();
	std::string getName();

	struct {
		uint16_t m_flags;
		uint16_t m_numEntries;
		uint32_t m_id;

		uint32_t m_creationTime;
		uint32_t m_modificationTime;
		uint32_t m_backupTime;

		uint8_t m_folderInfo[16];
		uint8_t m_extendedFolderInfo[16];
		uint32_t m_reserved[4];
	} m_FolderRecord;
	struct {
		uint8_t m_flags;
		uint8_t m_fileType;
		uint8_t m_fileInfo[16];
		uint32_t m_id;
		
		uint16_t m_dataForkBlockNumber;
		uint32_t m_dataForkBlockSize;
		uint32_t m_dataForkBlockAllocatedSize;

		uint16_t m_resourceForkBlockNumber;
		uint32_t m_resourceForkBlockSize;
		uint32_t m_resourceForkBlockAllocatedSize;

		uint32_t m_creationTime;
		uint32_t m_modificationTime;
		uint32_t m_backupTime;

		uint8_t m_extendedFileInfo[16];
		uint16_t m_clumpSize;
		uint32_t m_firstDataForkExtents[3];
		uint32_t m_firstResourceForkExtents[3];
		uint32_t m_reserved;
	} m_FileRecord;
	struct {
		uint32_t m_parentCNID;
		std::string m_name;
	} m_FolderThread;
};

struct sIndexNode {
	std::vector<uint8_t> m_key;
	uint32_t m_value;
};

struct sHeaderNode {
	uint16_t treeDepth;
	uint32_t rootNode;
	uint32_t leafRecords;
	uint32_t firstLeafNode;
	uint32_t lastLeafNode;
	uint16_t nodeSize;
	uint16_t maxKeyLength;
	uint32_t totalNodes;
	uint32_t freeNodes;
	uint16_t reserved1;
	uint32_t clumpSize;
	uint8_t btreeType;
	uint8_t reserved2;
	uint32_t attributes;
	uint32_t reserved3[16];
};

struct sNode {
	uint32_t m_startPositionOnDisk;
	void seekToRecord(FILE* fHandle, int recordIndex);

	// Node Descriptor
	uint32_t m_next;
	uint32_t m_previous;
	uint8_t m_type;
	uint8_t m_level;
	uint16_t m_numRecords;
	uint16_t m_reserved;

	std::vector<uint16_t> m_recordOffsets;

	std::vector<sLeafNode> m_leafNode;
	std::vector<sIndexNode> m_indexNode;
	sHeaderNode m_headerNode;
};

class bTree {
public:
	bool read(FILE* fHandle);
	void dump(const std::string& outputPath);
	void dumpLeafNodes(const std::string& outputFileName);

	std::vector<sNode> m_nodes;

	std::string getFolderPath(uint32_t CNID);
	FILE* m_fHandle = nullptr;
};
