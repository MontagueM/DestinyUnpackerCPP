#include "package.h"


const static unsigned char AES_KEY_0[16] =
{
	0xD6, 0x2A, 0xB2, 0xC1, 0x0C, 0xC0, 0x1B, 0xC5, 0x35, 0xDB, 0x7B, 0x86, 0x55, 0xC7, 0xDC, 0x3B,
};

const static unsigned char AES_KEY_1[16] =
{
	0x3A, 0x4A, 0x5D, 0x36, 0x73, 0xA6, 0x60, 0x58, 0x7E, 0x63, 0xE6, 0x76, 0xE4, 0x08, 0x92, 0xB5,
};

unsigned char nonce[12] =
{
	0x84, 0xEA, 0x11, 0xC0, 0xAC, 0xAB, 0xFA, 0x20, 0x33, 0x11, 0x26, 0x99,
};


Package::Package(std::string packagePath)
{
	Package::packagePath = packagePath;
}

void Package::readHeader()
{
	// Package data
	fopen_s(&pkgFile, packagePath.c_str(), "rb");
	fseek(pkgFile, 0x10, SEEK_SET);
	fread((char*)&header.pkgID, 1, 2, pkgFile);

	fseek(pkgFile, 0x30, SEEK_SET);
	fread((char*)&header.patchID, 1, 2, pkgFile);

	// Entry Table
	fseek(pkgFile, 0x44, SEEK_SET);
	fread((char*)&header.entryTableOffset, 1, 4, pkgFile);

	fseek(pkgFile, 0x60, SEEK_SET);
	fread((char*)&header.entryTableSize, 1, 4, pkgFile);

	// Block Table

	fseek(pkgFile, 0x68, SEEK_SET);
	fread((char*)&header.blockTableSize, 1, 4, pkgFile);
	fread((char*)&header.blockTableOffset, 1, 4, pkgFile);

	// Hash64 Table
	fseek(pkgFile, 0xB8, SEEK_SET);
	fread((char*)&header.hash64TableSize, 1, 4, pkgFile);
	fread((char*)&header.hash64TableOffset, 1, 4, pkgFile);
	header.hash64TableOffset += 64; // relative offset
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
		entry.reference = uint32ToHexStr(swapUInt32Endianness(entryA));

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
	for (uint32_t i = header.blockTableOffset; i < header.blockTableOffset + header.blockTableSize * 48; i += 48)
	{
		Block block = {0, 0, 0, 0, 0};
		fseek(pkgFile, i, SEEK_SET);
		fread((char*)&block.offset, 1, 4, pkgFile);
		fread((char*)&block.size, 1, 4, pkgFile);
		fread((char*)&block.patchID, 1, 2, pkgFile);
		fread((char*)&block.bitFlag, 1, 2, pkgFile);
		fseek(pkgFile, i+0x20, SEEK_SET);
		fread((char*)&block.gcmTag, 16, 1, pkgFile);
		blocks.push_back(block);
	}
}

void Package::modifyNonce()
{
	// Nonce
	nonce[0] ^= (header.pkgID >> 8) & 0xFF;
	nonce[11] ^= header.pkgID & 0xFF;
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
		pkgPatchPath[pkgPatchPath.size() - 5] = char(i+48);
		pkgPatchStreamPaths.push_back(pkgPatchPath);
		std::cout << pkgPatchPath << "\n";
	}
	// Extracting each entry to a file
	for (int i = 0; i < entries.size(); i++)
	{
		Entry entry = entries[i];
		int currentBlockID = entry.startingBlock;
		int blockCount = floor((entry.startingBlockOffset + entry.fileSize - 1) / BLOCK_SIZE);
		int lastBlockID = currentBlockID + blockCount;
		unsigned char* fileBuffer = new unsigned char[entry.fileSize];
		int currentBufferOffset = 0;
		while (currentBlockID <= lastBlockID)
		{
			Block currentBlock = blocks[currentBlockID];

			FILE* pFile;
			fopen_s(&pFile, pkgPatchStreamPaths[currentBlock.patchID].c_str(), "rb");
			fseek(pFile, currentBlock.offset, SEEK_SET);
			unsigned char* blockBuffer = new unsigned char[currentBlock.size];
			size_t result;
			result = fread(blockBuffer, 1, currentBlock.size, pFile);
			if (result != currentBlock.size) { fputs("Reading error", stderr); exit(3); }

			unsigned char* decryptBuffer = new unsigned char[currentBlock.size];
			unsigned char* decompBuffer = new unsigned char[BLOCK_SIZE];
			
			if (currentBlock.bitFlag & 0x2)
				decryptBlock(currentBlock, blockBuffer, decryptBuffer);
			else
				decryptBuffer = blockBuffer;

			if (currentBlock.bitFlag & 0x1)
				decompressBlock(currentBlock, decryptBuffer, decompBuffer);
			else
				decompBuffer = decryptBuffer;

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
		fopen_s(&oFile, name.c_str(), "wb");
		fwrite(fileBuffer, entry.fileSize, 1, oFile);
		fclose(oFile);
		delete[] fileBuffer;
	}
}

// Bcrypt decryption implementation largely from Sir Kane's SourcePublic_v2.cpp, very mysterious
void Package::decryptBlock(Block block, unsigned char* blockBuffer, unsigned char*& decryptBuffer)
{
	BCRYPT_ALG_HANDLE hAesAlg;
	NTSTATUS status;
	status = BCryptOpenAlgorithmProvider(&hAesAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
	status = BCryptSetProperty(hAesAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
		sizeof(BCRYPT_CHAIN_MODE_GCM), 0);

	alignas(alignof(BCRYPT_KEY_DATA_BLOB_HEADER)) unsigned char keyData[sizeof(BCRYPT_KEY_DATA_BLOB_HEADER) + 16];
	BCRYPT_KEY_DATA_BLOB_HEADER* pHeader = (BCRYPT_KEY_DATA_BLOB_HEADER*)keyData;
	pHeader->dwMagic = BCRYPT_KEY_DATA_BLOB_MAGIC;
	pHeader->dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
	pHeader->cbKeyData = 16;
	memcpy(pHeader+1, block.bitFlag & 0x4 ? AES_KEY_1 : AES_KEY_0, 16);
	BCRYPT_KEY_HANDLE hAesKey;

	status = BCryptImportKey(hAesAlg, nullptr, BCRYPT_KEY_DATA_BLOB, &hAesKey, nullptr, 0, keyData, sizeof(keyData), 0);
	ULONG decryptionResult;
	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO cipherModeInfo;

	BCRYPT_INIT_AUTH_MODE_INFO(cipherModeInfo);

	cipherModeInfo.pbTag = (PUCHAR)block.gcmTag;
	cipherModeInfo.cbTag = 0x10;
	cipherModeInfo.pbNonce = nonce;
	cipherModeInfo.cbNonce = sizeof(nonce);
	 
	status = BCryptDecrypt(hAesKey, (PUCHAR)blockBuffer, (ULONG)block.size, &cipherModeInfo, nullptr, 0,
		(PUCHAR)decryptBuffer, (ULONG)block.size, &decryptionResult, 0);
	if (status < 0)
		printf("bcrypt decryption failed!");
	BCryptDestroyKey(hAesKey);
	BCryptCloseAlgorithmProvider(hAesAlg, 0);

	delete[] blockBuffer;
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
	hOodleDll = LoadLibrary(L"I:/oo2core_8_win64.dll");
	if (hOodleDll == nullptr) {
		return false;
	}
	OodleLZ_Decompress = (int64_t)GetProcAddress(hOodleDll, "OodleLZ_Decompress");
	if (!OodleLZ_Decompress) printf("Failed to find Oodle compress/decompress functions in DLL!");
}

bool Package::Unpack()
{
	readHeader();
	if (!initOodle())
	{
		printf("Failed to initialise oodle");
		return 1;
	}
	modifyNonce();
	getEntryTable();
	getBlockTable();
	fclose(pkgFile);
	extractFiles();
	return 0;
}