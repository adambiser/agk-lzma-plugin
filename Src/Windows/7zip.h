#ifndef _H_7ZIP
#define _H_7ZIP

#include "StdAfx.h"

//#include <stdio.h>
#include <atlbase.h>

//#include "../LZMA SDK/CPP/Common/MyWindows.h"
//
//#include "../LZMA SDK/CPP/Common/Defs.h"
#include "../LZMA SDK/CPP/Common/MyInitGuid.h"
//
#include "../LZMA SDK/CPP/Common/IntToString.h"
#include "../LZMA SDK/CPP/Common/StringConvert.h"
//
#include "../LZMA SDK/CPP/Windows/DLL.h"
#include "../LZMA SDK/CPP/Windows/FileDir.h"
//#include "../LZMA SDK/CPP/Windows/FileFind.h"
//#include "../LZMA SDK/CPP/Windows/FileName.h"
#include "../LZMA SDK/CPP/Windows/PropVariant.h"
#include "../LZMA SDK/CPP/Windows/PropVariantConv.h"

#include "../LZMA SDK/CPP/7zip/Common/FileStreams.h"
#include "../LZMA SDK/CPP/7zip/Common/StreamObjects.h"

#include "../LZMA SDK/CPP/7zip/Archive/IArchive.h"

#include "../LZMA SDK/CPP/7zip/IPassword.h"

#include <vector>

// For syncing.
extern unsigned int _progressTweenChainID;
extern std::vector<unsigned int> _tweenChainIDs;
extern std::vector<unsigned int> _repeatingTweenChainIDs;
extern int _progressSyncRate;

//////////////////////////////////////////////////////////////
// Archive Open callback class

class CArchiveOpenCallback :
	public IArchiveOpenCallback,
	public ICryptoGetTextPassword,
	public CMyUnknownImp
{
public:
	MY_UNKNOWN_IMP1(ICryptoGetTextPassword)

	STDMETHOD(SetTotal)(const UInt64 *files, const UInt64 *bytes);
	STDMETHOD(SetCompleted)(const UInt64 *files, const UInt64 *bytes);

	STDMETHOD(CryptoGetTextPassword)(BSTR *password);

	bool PasswordIsDefined;
	UString Password;

	CArchiveOpenCallback() : PasswordIsDefined(false) {}
};

//////////////////////////////////////////////////////////////
// Extraction Stream that calls Sync.

class CMyBufPtrSeqOutStream :
	public ISequentialOutStream,
	public CMyUnknownImp
{
	Byte *_buffer;
	size_t _size;
	size_t _pos;
public:
	void Init(Byte *buffer, size_t size)
	{
		_buffer = buffer;
		_pos = 0;
		_size = size;
	}
	size_t GetPos() const { return _pos; }

	MY_UNKNOWN_IMP1(ISequentialOutStream)
	STDMETHOD(Write)(const void *data, UInt32 size, UInt32 *processedSize);
};

//////////////////////////////////////////////////////////////
// Archive Extracting callback class

class CArchiveExtractCallback :
	public IArchiveExtractCallback,
	public ICryptoGetTextPassword,
	public CMyUnknownImp
{
	//UInt64 TotalSize = 0;
public:
	MY_UNKNOWN_IMP1(ICryptoGetTextPassword)

	// IProgress
	STDMETHOD(SetTotal)(UInt64 size);
	STDMETHOD(SetCompleted)(const UInt64 *completeValue);

	// IArchiveExtractCallback
	STDMETHOD(GetStream)(UInt32 index, ISequentialOutStream **outStream, Int32 askExtractMode);
	STDMETHOD(PrepareOperation)(Int32 askExtractMode);
	STDMETHOD(SetOperationResult)(Int32 resultEOperationResult);

	// ICryptoGetTextPassword
	STDMETHOD(CryptoGetTextPassword)(BSTR *aPassword);

private:
	CMyComPtr<IInArchive> _archiveHandler;
	//FString _directoryPath;  // Output directory
	UString _filePath;       // name inside arcvhive
	//FString _diskFilePath;   // full path to file on disk
	bool _extractMode;
	//struct CProcessedFileInfo
	//{
	//	FILETIME MTime;
	//	UInt32 Attrib;
	//	bool isDir;
	//	bool AttribDefined;
	//	bool MTimeDefined;
	//} _processedFileInfo;

	CMyBufPtrSeqOutStream *_outStreamSpec;
	CMyComPtr<ISequentialOutStream> _outStream;
	byte *_data = NULL;			// Need to initialize this.
	unsigned int _dataSize = 0;	// Need to initialize this.

public:
	void Init(IInArchive *archiveHandler, const FString &directoryPath);

	unsigned int MemblockID;
	UInt64 NumErrors;
	bool PasswordIsDefined;
	UString Password;

	CArchiveExtractCallback() : PasswordIsDefined(false) {}
};

//////////////////////////////////////////////////////////////
// Archive Creating callback class

struct CDirItem
{
	UInt64 Size;
	FILETIME CTime;
	FILETIME ATime;
	FILETIME MTime;
	UString Name;
	//FString FullPath;
	//unsigned int MemblockID;
	byte *Data;
	long DataLength;
	UInt32 OldIndex;
	UInt32 Attrib;

	bool isDir() const { return (Attrib & FILE_ATTRIBUTE_DIRECTORY) != 0; }
};

class CArchiveUpdateCallback :
	//public IArchiveUpdateCallback,
	public ICryptoGetTextPassword,
	public IArchiveUpdateCallback2,
	public ICryptoGetTextPassword2,
	public CMyUnknownImp
{
	UInt64 TotalSize = 0;
public:
	MY_UNKNOWN_IMP3(IArchiveUpdateCallback2, ICryptoGetTextPassword2, ICryptoGetTextPassword) // Added last

	// IProgress
	STDMETHOD(SetTotal)(UInt64 size);
	STDMETHOD(SetCompleted)(const UInt64 *completeValue);

	// IUpdateCallback2
	STDMETHOD(GetUpdateItemInfo)(UInt32 index,
		Int32 *newData, Int32 *newProperties, UInt32 *indexInArchive);
	STDMETHOD(GetProperty)(UInt32 index, PROPID propID, PROPVARIANT *value);
	STDMETHOD(GetStream)(UInt32 index, ISequentialInStream **inStream);
	STDMETHOD(SetOperationResult)(Int32 operationResult);
	STDMETHOD(GetVolumeSize)(UInt32 index, UInt64 *size);
	STDMETHOD(GetVolumeStream)(UInt32 index, ISequentialOutStream **volumeStream);
	STDMETHOD(CryptoGetTextPassword)(BSTR *password);
	STDMETHOD(CryptoGetTextPassword2)(Int32 *passwordIsDefined, BSTR *password);

public:
	CRecordVector<UInt64> VolumesSizes;
	UString VolName;
	UString VolExt;

	FString DirPrefix;
	const CObjectVector<CDirItem> *DirItems;

	bool PasswordIsDefined;
	UString Password;
	bool AskPassword;

	bool m_NeedBeClosed;

	//FStringVector FailedFiles;
	//CRecordVector<HRESULT> FailedCodes;

	CArchiveUpdateCallback() : PasswordIsDefined(false), AskPassword(false), DirItems(0) {};

	~CArchiveUpdateCallback() { Finilize(); }
	HRESULT Finilize();

	void Init(const CObjectVector<CDirItem> *dirItems)
	{
		DirItems = dirItems;
		m_NeedBeClosed = false;
		//FailedFiles.Clear();
		//FailedCodes.Clear();
	}
};

#endif // _H_7ZIP
