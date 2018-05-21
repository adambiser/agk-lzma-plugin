#ifndef _H_LZMA_PLUGIN
#define _H_LZMA_PLUGIN

#include <vector>
#include <string>

extern std::vector<std::string> FilesToDelete;
bool SafeDelete(const char *filename);

#endif // _H_LZMA_PLUGIN