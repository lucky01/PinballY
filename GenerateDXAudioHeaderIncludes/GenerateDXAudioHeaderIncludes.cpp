// This is a custom build tool that generates hard-coded #include
// paths for including the legacy DirectX SDK files required to
// compile DirectXTK Audio for Windows Vista/7 compatibility.  To
// compile in this mode, we have to hard-code the directory paths
// to the DX SDK header files in some #include's.  Yuck.  This
// tool makes the whole mess slightly less brittle by at least
// generating them from an environment variable as part of the
// build process.

#include "stdafx.h"
#include "stdlib.h"

int main()
{
	char *path = getenv("DXSDK_DIR");
	if (path == 0)
	{
		fprintf(stderr, "Error: environment variable DXSDK_DIR isn't defined\n");
		fprintf(stderr, "Define DXSDK_DIR=<path to DirectX legacy SDK>\\ - path MUST end with \\\n");
		exit(2);
	}

	printf("// THIS FILE IS GENERATED by GenerateDXAudioHeaderIncludes.cpp\n");
	printf("// Do not edit\n");
	printf("#include <%sInclude\\comdecl.h>\n", path);
	printf("#include <%sInclude\\xaudio2.h>\n", path);
	printf("#include <%sInclude\\xaudio2fx.h>\n", path);
	printf("#include <%sInclude\\xapofx.h>\n", path);
	printf("#pragma warning(push)\n");
	printf("#pragma warning( disable : 4005 )\n");
	printf("#include <%sInclude\\x3daudio.h>\n", path);
	printf("#pragma warning(pop)\n");
	
	return 0;
}

