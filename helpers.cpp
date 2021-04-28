#include "helpers.h"

std::string uint16ToHexStr(uint16_t num)
{
	std::stringstream stream;
	stream << std::hex << num;
	std::string hexStr = stream.str();
	if (hexStr.size() % 4 != 0)
		hexStr = std::string(4 - (hexStr.size() % 4), '0').append(hexStr);
	return hexStr;
}

std::string uint32ToHexStr(uint32_t num)
{
	std::stringstream stream;
	stream << std::hex << num;
	std::string hexStr = stream.str();
	if (hexStr.size() % 8 != 0)
		hexStr = std::string(8 - (hexStr.size() % 8), '0').append(hexStr);
	return hexStr;
}

uint32_t hexStrToUint32(std::string hash)
{
	return swapUInt32Endianness(std::stoul(hash, nullptr, 16));
}

uint32_t swapUInt32Endianness(uint32_t x)
{
	x = (x >> 24) |
		((x << 8) & 0x00FF0000) |
		((x >> 8) & 0x0000FF00) |
		(x << 24);
	return x;
}