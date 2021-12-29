#include "main.h"
#include "helpers.h"
#include "package.h"

int main()
{
	packagesPath = "P:/D1/packages/";

	Package Pkg("0409", packagesPath);
	Pkg.Unpack();

	return 0;
}