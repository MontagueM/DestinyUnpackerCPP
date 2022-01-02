#include "package.h"

const int BLOCK_SIZE = 0x40000;

Package::Package(std::string packageID, std::string pkgsPath)
{
	packagesPath = pkgsPath;
	if (!std::filesystem::exists(packagesPath))
	{
		printf("Package path given is invalid!");
		exit(1);
	}
	packagePath = getLatestPatchIDPath(packageID);
}

std::string Package::getLatestPatchIDPath(std::string packageID)
{
	std::string fullPath = "";
	uint16_t patchID;
	int largestPatchID = -1;
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(packagesPath))
	{
		fullPath = entry.path().u8string();
		if (fullPath.find(packageID) != std::string::npos)
		{
			patchID = std::stoi(fullPath.substr(fullPath.size() - 5, 1));
			if (patchID > largestPatchID) largestPatchID = patchID;
			std::replace(fullPath.begin(), fullPath.end(), '\\', '/');
			packageName = fullPath.substr(0, fullPath.size() - 6);
			packageName = packageName.substr(packageName.find_last_of('/'));
		}
	}
	// Some strings are not covered, such as the bootstrap set so we need to do pkg checks
	if (largestPatchID == -1)
	{
		FILE* patchPkg = nullptr;
		uint16_t pkgID;
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(packagesPath))
		{
			fullPath = entry.path().u8string();


			patchPkg = _fsopen(fullPath.c_str(), "rb", _SH_DENYNO);
			//CreateFileA()
			if (patchPkg == nullptr) exit(67);
			fseek(patchPkg, 0x04, SEEK_SET);
			fread((char*)&pkgID, 1, 2, patchPkg);

			if (packageID == uint16ToHexStr(pkgID))
			{
				fseek(patchPkg, 0x20, SEEK_SET);
				fread((char*)&patchID, 1, 2, patchPkg);
				if (patchID > largestPatchID) largestPatchID = patchID;
				std::replace(fullPath.begin(), fullPath.end(), '\\', '/');
				packageName = fullPath.substr(0, fullPath.size() - 6);
				packageName = packageName.substr(packageName.find_last_of('/'));
			}
			fclose(patchPkg);
		}
	}

	return packagesPath + "/" + packageName + "_" + std::to_string(largestPatchID) + ".pkg";
}

bool Package::readHeader()
{
	// Package data
	pkgFile = _fsopen(packagePath.c_str(), "rb", _SH_DENYNO);
	if (pkgFile == nullptr)
	{
		return false;
	}
	fseek(pkgFile, 0x04, SEEK_SET);
	fread((char*)&header.pkgID, 1, 2, pkgFile);

	fseek(pkgFile, 0x20, SEEK_SET);
	fread((char*)&header.patchID, 1, 2, pkgFile);

	// Entry Table
	fseek(pkgFile, 0xB4, SEEK_SET);
	fread((char*)&header.entryTableSize, 1, 4, pkgFile);
	fread((char*)&header.entryTableOffset, 1, 4, pkgFile);

	// Block Table
	fseek(pkgFile, 0xD0, SEEK_SET);
	fread((char*)&header.blockTableSize, 1, 4, pkgFile);
	fread((char*)&header.blockTableOffset, 1, 4, pkgFile);

	return true;
}

void Package::getEntryTable()
{
	for (uint32_t i = header.entryTableOffset; i < header.entryTableOffset + header.entryTableSize * 16; i += 16)
	{
		Entry entry;

		// EntryA
		uint32_t entryA;
		fseek(pkgFile, i, SEEK_SET);
		fread((char*)&entryA, 1, 4, pkgFile);
		entry.reference = uint32ToHexStr(entryA);

		// EntryB
		uint32_t entryB;
		fread((char*)&entryB, 1, 4, pkgFile);
		entry.numType = (entryB >> 9) & 0x7F;
		entry.numSubType = (entryB >> 6) & 0x7;

		// EntryC
		uint32_t entryC;
		fread((char*)&entryC, 1, 4, pkgFile);
		entry.startingBlock = entryC & 0x3FFF;
		entry.startingBlockOffset = ((entryC >> 14) & 0x3FFF) << 4;

		// EntryD
		uint32_t entryD;
		fread((char*)&entryD, 1, 4, pkgFile);
		entry.fileSize = (entryD & 0x3FFFFFF) << 4 | (entryC >> 28) & 0xF;

		entries.push_back(entry);
	}
}

void Package::getBlockTable()
{
	for (uint32_t i = header.blockTableOffset; i < header.blockTableOffset + header.blockTableSize * 0x20; i += 0x20)
	{
		Block block = { 0, 0, 0, 0, 0 };
		fseek(pkgFile, i, SEEK_SET);
		fread((char*)&block.offset, 1, 4, pkgFile);
		fread((char*)&block.size, 1, 4, pkgFile);
		fread((char*)&block.patchID, 1, 2, pkgFile);
		fread((char*)&block.bitFlag, 1, 2, pkgFile);
		fseek(pkgFile, 0x14, SEEK_CUR); // SHA-1 Hash
		blocks.push_back(block);
	}
}

void Package::extractFiles()
{
	std::vector<std::string> pkgPatchStreamPaths;
	std::string outputPath = CUSTOM_DIR + "/output/" + uint16ToHexStr(header.pkgID);
	std::filesystem::create_directories(outputPath);
	// Initialising the required file streams
	for (int i = 0; i <= header.patchID; i++)
	{
		std::string pkgPatchPath = packagePath;
		pkgPatchPath[pkgPatchPath.size() - 5] = char(i + 48);
		pkgPatchStreamPaths.push_back(pkgPatchPath);
		std::cout << pkgPatchPath << "\n";
	}
	// Extracting each entry to a file
	for (int i = 0; i < entries.size(); i++)
	{
		Entry entry = entries[i];
		int currentBlockID = entry.startingBlock;
		int blockCount = floor((entry.startingBlockOffset + entry.fileSize - 1) / BLOCK_SIZE);
		if (entry.fileSize == 0) blockCount = 0; // Stupid check for weird C++ floor behaviour
		int lastBlockID = currentBlockID + blockCount;
		unsigned char* fileBuffer = new unsigned char[entry.fileSize];
		int currentBufferOffset = 0;
		while (currentBlockID <= lastBlockID)
		{
			Block currentBlock = blocks[currentBlockID];

			FILE* pFile;
			pFile = _fsopen(pkgPatchStreamPaths[currentBlock.patchID].c_str(), "rb", _SH_DENYNO);
			fseek(pFile, currentBlock.offset, SEEK_SET);
			unsigned char* blockBuffer = new unsigned char[currentBlock.size];
			size_t result;
			result = fread(blockBuffer, 1, currentBlock.size, pFile);
			if (result != currentBlock.size) { fputs("Reading error", stderr); exit(3); }

			unsigned char* decompBuffer = new unsigned char[BLOCK_SIZE];

			if (currentBlock.bitFlag & 0x1)
				decompressBlock(currentBlock, blockBuffer, decompBuffer);
			else
				decompBuffer = blockBuffer;

			if (currentBlockID == entry.startingBlock)
			{
				size_t cpySize;
				if (currentBlockID == lastBlockID)
					cpySize = entry.fileSize;
				else
					cpySize = BLOCK_SIZE - entry.startingBlockOffset;
				memcpy(fileBuffer, decompBuffer + entry.startingBlockOffset, cpySize);
				currentBufferOffset += cpySize;
			}
			else if (currentBlockID == lastBlockID)
			{
				memcpy(fileBuffer + currentBufferOffset, decompBuffer, entry.fileSize - currentBufferOffset);
			}
			else
			{
				memcpy(fileBuffer + currentBufferOffset, decompBuffer, BLOCK_SIZE);
				currentBufferOffset += BLOCK_SIZE;
			}

			fclose(pFile);
			currentBlockID++;
			delete[] decompBuffer;
		}

		FILE* oFile;
		std::string name = outputPath + "/" + uint16ToHexStr(header.pkgID) + "-" + uint16ToHexStr(i) + ".bin";
		oFile = _fsopen(name.c_str(), "rb", _SH_DENYNO);
		fwrite(fileBuffer, entry.fileSize, 1, oFile);
		fclose(oFile);
		delete[] fileBuffer;
	}
}

void Package::decompressBlock(Block block, unsigned char* decryptBuffer, unsigned char*& decompBuffer)
{
	int64_t result = ((OodleLZ64_DecompressDef)OodleLZ_Decompress)(decryptBuffer, block.size, decompBuffer, BLOCK_SIZE, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, 3);
	if (result <= 0)
		auto a = 0;
	delete[] decryptBuffer;
}

bool Package::initOodle()
{
	hOodleDll = LoadLibrary(L"oo2core_3_win64.dll");
	if (hOodleDll == nullptr) {
		return false;
	}
	OodleLZ_Decompress = (int64_t)GetProcAddress(hOodleDll, "OodleLZ_Decompress");
	if (!OodleLZ_Decompress) printf("Failed to find Oodle compress/decompress functions in DLL!");
	return true;
}

bool Package::Unpack()
{
	readHeader();
	if (!initOodle())
	{
		printf("\nFailed to initialise oodle");
		return 1;
	}
	getEntryTable();
	getBlockTable();
	fclose(pkgFile);
	extractFiles();
	return 0;
}

// Most efficient route to getting a single entry's reference
std::string Package::getEntryReference(std::string hash)
{
	// Entry index
	uint32_t id = hexStrToUint32(hash) % 8192;

	// Entry offset
	uint32_t entryTableOffset;
	pkgFile = _fsopen(packagePath.c_str(), "rb", _SH_DENYNO);
	if (pkgFile == nullptr)
	{
		printf("\nFailed to initialise pkg file, exiting...\n");
		std::cerr << hash << " " << packagePath.c_str() << std::endl << packagePath << std::endl;
		return "";
		//exit(status);
	}
	fseek(pkgFile, 0xB8, SEEK_SET);
	fread((char*)&entryTableOffset, 1, 4, pkgFile);

	// Getting reference
	uint32_t entryA;
	fseek(pkgFile, entryTableOffset + id * 16, SEEK_SET);
	fread((char*)&entryA, 1, 4, pkgFile);
	std::string reference = uint32ToHexStr(entryA);
	fclose(pkgFile);
	return reference;
}

uint8_t Package::getEntryTypes(std::string hash, uint8_t& subType)
{
	// Entry index
	uint32_t id = hexStrToUint32(hash) % 8192;

	// Entry offset
	uint32_t entryTableOffset;
	pkgFile = _fsopen(packagePath.c_str(), "rb", _SH_DENYNO);
	if (pkgFile == nullptr)
	{
		printf("\nFailed to initialise pkg file, exiting...\n");
		std::cerr << hash << std::endl << packagePath;
		return -1;
		//exit(1);
	}
	fseek(pkgFile, 0xB8, SEEK_SET);
	fread((char*)&entryTableOffset, 1, 4, pkgFile);

	// Getting reference
	// EntryB
	uint32_t entryB;
	fseek(pkgFile, entryTableOffset + id * 16 + 4, SEEK_SET);
	fread((char*)&entryB, 1, 4, pkgFile);
	uint8_t type = (entryB >> 9) & 0x7F;
	subType = (entryB >> 6) & 0x7;
	fclose(pkgFile);
	return type;
}

// This gets the minimum required data to pull out a single file from the game
unsigned char* Package::getEntryData(std::string hash, int& fileSize)
{
	// Entry index
	uint32_t id = hexStrToUint32(hash) % 8192;

	// Header data
	if (header.pkgID == 0)
	{
		bool status = readHeader();
		if (!status) return nullptr;
	}

	if (id >= header.entryTableSize) return nullptr;

	Entry entry;

	// EntryC
	uint32_t entryC;
	fseek(pkgFile, header.entryTableOffset + id * 16 + 8, SEEK_SET);
	fread((char*)&entryC, 1, 4, pkgFile);
	entry.startingBlock = entryC & 0x3FFF;
	entry.startingBlockOffset = ((entryC >> 14) & 0x3FFF) << 4;

	// EntryD
	uint32_t entryD;
	fread((char*)&entryD, 1, 4, pkgFile);
	entry.fileSize = (entryD & 0x3FFFFFF) << 4 | (entryC >> 28) & 0xF;
	fileSize = entry.fileSize;

	// Getting data to return
	if (!initOodle())
	{
		printf("\nFailed to initialise oodle, exiting...");
		exit(1);
	}

	unsigned char* buffer = getBufferFromEntry(entry);
	fclose(pkgFile);
	return buffer;
}

// For batch extraction
std::vector<std::string> Package::getAllFilesGivenRef(std::string reference)
{
	//uint32_t ref = hexStrToUint32(reference);
	std::vector<std::string> hashes;

	// Header data
	bool status = readHeader();
	if (!status) return std::vector<std::string>();

	getEntryTable();
	for (int i = 0; i < entries.size(); i++)
	{
		Entry entry = entries[i];
		if (entry.reference == reference)
		{
			uint32_t a = header.pkgID * 8192;
			uint32_t b = a + i + 2155872256;
			hashes.push_back(uint32ToHexStr(b));
		}
	}

	return hashes;
}

unsigned char* Package::getBufferFromEntry(Entry entry)
{
	if (!entry.fileSize) return nullptr;
	int blockCount = floor((entry.startingBlockOffset + entry.fileSize - 1) / BLOCK_SIZE);

	// Getting required block data
	for (uint32_t i = header.blockTableOffset + entry.startingBlock * 0x20; i <= header.blockTableOffset + entry.startingBlock * 0x20 + blockCount * 0x20; i += 0x20)
	{
		Block block = { 0, 0, 0, 0, 0 };
		fseek(pkgFile, i, SEEK_SET);
		fread((char*)&block.offset, 1, 4, pkgFile);
		fread((char*)&block.size, 1, 4, pkgFile);
		fread((char*)&block.patchID, 1, 2, pkgFile);
		fread((char*)&block.bitFlag, 1, 2, pkgFile);
		fseek(pkgFile, i + 0x20, SEEK_SET);
		blocks.push_back(block);
	}

	unsigned char* fileBuffer = new unsigned char[entry.fileSize];
	int currentBufferOffset = 0;
	int currentBlockID = 0;
	for (const Block& currentBlock : blocks) // & here is good as it captures by const reference, cheaper than by value
	{
		packagePath[packagePath.size() - 5] = currentBlock.patchID + 48;
		FILE* pFile;
		pFile = _fsopen(packagePath.c_str(), "rb", _SH_DENYNO);

		fseek(pFile, currentBlock.offset, SEEK_SET);
		unsigned char* blockBuffer = new unsigned char[currentBlock.size];
		size_t result;
		result = fread(blockBuffer, 1, currentBlock.size, pFile);
		if (result != currentBlock.size) { fputs("Reading error", stderr); exit(3); }

		unsigned char* decompBuffer = new unsigned char[BLOCK_SIZE];

		if (currentBlock.bitFlag & 0x1)
			decompressBlock(currentBlock, blockBuffer, decompBuffer);
		else
			decompBuffer = blockBuffer;

		if (currentBlockID == 0)
		{
			size_t cpySize;
			if (currentBlockID == blockCount)
				cpySize = entry.fileSize;
			else
				cpySize = BLOCK_SIZE - entry.startingBlockOffset;
			memcpy(fileBuffer, decompBuffer + entry.startingBlockOffset, cpySize);
			currentBufferOffset += cpySize;
		}
		else if (currentBlockID == blockCount)
		{
			memcpy(fileBuffer + currentBufferOffset, decompBuffer, entry.fileSize - currentBufferOffset);
		}
		else
		{
			memcpy(fileBuffer + currentBufferOffset, decompBuffer, BLOCK_SIZE);
			currentBufferOffset += BLOCK_SIZE;
		}

		fclose(pFile);
		currentBlockID++;
		delete[] decompBuffer;
	}
	blocks.clear();
	return fileBuffer;
}