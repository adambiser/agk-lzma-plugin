#include "7zip.h"
#include "../AGKLibraryCommands.h"
#include <string>
#include <vector>

#ifdef _WIN32
HINSTANCE g_hInstance = 0;
#endif

//#define VERBOSE

using namespace NWindows;
using namespace NFile;
using namespace NDir;

// For syncing.
#include <chrono>
unsigned int _progressTweenChainID = 0;
std::vector<unsigned int> _tweenChainIDs;
std::vector<unsigned int> _repeatingTweenChainIDs;
int _progressSyncRate = 0;
auto _progressLastSyncTime = std::chrono::high_resolution_clock::now();

static HRESULT IsArchiveItemProp(IInArchive *archive, UInt32 index, PROPID propID, bool &result)
{
	NCOM::CPropVariant prop;
	RINOK(archive->GetProperty(index, propID, &prop));
	if (prop.vt == VT_BOOL)
	{
		result = VARIANT_BOOLToBool(prop.boolVal);
	}
	else if (prop.vt == VT_EMPTY)
	{
		result = false;
	}
	else
	{
		return E_FAIL;
	}
	return S_OK;
}

static HRESULT IsArchiveItemFolder(IInArchive *archive, UInt32 index, bool &result)
{
	return IsArchiveItemProp(archive, index, kpidIsDir, result);
}


static const wchar_t * const kEmptyFileAlias = L"[Content]";


//////////////////////////////////////////////////////////////
// Archive Open callback class

STDMETHODIMP CArchiveOpenCallback::SetTotal(const UInt64 * /* files */, const UInt64 * /* bytes */)
{
	return S_OK;
}

STDMETHODIMP CArchiveOpenCallback::SetCompleted(const UInt64 * /* files */, const UInt64 * /* bytes */)
{
	return S_OK;
}

STDMETHODIMP CArchiveOpenCallback::CryptoGetTextPassword(BSTR *password)
{
	if (!PasswordIsDefined)
	{
		// You can ask real password here from user
		// Password = GetPassword(OutStream);
		// PasswordIsDefined = true;
		agk::PluginError("Open: Password is not defined");
		return E_ABORT;
	}
	return StringToBstr(Password, password);
}

static const char * const kIncorrectCommand = "incorrect command";

inline void UpdateProgressSprite(UInt32 pos, UInt32 size)
{
	// Syncing is disabled.
	if (_progressSyncRate == 0)
	{
		return;
	}
	// Avoid divide by 0.
	if (size == 0)
	{
		return;
	}
#ifdef VERBOSE
	std::string status = "Progress: ";
	char num[30];
	ZeroMemory(num, 30);
	itoa(pos, num, 10);
	status.append(num);
	status.append(" of ");
	ZeroMemory(num, 30);
	itoa(size, num, 10);
	status.append(num);
	agk::Log(status.c_str());
#endif
	auto current = std::chrono::high_resolution_clock::now();
	auto dur = current - _progressLastSyncTime;
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
	//char *num = agk::Str((int)ms);
	//agk::Log(num);
	//agk::DeleteString(num);
	// Always do a sync when done.
	if ((pos < size) && (ms < _progressSyncRate))
	{
		return;
	}
	//agk::Log("sync");
	_progressLastSyncTime = current;
	if (_progressTweenChainID)
	{
		float percent = 100.0f * pos / size;
		// TweenChain needs to be playing in order to use SetTweenChainTime.
		if (!agk::GetTweenChainPlaying(_progressTweenChainID))
		{
			agk::PlayTweenChain(_progressTweenChainID);
		}
		agk::SetTweenChainTime(_progressTweenChainID, percent);
	}
	// Update non-repeating tween chains
	for (unsigned int index = 0; index < _tweenChainIDs.size(); index++)
	{
		unsigned int tweenChainID = _tweenChainIDs.at(index);
		if (agk::GetTweenChainPlaying(tweenChainID))
		{
			agk::UpdateTweenChain(tweenChainID, agk::GetFrameTime());
		}
	}
	// Update repeating tween chains
	for (unsigned int index = 0; index < _repeatingTweenChainIDs.size(); index++)
	{
		unsigned int tweenChainID = _repeatingTweenChainIDs.at(index);
		if (agk::GetTweenChainPlaying(tweenChainID))
		{
			agk::UpdateTweenChain(tweenChainID, agk::GetFrameTime());
		}
		else
		{
			agk::PlayTweenChain(tweenChainID);
		}
	}
	//agk::PrintC(agk::ScreenFPS());
	//agk::PrintC(", ");
	//agk::PrintC(agk::GetFrameTime());
	//agk::PrintC(", ");
	//agk::Print(_progressSyncRate);
	agk::Sync();
}

//////////////////////////////////////////////////////////////
// Archive Extracting callback class

static const char * const kTestingString = "Testing     ";
static const char * const kExtractingString = "Extracting  ";
static const char * const kSkippingString = "Skipping    ";

static const char * const kUnsupportedMethod = "Unsupported Method";
static const char * const kCRCFailed = "CRC Failed";
static const char * const kDataError = "Data Error";
static const char * const kUnavailableData = "Unavailable data";
static const char * const kUnexpectedEnd = "Unexpected end of data";
static const char * const kDataAfterEnd = "There are some data after the end of the payload data";
static const char * const kIsNotArc = "Is not archive";
static const char * const kHeadersError = "Headers Error";


void CArchiveExtractCallback::Init(IInArchive *archiveHandler, const FString &directoryPath)
{
	NumErrors = 0;
	_archiveHandler = archiveHandler;
	// Ignore directoryPath.
	//_directoryPath = directoryPath;
	//NName::NormalizeDirPathPrefix(_directoryPath);
}

STDMETHODIMP CArchiveExtractCallback::SetTotal(UInt64 /*size*/)
{
	//TotalSize = size;
	return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::SetCompleted(const UInt64 * /*completeValue*/)
{
	// Don't to the progress bar update here.  Doesn't behave as expected.  Use CMyBufPtrSeqOutStream::Write.
	return S_OK;
}

STDMETHODIMP CMyBufPtrSeqOutStream::Write(const void *data, UInt32 size, UInt32 *processedSize)
{
	size_t rem = _size - _pos;
	if (rem > size)
		rem = (size_t)size;
	if (rem != 0)
	{
		memcpy(_buffer + _pos, data, rem);
		_pos += rem;
	}
	::UpdateProgressSprite(_pos, _size);
	if (processedSize)
		*processedSize = (UInt32)rem;
	return (rem != 0 || size == 0) ? S_OK : E_FAIL;
}

STDMETHODIMP CArchiveExtractCallback::GetStream(UInt32 index,
	ISequentialOutStream **outStream, Int32 askExtractMode)
{
	*outStream = 0;
	_outStream.Release();
	_dataSize = 0;
	MemblockID = 0;

#ifdef VERBOSE
	std::string status = "GetStream: ";
	char num[30];
	ZeroMemory(num, 30);
	itoa(index, num, 10);
	status.append(num);
	status.append(", mode: ");
	ZeroMemory(num, 30);
	itoa(askExtractMode, num, 10);
	status.append(num);
	agk::Log(status.c_str());
#endif

	// Do nothing unless actually extracting data.
	if (askExtractMode != NArchive::NExtract::NAskMode::kExtract)
	{
		return S_OK;
	}
	bool isDir;
	RINOK(IsArchiveItemFolder(_archiveHandler, index, isDir));
	if (isDir)
	{
		return E_FAIL;
	}
	else
	{
		//_progressLastSyncTime = std::chrono::high_resolution_clock::now();
		// Get Size
		NCOM::CPropVariant prop;
		RINOK(_archiveHandler->GetProperty(index, kpidSize, &prop));
		UInt64 newFileSize;
		ConvertPropVariantToUInt64(prop, newFileSize);
		// Prepare buffer.
		_dataSize = (unsigned int)newFileSize;
		_data = new byte[_dataSize];
		ZeroMemory(_data, _dataSize);
		// Prepare stream.
		_outStreamSpec = new CMyBufPtrSeqOutStream;
		CMyComPtr<ISequentialOutStream> outStreamLoc(_outStreamSpec);
		_outStreamSpec->Init(_data, _dataSize);
		_outStream = outStreamLoc;
		*outStream = outStreamLoc.Detach();
	}
	return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::PrepareOperation(Int32 askExtractMode)
{
	_extractMode = false;
	switch (askExtractMode)
	{
	case NArchive::NExtract::NAskMode::kExtract:  _extractMode = true; break;
	};
	//switch (askExtractMode)
	//{
	//case NArchive::NExtract::NAskMode::kExtract:  agk::Log(kExtractingString); break;
	//case NArchive::NExtract::NAskMode::kTest:  agk::Log(kTestingString); break;
	//case NArchive::NExtract::NAskMode::kSkip:  agk::Log(kSkippingString); break;
	//};
	//agk::Log(_filePath);
	return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::SetOperationResult(Int32 operationResult)
{
	switch (operationResult)
	{
	case NArchive::NExtract::NOperationResult::kOK:
		break;
	default:
	{
		NumErrors++;
		//Print("  :  ");
		const char *s = NULL;
		switch (operationResult)
		{
		case NArchive::NExtract::NOperationResult::kUnsupportedMethod:
			s = kUnsupportedMethod;
			break;
		case NArchive::NExtract::NOperationResult::kCRCError:
			s = kCRCFailed;
			break;
		case NArchive::NExtract::NOperationResult::kDataError:
			s = kDataError;
			break;
		case NArchive::NExtract::NOperationResult::kUnavailable:
			s = kUnavailableData;
			break;
		case NArchive::NExtract::NOperationResult::kUnexpectedEnd:
			s = kUnexpectedEnd;
			break;
		case NArchive::NExtract::NOperationResult::kDataAfterEnd:
			s = kDataAfterEnd;
			break;
		case NArchive::NExtract::NOperationResult::kIsNotArc:
			s = kIsNotArc;
			break;
		case NArchive::NExtract::NOperationResult::kHeadersError:
			s = kHeadersError;
			break;
		}
		std::string error = "Extract Error";
		if (s)
		{
			error.append(": ");
			error.append(s);
		}
		else
		{
			char temp[16];
			ConvertUInt32ToString(operationResult, temp);
			error.append(" #");
			error.append(temp);
		}
		agk::Log(error.c_str());
		//return S_FALSE;
	}
	}

	//if (_outFileStream)
	//{
	//	if (_processedFileInfo.MTimeDefined)
	//		_outFileStreamSpec->SetMTime(&_processedFileInfo.MTime);
	//	RINOK(_outFileStreamSpec->Close());
	//}
	//agk::Message("SetOperationResult");

	//char value[30];
	//ZeroMemory(value, 30);
	//for (int x = 0; x < 10; x++)
	//{
	//	_itoa_s(data[x], value + x * 3, 3, 16); // should be 48 65 79 20 74 68 65 72 65 21 (Hey there!)
	//}
	//for (int x = 0; x < 30; x++)
	//{
	//	if (value[x] == 0)
	//	{
	//		value[x] = ' ';
	//	}
	//}
	//value[29] = '\0';
	//////_itoa(procSize, value, 10);
	//agk::Message(value);
	// Create a memblock from the extracted data.
	if ((operationResult == NArchive::NExtract::NOperationResult::kOK) && (_dataSize > 0))
	{
#ifdef VERBOSE
		std::string status = "Creating memblock.  Size = ";
		char num[30];
		ZeroMemory(num, 30);
		itoa(_dataSize, num, 10);
		status.append(num);
		agk::Log(status.c_str());
#endif
		MemblockID = agk::CreateMemblock(_dataSize);
		memcpy_s(agk::GetMemblockPtr(MemblockID), _dataSize, _data, _dataSize);
	}
	delete[] _data;
	_dataSize = 0;
	_outStream.Release();
	//if (_extractMode && _processedFileInfo.AttribDefined)
	//	SetFileAttrib_PosixHighDetect(_diskFilePath, _processedFileInfo.Attrib);
	//PrintNewLine();
	return S_OK;
}


STDMETHODIMP CArchiveExtractCallback::CryptoGetTextPassword(BSTR *password)
{
	if (!PasswordIsDefined)
	{
		// You can ask real password here from user
		// Password = GetPassword(OutStream);
		// PasswordIsDefined = true;
		agk::PluginError("Extract: Password is not defined");
		return E_ABORT;
	}
	return StringToBstr(Password, password);
}

//////////////////////////////////////////////////////////////
// Archive Creating callback class

STDMETHODIMP CArchiveUpdateCallback::SetTotal(UInt64 size)
{
	TotalSize = size;
	return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::SetCompleted(const UInt64 *completeValue)
{
	::UpdateProgressSprite((int)*completeValue, (int)TotalSize);
	return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::GetUpdateItemInfo(UInt32 index,
	Int32 *newData, Int32 *newProperties, UInt32 *indexInArchive)
{
	const CDirItem &dirItem = (*DirItems)[index];
	if (newData)
	{
		*newData = BoolToInt(dirItem.Data != NULL);
	}
	if (newProperties)
	{
		*newProperties = BoolToInt((dirItem.Data != NULL) || dirItem.OldIndex == MAXUINT32);
	}
	if (indexInArchive)
	{
		*indexInArchive = dirItem.OldIndex;
	}
#ifdef VERBOSE
	std::string msg;
	char *num;
	num = agk::Str((int)index);
	msg.append("GetUpdateItemInfo: ").append(num);
	agk::DeleteString(num);
	AString aname;
	UnicodeStringToMultiByte2(aname, dirItem.Name, (UINT)CP_OEMCP);
	msg.append(", Name: ").append(aname);
	num = agk::Str(*newData);
	msg.append(", newData: ").append(num);
	agk::DeleteString(num);
	num = agk::Str(*newProperties);
	msg.append(", newProperties: ").append(num);
	agk::DeleteString(num);
	num = agk::Str((int)*indexInArchive);
	msg.append(", indexInArchive: ").append(num);
	agk::DeleteString(num);
	agk::Log(msg.c_str());
#endif
	return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value)
{
	NCOM::CPropVariant prop;

	if (propID == kpidIsAnti)
	{
		prop = false;
		prop.Detach(value);
		return S_OK;
	}
	{
		const CDirItem &dirItem = (*DirItems)[index];
		switch (propID)
		{
		case kpidPath:  prop = dirItem.Name; break;
		case kpidIsDir:  prop = dirItem.isDir(); break;
		case kpidSize:  prop = dirItem.Size; break;
		case kpidAttrib:  prop = dirItem.Attrib; break;
		case kpidCTime:  prop = dirItem.CTime; break;
		case kpidATime:  prop = dirItem.ATime; break;
		case kpidMTime:  prop = dirItem.MTime; break;
		}
	}
	prop.Detach(value);
	return S_OK;
}

HRESULT CArchiveUpdateCallback::Finilize()
{
	if (m_NeedBeClosed)
	{
		//PrintNewLine();
		m_NeedBeClosed = false;
	}
	return S_OK;
}

static void GetStream2(const wchar_t *name)
{
	if (name[0] == 0)
		name = kEmptyFileAlias;
#ifdef VERBOSE
	std::string msg;
	msg.append("Compressing  ");
	//agk::Log(name);
	int length = wcslen(name) + 1;
	char *aname = new char[length];
	ZeroMemory(aname, length);
	wcstombs(aname, name, length - 1);
	msg.append(aname);
	agk::Log(msg.c_str());
#endif
}

STDMETHODIMP CArchiveUpdateCallback::GetStream(UInt32 index, ISequentialInStream **inStream)
{
	RINOK(Finilize());

#ifdef VERBOSE
	std::string msg;
	char *num = agk::Str((int)index);
	msg.append("CArchiveUpdateCallback::GetStream: ").append(num);
	agk::DeleteString(num);
	agk::Log(msg.c_str());
#endif
	//_dataSize = 0;
	const CDirItem &dirItem = (*DirItems)[index];
	GetStream2(dirItem.Name);

	if (dirItem.isDir())
		return S_OK;

	{
		// Prepare buffer.
		if (dirItem.Data == NULL)
		{
			agk::PluginError("No data was given.");
			return S_FALSE;
			//return S_OK;
		}
		CBufInStream *inStreamSpec = new CBufInStream;
		CMyComPtr<ISequentialInStream> inStreamLoc(inStreamSpec);
		inStreamSpec->Init(dirItem.Data, dirItem.DataLength);
		//CInFileStream *inStreamSpec = new CInFileStream;
		//CMyComPtr<ISequentialInStream> inStreamLoc(inStreamSpec);
		//FString path = DirPrefix + dirItem.FullPath;
		//if (!inStreamSpec->Open(path))
		//{
		//	DWORD sysError = ::GetLastError();
		//	FailedCodes.Add(sysError);
		//	FailedFiles.Add(path);
		//	// if (systemError == ERROR_SHARING_VIOLATION)
		//	{
		//		agk::PluginError("WARNING: can't open file");
		//		// Print(NError::MyFormatMessageW(systemError));
		//		return S_FALSE;
		//	}
		//	// return sysError;
		//}
		*inStream = inStreamLoc.Detach();
	}
	return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::SetOperationResult(Int32 /* operationResult */)
{
	m_NeedBeClosed = true;
	return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::GetVolumeSize(UInt32 index, UInt64 *size)
{
	if (VolumesSizes.Size() == 0)
		return S_FALSE;
	if (index >= (UInt32)VolumesSizes.Size())
		index = VolumesSizes.Size() - 1;
	*size = VolumesSizes[index];
	return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::GetVolumeStream(UInt32 index, ISequentialOutStream **volumeStream)
{
	wchar_t temp[16];
	ConvertUInt32ToString(index + 1, temp);
	UString res = temp;
	while (res.Len() < 2)
		res.InsertAtFront(L'0');
	UString fileName = VolName;
	fileName += '.';
	fileName += res;
	fileName += VolExt;
	COutFileStream *streamSpec = new COutFileStream;
	CMyComPtr<ISequentialOutStream> streamLoc(streamSpec);
	if (!streamSpec->Create(us2fs(fileName), false))
		return ::GetLastError();
	*volumeStream = streamLoc.Detach();
	return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::CryptoGetTextPassword(BSTR *password)
{
	//agk::Log("CryptoGetTextPassword");
	if (!PasswordIsDefined)
	{
		// You can ask real password here from user
		// Password = GetPassword(OutStream);
		// PasswordIsDefined = true;
		agk::PluginError("Update: Password is not defined");
		return E_ABORT;
	}
	return StringToBstr(Password, password);
}

STDMETHODIMP CArchiveUpdateCallback::CryptoGetTextPassword2(Int32 *passwordIsDefined, BSTR *password)
{
	//agk::Log("CryptoGetTextPassword2");
	if (!PasswordIsDefined)
	{
		if (AskPassword)
		{
			// You can ask real password here from user
			// Password = GetPassword(OutStream);
			// PasswordIsDefined = true;
			agk::PluginError("Update2: Password is not defined.");
			return E_ABORT;
		}
	}
	*passwordIsDefined = BoolToInt(PasswordIsDefined);
	return StringToBstr(Password, password);
}
