#include "main.h"
#include "helpers.h"
#include "package.h"

int main()
{
	packagesPath = "L:/v2922/packages/";

	Package Pkg("01f5", packagesPath);
	Pkg.Unpack();

	return 0;
}