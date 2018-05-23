#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include <sstream>
#include "LzmaPlugin.h"

#include "7zip.h"
//#include "../LZMA SDK/CPP//7zip/Archive/7z/7zHandler.h"

#include "../AGKLibraryCommands.h"

using namespace NWindows;
using namespace NFile;
using namespace NDir;
//using namespace NArchive;
//using namespace N7z;

#define PluginName "LzmaPlugin"
// Tou can find the list of all GUIDs in Guid.txt file.
// use another CLSIDs, if you want to support other formats (zip, rar, ...).
// {23170F69-40C1-278A-1000-000110070000}
DEFINE_GUID(CLSID_CFormat7z,
	0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x07, 0x00, 0x00);

#define CLSID_Format CLSID_CFormat7z

#define kDllName "7z.dll"
#define kDllNameA "7za.dll"


NDLL::CLibrary lib;
Func_CreateObject createObjectFunc = NULL;
std::vector<std::string> FilesToDelete;

#define ReportError(msg) agk::PluginError(PluginName ": " msg)
#define ReportErrorS(msg, parm)				\
	{										\
		std::string error;					\
		error.append(PluginName);			\
		error.append(": ");					\
		error.append(msg);					\
		error.append(": ");					\
		error.append(parm);					\
		agk::PluginError(error.c_str());	\
	}
#define ReportErrorN(msg, value)			\
	{										\
		char num[30];						\
		ZeroMemory(num, 30);				\
		itoa(value, num, 10);				\
		ReportErrorS(msg, num);				\
	}
#define ReportErrorH(msg, value)			\
	{										\
		char num[30];						\
		ZeroMemory(num, 30);				\
		itoa(value, num, 16);				\
		ReportErrorS(msg, num);				\
	}

#define Print(msg) agk::Log(PluginName ": " msg)
#define PrintS(msg, parm)				\
	{									\
		std::string text;				\
		text.append(PluginName);		\
		text.append(": ");				\
		text.append(msg);				\
		text.append(": ");				\
		text.append(parm);				\
		agk::Log(text.c_str());			\
	}
#define PrintN(msg, value)					\
	{										\
		char num[30];						\
		ZeroMemory(num, 30);				\
		itoa(value, num, 10);				\
		PrintS(msg, num);					\
	}

#define CheckHRESULT(result, error)	\
	if (result != S_OK)				\
		agk::PluginError(error)

static void GetAString(UString s, AString &a)
{
	UnicodeStringToMultiByte2(a, s, (UINT)CP_OEMCP);
}

static void GetAString(const wchar_t *s, AString &a)
{
	GetAString(UString(s), a);
}

static FString CmdStringToFString(const char *s)
{
	return us2fs(GetUnicodeString(s));
}

/*
Attempts to detect when the game is running in debug mode by checking the read path.
*/
bool RunningInDebugMode()
{
	char *path = agk::GetReadPath();
	for (char *p = path; *p != '\0'; p++)
	{
		*p = tolower(*p);
	}
	bool result = (strstr(path, "/tier 1/compiler/interpreters/") != 0);
	agk::DeleteString(path);
	return result;
}

// String replace
int replace(char *str, char ch1, char ch2)
{
	int changes = 0;
	while (*str != '\0')
	{
		if (*str == ch1)
		{
			*str = ch2;
			changes++;
		}
		str++;
	}
	return changes;
}

// Make all backslashes into slashes for quicker string comparison when finding the item index.
void NormalizeItemName(char *name)
{
	replace(name, '\\', '/');
}

/*
Converts a const char* to agk:string.
*/
char *CreateString(const char* text)
{
	if (text)
	{
		int length = (int)strlen(text) + 1;
		char *result = agk::CreateString(length);
		strcpy_s(result, length, text);
		return result;
	}
	char *result = agk::CreateString(1);
	strcpy_s(result, 1, "");
	return result;
}

/*
Converts a std::string to agk:string.
*/
char *CreateString(std::string text)
{
	return CreateString(text.c_str());
}

// Append to std::string and delete the string return from an AGK command.
void AppendAGKString(std::string &str, char *agkString)
{
	&str.append(agkString);
	agk::DeleteString(agkString);
}

/*
This structure contains all information for both archive readers and writers.
When writing, the writer will also have a reader when the file being written already exists.

The directory items for all archives are pre-cached on opening.
*/
struct ArchiveInfo
{
	CMyComPtr<IInArchive> reader;
	CMyComPtr<IOutArchive> writer;
	std::string password;
	std::string path; // full path, including file name.
	CObjectVector<CDirItem> dirItems;

	void Clear()
	{
		reader = NULL;
		writer = NULL;
		path.clear();
		password.clear();
		dirItems.Clear();
	}

	void LoadDirItems()
	{
		if (reader == NULL)
		{
			return;
		}
		// Load existing items.
		UInt32 numItems = 0;
		reader->GetNumberOfItems(&numItems);
		for (UInt32 index = 0; index < numItems; index++)
		{
			CDirItem di;
			di.OldIndex = index;
			di.Data = NULL;
			di.DataLength = 0;
			// Get name of file
			{
				NCOM::CPropVariant prop;
				CheckHRESULT(reader->GetProperty(index, kpidPath, &prop), "Error retrieving item name.");
				AString temp;
				GetAString(prop.bstrVal, temp);
				temp.Replace('\\', '/'); // Same as NormalizeItemName.
				//PrintS("Exists in archive", temp);
				di.Name = UString(temp); //UString(prop.bstrVal);
			}
			{
				// Get uncompressed size of file
				NCOM::CPropVariant prop;
				CheckHRESULT(reader->GetProperty(index, kpidSize, &prop), "Error retrieving item size.");
				di.Size = prop.uhVal.QuadPart;
			}
			{
				// Get whether item is a directory.
				NCOM::CPropVariant prop;
				CheckHRESULT(reader->GetProperty(index, kpidAttrib, &prop), "Error retrieving item attributes.");
				di.Attrib = prop.ulVal;
			}
			dirItems.Add(di);
		}
	}

	ArchiveInfo(CMyComPtr<IInArchive> r, const char *pw, std::string p) : 
		reader(r), 
		writer(NULL), 
		path(p), 
		password(pw)
	{
		LoadDirItems();
	}

	ArchiveInfo(CMyComPtr<IOutArchive> w, CMyComPtr<IInArchive> r, const char *pw, std::string p) :
		reader(r), 
		writer(w), 
		path(p), 
		password(pw)
	{
		LoadDirItems();
	}
};

// List of all archives.
std::vector<ArchiveInfo> archives;
// These modes are for use with GetArchiveInfo in order to know what type of archive is expected.
// ie: User can't ask to write to an archive that's only opened for read.
#define MODE_ANY	0
#define MODE_READ	1
#define MODE_WRITE	2

/*
Gets the archive info for the given archive ID.

The given mode is checked to see if the archive with that ID is opened in the expected mode.
*/
bool GetArchiveInfo(int archiveID, int mode, ArchiveInfo *&info)
{
	archiveID--; // Convert from 1-based to 0-based.
	if (archiveID >= 0 && archiveID < (int)archives.size())
	{
		info = &archives[archiveID];
		if (info->writer != NULL || info->reader != NULL)
		{
			switch (mode)
			{
			case MODE_ANY:
				break;
			case MODE_READ:
				if ((info->reader == NULL) || (info->writer != NULL))
				{
					info = NULL;
					ReportError("Archive is not opened for read.");
					return false;
				}
				break;
			case MODE_WRITE:
				if (info->writer == NULL)
				{
					info = NULL;
					ReportError("Archive is not opened for write.");
					return false;
				}
				break;
			default:
				info = NULL;
				ReportError("Requested unknown archive mode.");
				return false;
			}
			return true;
		}
	}
	info = NULL;
	ReportError("Invalid archive ID.");
	return false;
}

/*
Load the 7z or 7za DLL.
Find the CreateObject function from the library.
*/
bool Load7ZipLibrary()
{
	if (createObjectFunc)
	{
		// Already found the CreateObject function.
		return true;
	}
	Print("Searching for 7-zip library");
	if (lib.Load(NDLL::GetModuleDirPrefix() + FTEXT(kDllName)))
	{
		PrintS("Found full library", fs2fas(NDLL::GetModuleDirPrefix() + FTEXT(kDllName)));
	}
	else if (lib.Load(NDLL::GetModuleDirPrefix() + FTEXT(kDllNameA)))
	{
		PrintS("Found reduced library", fs2fas(NDLL::GetModuleDirPrefix() + FTEXT(kDllNameA)));
	}
	else
	{
		ReportError("Can not load 7-zip library.");
		return false;
	}
	createObjectFunc = (Func_CreateObject)lib.GetProc("CreateObject");
	if (!createObjectFunc)
	{
		ReportError("Can not get CreateObject function.");
		return false;
	}
	Print("7-zip library has been loaded.");
	return true;
}

/*
If given a raw: path, it is returned as is.
Otherwise an absolute path for the file within the write folder is returned.
If allowReadPath is true, the read path is returned instead if the write path does not exist.
*/
std::string GetArchiveFilePath(const char *archiveFileName, bool allowReadPath = true)
{
	std::string archivePath;
	if (strncmp(archiveFileName, "raw:", 4) == 0)
	{
		archivePath.append(archiveFileName + 4);
	}
	else
	{
		AppendAGKString(archivePath, agk::GetWritePath());
		AppendAGKString(archivePath, agk::GetFolder());
		archivePath.append(archiveFileName);
		if (allowReadPath)
		{
			std::string testPath;
			testPath.append("raw:");
			testPath.append(archivePath);
			if (!agk::GetFileExists(testPath.c_str()))
			{
				archivePath.clear();
				AppendAGKString(archivePath, agk::GetReadPath());
				AppendAGKString(archivePath, agk::GetFolder());
				archivePath.append(archiveFileName);
			}
		}
	}
	return archivePath;
}

/*
Creates and returns an IID_IInArchive archive interface.
*/
CMyComPtr<IInArchive> CreateArchiveReader()
{
	if (!Load7ZipLibrary())
	{
		return NULL;
	}
	//CHandler *archiveHandler = new CHandler;
	CMyComPtr<IInArchive> archive;// = archiveHandler;
	if (createObjectFunc(&CLSID_Format, &IID_IInArchive, reinterpret_cast<void**>(&archive)) != S_OK)
	{
		ReportError("CreateArchiveReader failed to get class object.");
		return NULL;
	}
	return archive;
}

/*
Creates and returns an IID_IOutArchive archive interface.
If an IInArchive interface is given, the IID_IOutArchive will be queried from it.
*/
CMyComPtr<IOutArchive> CreateArchiveWriter(CMyComPtr<IInArchive> reader)
{
	if (!Load7ZipLibrary())
	{
		return NULL;
	}
	CMyComPtr<IOutArchive> archive;
	if (reader != NULL)
	{
		if ((reader->QueryInterface(IID_IOutArchive, reinterpret_cast<void**>(&archive)) != S_OK) || (archive == NULL))
		{
			ReportError("CreateArchiveWriter could not query IID_IOutArchive interface.");
			return NULL;
		}
		return archive;
	}
	if (createObjectFunc(&CLSID_Format, &IID_IOutArchive, reinterpret_cast<void**>(&archive)) != S_OK)
	{
		ReportError("CreateArchiveWriter failed to get class object.");
		return NULL;
	}
	return archive;
}

/*
Opens an archive reader to the file at the given path with the given password.
*/
bool OpenArchiveReader(CMyComPtr<IInArchive> archive, const char *archivePath, const char *password)
{
	if (archive == NULL)
	{
		ReportError("Given NULL archive reader.");
		return false;
	}
	PrintS("OpenArchiveReader", archivePath);
	CInFileStream *fileSpec = new CInFileStream;
	CMyComPtr<IInStream> file = fileSpec;
	if (!fileSpec->Open(CmdStringToFString(archivePath)))
	{
		ReportErrorS("Can not open archive file", archivePath);
		return false;
	}
	// Set up the open callback.
	CArchiveOpenCallback *openCallbackSpec = new CArchiveOpenCallback;
	CMyComPtr<IArchiveOpenCallback> openCallback(openCallbackSpec);
	openCallbackSpec->PasswordIsDefined = (password != NULL);
	openCallbackSpec->Password = UString(password);
	const UInt64 scanSize = 1 << 23;
	if (archive->Open(file, &scanSize, openCallback) != S_OK)
	{
		ReportErrorS("Can not open file as archive", archivePath);
		return false;
	}
	return true;
}

/*
Find the index within the directory items list of the given item name.
*/
bool GetItemIndex(ArchiveInfo *info, const char *itemName, UInt32 &index)
{
	for (UInt32 _index = 0; _index < info->dirItems.Size(); _index++)
	{
		CDirItem &di = info->dirItems[_index];
		AString name;
		GetAString(di.Name, name);
		if (strcmp(itemName, name) == 0)
		{
			index = _index;
			return true;
		}
	}
	index = 0;
	return false;
}

/*
Opens an archive in read mode using the given password.  Password can be an empty string.

Returns the archive ID or 0 if the archive failed to open.
*/
extern "C" DLL_EXPORT int OpenToRead(const char *archiveFileName, const char *password)
{
	std::string archivePath = GetArchiveFilePath(archiveFileName);
	CMyComPtr<IInArchive> archive = CreateArchiveReader();
	if (!OpenArchiveReader(archive, archivePath.c_str(), password))
	{
		return 0;
	}
	ArchiveInfo info = ArchiveInfo(archive, password, archivePath);
	archives.push_back(info);
	return archives.size();
}

/*
Opens an archive in write mode using the given password.  Password can be an empty string.
Note that this writes to a temporary file while placing a lock on any existing file.

Returns the archive ID or 0 if the archive failed to open.
*/
extern "C" DLL_EXPORT int OpenToWrite(const char *archiveFileName, const char *password)
{
	std::string archivePath = GetArchiveFilePath(archiveFileName);
	// Open the archive for read if it exists.  If so, cache the item list.
	CMyComPtr<IInArchive> archiveReader;
	std::string testPath;
	testPath.append("raw:").append(archivePath);
	if (agk::GetFileExists(testPath.c_str()))
	{
		//Print("Appending to existing archive.");
		archiveReader = CreateArchiveReader();
		if (!OpenArchiveReader(archiveReader, archivePath.c_str(), password))
		{
			return 0;
		}
	}
	CMyComPtr<IOutArchive> outArchive;
	outArchive = CreateArchiveWriter(archiveReader);
	if (outArchive == NULL)
	{
		if (archiveReader != NULL)
		{
			archiveReader->Close();
		}
		return 0;
	}
	ArchiveInfo info = ArchiveInfo(outArchive, archiveReader, password, archivePath);
	archives.push_back(info);
	return archives.size();
}

/*
Check errno for file errors that should be reported.
*/
bool HadReportableFileError()
{
	switch (errno)
	{
	case ERROR_FILE_NOT_FOUND:
		// Ignore error.
		return false;
	default:
		return true;
	}
}

/*
Delete without raising an error.
Reports true or false.
*/
bool SafeDelete(const char *fileName)
{
	if ((fileName == NULL) || (strlen(fileName) == 0))
	{
		return false;
	}
	if ((remove(fileName) != 0) && HadReportableFileError())
	{
		return false;
	}
	return true;
}

/*
Rename without raising an error.
1) Delete any existing .bak file. (Based on new file name.)
2) Back up any existing file with the new file name.
3) Rename the old (temp) file to the new file name.
4) Delete the created backup.

Reports true or false.  Will report false after any failing step except the last.
*/
bool SafeRename(const char *oldFileName, const char *newFileName)
{
	std::string backup;
	// Start by renaming the existing newFileName.
	backup = newFileName;
	backup.append(".bak");
	if (!SafeDelete(backup.c_str()))
	{
		ReportErrorN("Failed to remove existing file backup. Error", errno);
		return false;
	}
	if ((rename(newFileName, backup.c_str()) != 0) && HadReportableFileError())
	{
		ReportErrorN("Failed to backup original file. Error", errno);
		return false;
	}
	if (rename(oldFileName, newFileName) != 0)
	{
		ReportErrorN("Failed to rename file. Error", errno);
		return false;
	}
	if (!SafeDelete(backup.c_str()))
	{
		// This is a warning since the rename did take place.
		ReportErrorN("Failed to remove file backup. Error", errno);
	}
	return true;
}

/*
This method attempts to discover the sync rate of the game whether set manually or with vsync enabled using GetFrameTime.
Frame rates between 30 and 120 FPS are considered acceptable.
The plugin tries to maintain the same sync rate, but can slow down.
*/
void CalculateSyncRate()
{
	_progressSyncRate = (int)(agk::GetFrameTime() * 1000.0f);
	// Allow FPS between 30 and 120 FPS.
	if (_progressSyncRate > 33)
	{
		_progressSyncRate = 33;
	}
	if (_progressSyncRate < 8)
	{
		_progressSyncRate = 8;
	}
}

/*
Closes an open archive of the given archive ID.
If the archive is in write mode, data will be compressed and the new file will be created at this time.
*/
extern "C" DLL_EXPORT void Close(int archiveID)
{
	ArchiveInfo *info;
	if (!GetArchiveInfo(archiveID, MODE_ANY, info))
	{
		return;
	}
	if (info->writer != NULL)
	{
		//CloseWriter(info);
		// Get the original fileName and extension.
		char fileName[_MAX_FNAME];
		char ext[_MAX_EXT];
		if (_splitpath_s(info->path.c_str(), NULL, 0, NULL, 0, fileName, _MAX_FNAME, ext, _MAX_EXT) != 0)
		{
			ReportError("Could not split file path.");
		}
		else
		{
			// Create the temp file path.
			std::string tempPath;
			AppendAGKString(tempPath, agk::GetWritePath());
			AppendAGKString(tempPath, agk::GetFolder()); 
			tempPath.append(fileName).append(ext).append(".tmp");
			// Create the archive.
			COutFileStream *outFileStreamSpec = new COutFileStream;
			CMyComPtr<IOutStream> outFileStream = outFileStreamSpec;
			FString archiveNameTemp = CmdStringToFString(tempPath.c_str());
			if (!outFileStreamSpec->Open(archiveNameTemp, CREATE_ALWAYS))
			{
				ReportError("Could not create temporary archive file.");
			}
			else
			{
				// Perform the update.
				//agk::Log("UpdateItems");
				CalculateSyncRate();
				CArchiveUpdateCallback *updateCallbackSpec = new CArchiveUpdateCallback;
				CMyComPtr<IArchiveUpdateCallback> updateCallback(updateCallbackSpec);
				updateCallbackSpec->PasswordIsDefined = (info->password.size() > 0);
				updateCallbackSpec->Password = UString(info->password.c_str());
				updateCallbackSpec->Init(&info->dirItems);
				HRESULT result = info->writer->UpdateItems(outFileStream, info->dirItems.Size(), updateCallback);
				// Clean up.  Release file locks so that the temp file can be renamed. etc
				updateCallbackSpec->Finilize();
				outFileStreamSpec->Close();
				outFileStreamSpec = NULL;
				info->writer->Release();
				if (info->reader != NULL)
				{
					info->reader->Close();
				}
				// Clear item data.
				for (UInt32 index = 0; index < info->dirItems.Size(); index++)
				{
					CDirItem &dirItem = info->dirItems[index];
					if (dirItem.Data != NULL)
					{
						delete[] dirItem.Data;
						dirItem.Data = NULL;
					}
					dirItem.DataLength = 0;
				}
				// Check update results.
				switch (result)
				{
				case S_OK:
					// If using a raw: path, the raw path is the destination. Otherwise, the write folder is the destination.
					{
						std::string destPath;
						if (info->path.compare(0, 4, "raw:") == 0)
						{
							destPath.append(info->path.substr(4));
						}
						else
						{
							AppendAGKString(destPath, agk::GetWritePath());
							AppendAGKString(destPath, agk::GetFolder());
							destPath.append(fileName).append(ext);
						}
						// Copy the temp file.
						SafeRename(tempPath.c_str(), destPath.c_str());
					}
					break;
				case E_ABORT:
					SafeDelete(tempPath.c_str());
					ReportError("Update aborted");
					break;
				default:
					SafeDelete(tempPath.c_str());
					ReportErrorH("Update error. HRESULT", result);
					break;
				}
				info->Clear();
				return;
			}
		}
		info->writer->Release();
	}
	if (info->reader != NULL)
	{
		info->reader->Close();
	}
	info->Clear();
}

extern "C" DLL_EXPORT int MoveFileToReadPath(const char *fileName)
{
	if (RunningInDebugMode())
	{
		agk::Message("This command does not work while running in debug mode.");
		return 0;
	}
	std::string writePath = fileName;
	std::string readPath;
	AppendAGKString(readPath, agk::GetReadPath());
	AppendAGKString(readPath, agk::GetFolder());
	// Add support for the raw prefix.
	if (writePath.compare(0, 4, "raw:") == 0)
	{
		writePath = writePath.substr(4);
		char fname[_MAX_FNAME];
		char ext[_MAX_EXT];
		if (_splitpath_s(writePath.c_str(), NULL, 0, NULL, 0, fname, _MAX_FNAME, ext, _MAX_EXT) != 0)
		{
			ReportError("Could not split file path.");
			return false;
		}
		readPath.append(fname);
		readPath.append(ext);
	}
	else
	{
		writePath.clear();
		AppendAGKString(writePath, agk::GetWritePath());
		AppendAGKString(writePath, agk::GetFolder());
		writePath.append(fileName);
		readPath.append(fileName);
	}
	return SafeRename(writePath.c_str(), readPath.c_str());
}

/*
Returns the file path of the given archive ID.

When in write mode, this path will point to the read folder if the archive is being appended and the file was found there.
Otherwise, this will be the write path or the raw path, if one is given.
*/
extern "C" DLL_EXPORT char *GetFilePath(int archiveID)
{
	ArchiveInfo *info;
	if (!GetArchiveInfo(archiveID, MODE_ANY, info))
	{
		return CreateString("");
	}
	return CreateString(info->path);
}

/*
Sets the progress tween chain.
A progress tween chain must have a duration of 100 and a delay of 0 to work properly.
The tween chain is updated while compressing and extracting.
*/
extern "C" DLL_EXPORT void SetProgressTweenChain(int tweenChainID)
{
	_progressTweenChainID = tweenChainID;
}

/*
Clears the list of tween chains that the plugin should update while compressing and extracting.
*/
extern "C" DLL_EXPORT void ClearTweenChains()
{
	_tweenChainIDs.clear();
}

/*
Adds a tween chain to the list of tween chains that the plugin should update while compressing and extracting.
*/
extern "C" DLL_EXPORT void AddTweenChain(int tweenChainID)
{
	_tweenChainIDs.push_back(tweenChainID);
}

/*
Clears the list of tween chains that the plugin should update and restart if they finish while compressing and extracting.
*/
extern "C" DLL_EXPORT void ClearRepeatingTweenChains()
{
	_repeatingTweenChainIDs.clear();
}

/*
Adds a tween chain to the list of tween chains that the plugin should update and restart if they finish while compressing and extracting.
*/
extern "C" DLL_EXPORT void AddRepeatingTweenChain(int tweenChainID)
{
	_repeatingTweenChainIDs.push_back(tweenChainID);
}

/*
Returns the directory item list of the archive as a JSON string.
[
	{
		"Name": "Item name",
		"Size": 1024,
		"IsDir": 0
	}
]
*/
extern "C" DLL_EXPORT char *GetItemListJSON(int archiveID)
{
	std::ostringstream json;
	json << "[";
	// Get the archive info.
	ArchiveInfo *info;
	if (GetArchiveInfo(archiveID, MODE_ANY, info))
	{
		const char* separator = "";
		for (UInt32 index = 0; index < info->dirItems.Size(); index++)
		{
			CDirItem &di = info->dirItems[index];
			json << separator << "\n";
			separator = ",";
			json << "\t{\n";
			AString name;
			GetAString(di.Name, name);
			json << "\t\t\"Name\": \"" << name << "\"";
			json << ",\n";
			json << "\t\t\"Size\": " << di.Size;
			json << ",\n";
			json << "\t\t\"IsDir\": " << (di.isDir() ? 1 : 0);
			json << "\n\t}";
		}
	}
	json << "\n]";
	return CreateString(json.str());
}

/*
Checks to see if the archive has an item with the given name.
*/
extern "C" DLL_EXPORT int HasItem(int archiveID, char *itemName)
{
	ArchiveInfo *info;
	if (!GetArchiveInfo(archiveID, MODE_ANY, info))
	{
		return 0;
	}
	// Find item index
	UInt32 index;
	NormalizeItemName(itemName);
	if (!GetItemIndex(info, itemName, index))
	{
		return 0;
	}
	return 1;
}

/*
Get an item from the archive as a memblock.

Returns an memblock ID or 0.
*/
extern "C" DLL_EXPORT int GetItemAsMemblock(int archiveID, char *itemName)
{
	ArchiveInfo *info;
	if (!GetArchiveInfo(archiveID, MODE_READ, info))
	{
		return 0;
	}
	// Find item index
	UInt32 index;
	NormalizeItemName(itemName);
	if (!GetItemIndex(info, itemName, index))
	{
		ReportErrorS("Could not find item index", itemName);
		return 0;
	}
	CalculateSyncRate();
	// Create extract callback.
	CArchiveExtractCallback *extractCallbackSpec = new CArchiveExtractCallback;
	CMyComPtr<IArchiveExtractCallback> extractCallback(extractCallbackSpec);
	extractCallbackSpec->Init(info->reader, FString()); // second parameter is output folder path, which isn't used.
	extractCallbackSpec->PasswordIsDefined = (info->password.size() > 0);
	extractCallbackSpec->Password = UString(info->password.c_str());
	UInt32 indices[1];
	indices[0] = index;
	HRESULT result = info->reader->Extract(indices, 1, false, extractCallback);
	if (result != S_OK)
	{
		if (extractCallbackSpec->MemblockID)
		{
			agk::DeleteMemblock(extractCallbackSpec->MemblockID);
		}
		ReportErrorH("Extract Error", result);
		return 0;
	}
	return extractCallbackSpec->MemblockID;
}

/*
Extracts an item from the archive to the given file name.
Set deleteOnExit to 1 to tell the plugin to attempt to delete the extracted file on closing.
The "raw:" prefix is supported.

Returns a boolean indicating success.
*/
extern "C" DLL_EXPORT int ExtractItemToFile(int archiveID, char *itemName, char *fileName, int deleteOnExit)
{
	int memblock = GetItemAsMemblock(archiveID, itemName);
	if (memblock == 0)
	{
		return 0;
	}
	agk::GetErrorOccurred(); // Clear any existing error.
	agk::CreateFileFromMemblock(fileName, memblock);
	int hadError = agk::GetErrorOccurred();
	agk::DeleteMemblock(memblock);
	// Delete on exit.
	if (deleteOnExit)
	{
		std::string extractPath = GetArchiveFilePath(fileName, false);
		if (extractPath.compare(0, 4, "raw:") == 0)
		{
			extractPath = extractPath.substr(4);
		}
		FilesToDelete.push_back(extractPath);
	}
	return hadError ? 0 : 1; // Convert "hadError" to a success-indicating bool int.
}

/*
Get an item from the archive as an image.

Returns an image ID or 0.
*/
extern "C" DLL_EXPORT int GetItemAsImage(int archiveID, char *itemName)
{
	int memblock = GetItemAsMemblock(archiveID, itemName);
	if (memblock == 0)
	{
		return 0;
	}
	unsigned int image = agk::CreateImageFromMemblock(memblock);
	agk::DeleteMemblock(memblock);
	return image;
}

/*
Get an item from the archive as an object created from a mesh.

Returns an object ID or 0.
*/
extern "C" DLL_EXPORT int GetItemAsMeshObject(int archiveID, char *itemName)
{
	int memblock = GetItemAsMemblock(archiveID, itemName);
	if (memblock == 0)
	{
		return 0;
	}
	unsigned int object = agk::CreateObjectFromMeshMemblock(memblock);
	agk::DeleteMemblock(memblock);
	return object;
}

/*
Get an item from the archive as a sound.

Returns a sound ID or 0.
*/
extern "C" DLL_EXPORT int GetItemAsSound(int archiveID, char *itemName)
{
	int memblock = GetItemAsMemblock(archiveID, itemName);
	if (memblock == 0)
	{
		return 0;
	}
	unsigned int sound = agk::CreateSoundFromMemblock(memblock);
	agk::DeleteMemblock(memblock);
	return sound;
}

/*
Get an item from the archive as a string.

Returns the extracted string.
*/
extern "C" DLL_EXPORT char *GetItemAsString(int archiveID, char *itemName)
{
	int memblock = GetItemAsMemblock(archiveID, itemName);
	if (memblock == 0)
	{
		return CreateString("");
	}
	char * result = agk::GetMemblockString(memblock, 0, agk::GetMemblockSize(memblock));
	agk::DeleteMemblock(memblock);
	return result;
}

//std::list<std::string> GetParentFolderList(char *path)
//{
//	std::list<std::string> output;
//	std::string s = path;
//	std::string::size_type pos = 0;
//	while ((pos = s.find('/', pos)) != std::string::npos)
//	{
//		std::string substring(s.substr(0, pos));
//		output.push_back(substring);
//		pos++;
//	}
//	//output.push_back(path);
//	return output;
//}

/*
Removes all items from the archive.

Returns a boolean indicating success.
*/
extern "C" DLL_EXPORT int Clear(int archiveID, char *itemName)
{
	ArchiveInfo *info;
	if (!GetArchiveInfo(archiveID, MODE_WRITE, info))
	{
		return false;
	}
	for (UInt32 index = 0; index < info->dirItems.Size(); index++)
	{
		CDirItem &di = info->dirItems[index];
		delete[] di.Data;
	}
	info->dirItems.ClearAndFree();
	return true;
}

/*
Deletes an item from the archive.

Returns a boolean indicating success.
*/
extern "C" DLL_EXPORT int DeleteItem(int archiveID, char *itemName)
{
	ArchiveInfo *info;
	if (!GetArchiveInfo(archiveID, MODE_WRITE, info))
	{
		return false;
	}
	NormalizeItemName(itemName);
	UInt32 index;
	if (GetItemIndex(info, itemName, index))
	{
		CDirItem &di = info->dirItems[index];
		// Clear any existing data array.
		delete[] di.Data;
		info->dirItems.Delete(index);
		return true;
	}
	return false;
}

/*
Sets an item from a memblock.

Returns a boolean indicating success.
*/
extern "C" DLL_EXPORT int SetItemFromMemblock(int archiveID, char *itemName, const unsigned int memblockID)
{
	if (memblockID == 0 || !agk::GetMemblockExists(memblockID))
	{
		return false;
	}
	ArchiveInfo *info;
	if (!GetArchiveInfo(archiveID, MODE_WRITE, info))
	{
		return false;
	}
	NormalizeItemName(itemName);
	// Use the system time for the item datetimes.
	FILETIME filetime;
	GetSystemTimeAsFileTime(&filetime);
	// Add the item itself.
	{
		CDirItem di;
		di.Attrib = FILE_ATTRIBUTE_ARCHIVE;
		di.Size = agk::GetMemblockSize(memblockID);
		di.CTime = filetime;
		di.ATime = filetime;
		di.MTime = filetime;
		di.Name = UString(itemName);
		di.DataLength = agk::GetMemblockSize(memblockID);
		di.Data = new byte[di.DataLength];
		CopyMemory(di.Data, agk::GetMemblockPtr(memblockID), di.DataLength);
		UInt32 index;
		if (GetItemIndex(info, itemName, index))
		{
			//PrintS("Replacing", itemName);
			CDirItem &old = info->dirItems[index];
			// Clear any existing data array.
			delete[] old.Data;
			// Delete the old and insert the new.  Could just change old...
			di.OldIndex = old.OldIndex;
			info->dirItems.Delete(index);
			info->dirItems.Insert(index, di);
		}
		else
		{
			//PrintS("Adding", itemName);
			di.OldIndex = MAXUINT32;
			info->dirItems.Add(di);
		}
	}
	return true;
}

/*
Sets an item from a file.

Returns a boolean indicating success.
*/
extern "C" DLL_EXPORT int SetItemFromFile(int archiveID, char *itemName, const char *fileName)
{
	unsigned int memblock = agk::CreateMemblockFromFile(fileName);
	if (memblock == 0)
	{
		return 0;
	}
	int result = SetItemFromMemblock(archiveID, itemName, memblock);
	agk::DeleteMemblock(memblock);
	return result;
}

/*
Sets an item from an image.

Returns a boolean indicating success.
*/
extern "C" DLL_EXPORT int SetItemFromImage(int archiveID, char *itemName, const unsigned int imageID)
{
	unsigned int memblock = agk::CreateMemblockFromImage(imageID);
	if (memblock == 0)
	{
		return 0;
	}
	int result = SetItemFromMemblock(archiveID, itemName, memblock);
	agk::DeleteMemblock(memblock);
	return result;
}

/*
Sets an item from an image file.

Returns a boolean indicating success.
*/
extern "C" DLL_EXPORT int SetItemFromImageFile(int archiveID, char *itemName, const char *fileName)
{
	unsigned int imageID = agk::LoadImage(fileName);
	if (imageID == 0)
	{
		return 0;
	}
	int result = SetItemFromImage(archiveID, itemName, imageID);
	agk::DeleteImage(imageID);
	return result;
}

/*
Sets an item from an object mesh.

Returns a boolean indicating success.
*/
extern "C" DLL_EXPORT int SetItemFromObjectMesh(int archiveID, char *itemName, const unsigned int objectID, const unsigned int meshIndex)
{
	unsigned int memblock = agk::CreateMemblockFromObjectMesh(objectID, meshIndex);
	if (memblock == 0)
	{
		return 0;
	}
	int result = SetItemFromMemblock(archiveID, itemName, memblock);
	agk::DeleteMemblock(memblock);
	return result;
}

/*
Sets an item from a sound.

Returns a boolean indicating success.
*/
extern "C" DLL_EXPORT int SetItemFromSound(int archiveID, char *itemName, const unsigned int soundID)
{
	unsigned int memblock = agk::CreateMemblockFromSound(soundID);
	if (memblock == 0)
	{
		return 0;
	}
	int result = SetItemFromMemblock(archiveID, itemName, memblock);
	agk::DeleteMemblock(memblock);
	return result;
}

/*
Sets an item from a sound file.

Returns a boolean indicating success.
*/
extern "C" DLL_EXPORT int SetItemFromSoundFile(int archiveID, char *itemName, const char *fileName)
{
	unsigned int soundID = agk::LoadSound(fileName);
	if (soundID == 0)
	{
		return 0;
	}
	int result = SetItemFromSound(archiveID, itemName, soundID);
	agk::DeleteSound(soundID);
	return result;
}

/*
Sets an item from a string.

Returns a boolean indicating success.
*/
extern "C" DLL_EXPORT int SetItemFromString(int archiveID, char *itemName, const char *text)
{
	// Note: This method does NOT write the null terminator.
	// GetMemblockString will add it again when retrieving even if it's not there.
	unsigned int memblock = agk::CreateMemblock(strlen(text));
	if (memblock == 0)
	{
		return 0;
	}
	//agk::SetMemblockString(memblock, 0, text);
	// Use CopyMemory instead of SetMemblockString so the null terminator can be avoided.
	CopyMemory(agk::GetMemblockPtr(memblock), text, strlen(text));
	int result = SetItemFromMemblock(archiveID, itemName, memblock);
	agk::DeleteMemblock(memblock);
	return result;
}
