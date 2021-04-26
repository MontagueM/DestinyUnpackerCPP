#include "main.h"
#include "helpers.h"
#include "package.h"

namespace fs = std::filesystem;

bool getLatestPatchIDs_Str(std::vector<std::string>& latestPackages, std::string packagesPath)
{
	std::vector<std::string> allPackages;
	std::unordered_map<int, int> patchIDMap;
	std::string fullPath;
	std::string reducedPath;
	int patchID;
	std::string pkgID;

	// Getting all packages
	for (const fs::directory_entry& entry : fs::directory_iterator(packagesPath))
	{
		fullPath = entry.path().u8string();	

		// Removing the .pkg and before /
		reducedPath = fullPath.substr(0, fullPath.size() - 4);
		reducedPath = reducedPath.substr(reducedPath.find_last_of('/'));
		reducedPath = reducedPath.substr(1);

		// Getting patch ID
		patchID = reducedPath.at(reducedPath.size() - 1) - 48;

		// Getting pkgID, need to check if theres a language or not
		if (reducedPath.at(reducedPath.size() - 5) == '_')
		{
			pkgID = reducedPath.substr(reducedPath.size() - 9, 4);
		}
		else
		{
			pkgID = reducedPath.substr(reducedPath.size() - 6, 4);
		}


		//std::cout << reducedPath << "  " << pkgID << "  " << patchID << "\n";
	}

	return true;
}

bool getLatestPatchIDs_Bin(std::vector<std::string>& latestPackages, std::string packagesPath)
{
	std::vector<std::string> allPackages;
	// Map from pkg name to max patchID
	std::unordered_map<std::string, int> patchIDMap;
	// Map from 
	std::unordered_map<std::string, std::string> pkgIDMap;
	std::string previousReducedPath = "";

	std::string fullPath;
	std::string reducedPath;
	int patchID;
	uint16_t pkgIDBytes = 0;
	std::string pkgID;
	std::ifstream pkgFile;


	// Getting all packages
	for (const fs::directory_entry& entry : fs::directory_iterator(packagesPath))
	{
		// PkgID from .pkg as gets better nums
		fullPath = entry.path().u8string();
		pkgFile.open(fullPath, std::ifstream::in | std::ifstream::binary);
		pkgFile.seekg(0x10, std::ifstream::beg);
		pkgFile.read((char*)&pkgIDBytes, std::ifstream::beg + 2);
		pkgID = uint16ToHexStr(pkgIDBytes);


		// Getting patch ID from name of pkg
		reducedPath = fullPath.substr(0, fullPath.size() - 4);
		reducedPath = reducedPath.substr(reducedPath.find_last_of('/'));
		reducedPath = reducedPath.substr(1);

		patchID = reducedPath.at(reducedPath.size() - 1) - 48;

		//std::cout << pkgID << "  " << patchID << "\n";
		pkgFile.close();

		if (patchID > patchIDMap[pkgID])
			patchIDMap[pkgID] = patchID;

		if (previousReducedPath != reducedPath)
		{
			if (previousReducedPath != "")
			{
				latestPackages.push_back(fullPath);
			}
			previousReducedPath = reducedPath;
		}

	}


	return true;
}

/*
This function assumes the directory is sorted so that each patch number increments up
*/
std::vector<std::string> getLatestPatchIDs(std::string packagesPath)
{
	std::vector<std::string> latestPackages;
	std::string previousReducedPath = "";
	std::string previousFullPath = "";
	std::string fullPath;
	std::string reducedPath;

	// Getting all packages
	for (const fs::directory_entry& entry : fs::directory_iterator(packagesPath))
	{
		fullPath = entry.path().u8string();
		reducedPath = fullPath.substr(0, fullPath.size() - 6);
		//std::cout << fullPath << "\n";
		if (previousReducedPath != reducedPath)
		{
			if (previousReducedPath != "")
			{
				latestPackages.push_back(previousFullPath);
			}
		}
		previousReducedPath = reducedPath;
		previousFullPath = fullPath;
	}

	return latestPackages;
}


int main()
{
	packagesPath = "I:/SteamLibrary/steamapps/common/Destiny 2/packages/";
	
	//std::vector<std::string> latestPackages;
	//bool bGotLatestPatchIDs = getLatestPatchIDs_Str(latestPackages, packagesPath);
	//bool bGotLatestPatchIDs = getLatestPatchIDs_Bin(latestPackages, packagesPath);
	std::vector<std::string> latestPackages = getLatestPatchIDs(packagesPath);

	for (std::string path : latestPackages)
	{
		std::cout << "Unpacking " << path << "\n";
		Package Pkg(path);
		Pkg.Unpack();
	}

	//std::string debugPackagePath = latestPackages[0];
	//Package debugPackage(debugPackagePath);
	//debugPackage.Unpack();

	return 0;
}