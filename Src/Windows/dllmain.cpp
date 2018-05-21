#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include "../LZMA SDK/CPP/Windows/NtCheck.h"
#include <windows.h>
#include "../AGKLibraryCommands.h"
#include "LzmaPlugin.h"

BOOL APIENTRY DllMain( HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved )
{
	switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
			NT_CHECK;
			break;
		case DLL_THREAD_ATTACH:
			break;
		case DLL_THREAD_DETACH:
			break;
		case DLL_PROCESS_DETACH:
			while (FilesToDelete.size() > 0)
			{
				std::string file = FilesToDelete.back();
				FilesToDelete.pop_back();
				SafeDelete(file.c_str());
			}
			break;
	}
	return TRUE;
}

