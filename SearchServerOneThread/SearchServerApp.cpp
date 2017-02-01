#include <Windows.h>
#include <process.h>
#include <stdio.h>
#include <assert.h>
#include "SearchService.h"
#include "SearchServerApp.h"

static PSearchService service;
FILE_LIST fileList;
REQUEST_LIST requests;

VOID ErrorExit(LPSTR lpszMessage)
{
	fprintf(stderr, "%s\n", lpszMessage);
	ExitProcess(0);
}

DWORD getNumberOfProcessors() {
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwNumberOfProcessors;
}

/**
* Adds file to file list
* @param file - size of list, if 0 then it'll be MAX_ENTRIES by default
*/
VOID initFileList(LONG size){
	DWORD checkSize = size;
	if (checkSize == 0) 
		checkSize = MAX_ENTRIES;

	fileList = { 0 };
	fileList.list = (PFILE_NODE)malloc(sizeof(FILE_NODE) * checkSize);
	InitializeCriticalSection(&(fileList.cs));
}

/**
* Adds file to file list
* @param file - file name
* @return PFILE_NODE of corresponding file node
*/
PFILE_NODE addToFileList(PCHAR file) {
	PFILE_NODE fn = fileList.list + fileList.numberOfFiles;
	fn = (PFILE_NODE)malloc(sizeof(FILE_NODE));
	
	memcpy(fn->name, file, MAX_CHARS);
	sprintf_s(fn->lockName, MAX_CHARS, "%sMtx", file);
	fn->fileLock = (HANDLE)CreateMutex(NULL, FALSE, fn->lockName);
	//fn->fileLock = (HANDLE)InitializeCriticalSection...
	//fn->fileLock = (HANDLE)CreateEvent...
	return fn;
}

/**
 * Checks for file in file list
 * @param file - File name
 * @return PFILE_NODE of corresponding file node
 */
PFILE_NODE getFile(PCHAR file) {
	for (DWORD i = 0; i < fileList.numberOfFiles; i++) {
		if (strcmp(file, (fileList.list + i)->name) == 0) {
			return (fileList.list + i);
		}
	}
	return addToFileList(file);
}

/**
 * Closes file list.
 */
VOID closeFileList() {
	for (DWORD i = 0; i < fileList.numberOfFiles; i++) {
		PFILE_NODE fn = (fileList.list + i);
	}
	CloseHandle(&fileList.cs);
	free(fileList.list);
}

VOID InsertRequest(PCHAR filename, PTHREAD_LOCAL local) {
	EnterCriticalSection(&requests.request_section);
	PREQUEST_NODE req = requests.files + requests.put;
	requests.put = (requests.put + 1) % MAX_ENTRIES;
	memcpy_s(&req->file, MAX_CHARS, filename, MAX_CHARS);
	req->local = local;
	LeaveCriticalSection(&requests.request_section);
}


/*-----------------------------------------------------------------------
This function allows the processing of a selected set of files in a directory
It uses the Windows functions for directory file iteration, namely
"FindFirstFile" and "FindNextFile"
*/
VOID processEntry(PCHAR path, PEntry entry) {
	//DWORD dwTlsIndex;
	//if ((dwTlsIndex = TlsAlloc()) == TLS_OUT_OF_INDEXES)
	//	ErrorExit("TlsAlloc failed");
	//
	//PTHREAD_LOCAL local = (PTHREAD_LOCAL)LocalAlloc(LPTR, sizeof(THREAD_LOCAL));
	PTHREAD_LOCAL local = (PTHREAD_LOCAL)malloc(sizeof(THREAD_LOCAL));
	local->entry = entry;
	local->fileCount = 0;

	WCHAR tmp[MAX_CHARS];
	_snwprintf_s(tmp, _countof(tmp), L"%dStartEndingEvt", GetCurrentThreadId());
	local->beginEvt = CreateEventW(NULL, TRUE, FALSE, tmp); assert(local->beginEvt != NULL);

	_snwprintf_s(tmp, _countof(tmp), L"%dEndProccessEvt", GetCurrentThreadId());
	local->endEvt = CreateEventW(NULL, TRUE, FALSE, tmp); assert(local->endEvt != NULL);

	//if (!TlsSetValue(dwTlsIndex, local))
	//	ErrorExit("TlsSetValue error");

	HANDLE iterator;
	WIN32_FIND_DATA fileData;
	TCHAR buffer[MAX_PATH];		// auxiliary buffer
	DWORD res;

	_tprintf(_T("Token to search: %s\n"), entry->value);
	// the buffer is needed to define a match string that guarantees 
	// a priori selection for all files
	_stprintf_s(buffer, _T("%s\\%s"), path, _T("*.*"));

	// start iteration
	if ((iterator = FindFirstFile(buffer, &fileData)) == INVALID_HANDLE_VALUE)
		ErrorExit("Invalid handle given by FindFirstFile.");

	// process only file entries
	do {
		if (fileData.dwFileAttributes == FILE_ATTRIBUTE_ARCHIVE) {
			res = WaitForSingleObject(requests.emptySem, INFINITE);
			assert(res == WAIT_OBJECT_0);

			InsertRequest(fileData.cFileName, local);
			InterlockedIncrement(&(local->fileCount));

			res = ReleaseSemaphore(requests.fullSem, 1, NULL);
			assert(res != 0);
		}
	} while (FindNextFile(iterator, &fileData));

	SetEvent(local->beginEvt);
	res = WaitForSingleObject(local->endEvt, INFINITE);
	assert(res == WAIT_OBJECT_0);

	// sinalize client and finish answer
	SetEvent(entry->answReadyEvt);
	CloseHandle(entry->answReadyEvt);

	free(local);
	//LPVOID lpvData = TlsGetValue(dwTlsIndex);
	//if (lpvData != 0)
	//	LocalFree((HLOCAL)lpvData);

	FindClose(iterator);
}

unsigned __stdcall file_thread(void* arg) {
	PREQUEST_NODE req;
	DWORD res;
	//Semaphore 
	res = WaitForSingleObject(requests.fullSem, INFINITE); 
	assert(res != WAIT_FAILED);

	EnterCriticalSection(&requests.request_section);

	res = ReleaseSemaphore(requests.emptySem, 1, NULL); 
	assert(res != 0);
	
	//Getting request node
	req = requests.files + requests.get;
	requests.get = (requests.get + 1) % MAX_ENTRIES;
	PTHREAD_LOCAL local = req->local;
	LeaveCriticalSection(&requests.request_section);

	//Getting local thread storage
	//PTHREAD_LOCAL local = (PTHREAD_LOCAL)TlsGetValue(req->tlsId);

	// alloc buffer to hold bytes readed from file stream
	DWORD tokenSize = strlen(req->local->entry->value);
	PCHAR windowBuffer = (PCHAR)HeapAlloc(GetProcessHeap(), 0, tokenSize + 1);
	// set auxiliary vars
	PCHAR answer;
	PSharedBlock pSharedBlock = (PSharedBlock)service->sharedMem;
	answer = pSharedBlock->answers[req->local->entry->answIdx];
	memset(answer, 0, MAX_CHARS);
	
	CHAR c;
	DWORD bytesReaded;
	
	_tprintf(_T("Search on file: %s\n"), req->file);
	PFILE_NODE file = getFile(req->file);
	res = WaitForSingleObject(file->fileLock, INFINITE); 
	assert(res == 0);

	HANDLE hFile = CreateFile(file->name, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	assert(hFile != INVALID_HANDLE_VALUE);
	
	// clear windowBuffer
	memset(windowBuffer, 0, tokenSize + 1);
	
	res = ReadFile(hFile, &c, 1, &bytesReaded, NULL);
	while (res && bytesReaded == 1) {
		// slide window to accommodate new char
		memmove_s(windowBuffer, tokenSize, windowBuffer + 1, tokenSize - 1);
		windowBuffer[tokenSize - 1] = c;
		// test accumulated bytes with token
		if (memcmp(windowBuffer, req->local->entry->value, tokenSize) == 0) {
			// append filename to answer and go to next file
			strcat_s(answer, MAX_CHARS, file->name);
			strcat_s(answer, MAX_CHARS, "\n");
			break;
		}
		res = ReadFile(hFile, &c, 1, &bytesReaded, NULL);
	}
	ReleaseMutex(file->fileLock);
	CloseHandle(hFile);
	HeapFree(GetProcessHeap(), 0, windowBuffer);
	
	WaitForSingleObject(req->local->beginEvt, INFINITE);
	InterlockedDecrement(&(req->local->fileCount));
	if (local->fileCount == 0)
		SetEvent(req->local->endEvt);

	return file_thread(arg);
}

/*-----------------------------------------------------------------------
This function corresponds to a thread created on main. This function will 
run in a loop taking requests from SearchClient. It uses the global 
variable PSearchService to get the request. It will only stop when
SearchStopper is called.
*/
unsigned __stdcall server_thread(void* arg) {
	BOOL res;
	Entry entry;
	PPROCESS_CONTEXT ctx = (PPROCESS_CONTEXT)arg;

	while (1) {
		res = SearchGet(service, &entry);
		if (res == FALSE)
			goto error;
	
		processEntry(ctx->path, &entry);
	}
	return 1;
error: 
	return 0;
}

VOID InitializeRequestsList(DWORD nop) {
	requests = { 0 };
	requests.files = (PREQUEST_NODE)malloc(sizeof(REQUEST_NODE)*nop);

	unsigned int nmb = (unsigned int)nop;

	WCHAR msg[MAX_CHARS];
	_snwprintf_s(msg, _countof(msg), L"FreeSem");
	requests.emptySem = CreateSemaphoreW(NULL, MAX_ENTRIES, MAX_ENTRIES, msg);
	assert(requests.emptySem != NULL);

	_snwprintf_s(msg, _countof(msg), L"FullSem");
	requests.fullSem = CreateSemaphoreW(NULL, 0, MAX_ENTRIES, msg);
	assert(requests.fullSem != NULL);

	InitializeCriticalSection(&requests.request_section);

	for (int i = 0; i < nop; i++) {
		HANDLE thread = (HANDLE)_beginthreadex(NULL, 0, file_thread, NULL, 0, &nmb);
	}
}

VOID CloseRequestsList() {
	CloseHandle(requests.emptySem);
	CloseHandle(requests.fullSem);
	DeleteCriticalSection(&requests.request_section);
	free(requests.files);
}


INT main(DWORD argc, PCHAR argv[]) {
	PCHAR name;
	PCHAR path;
	DWORD res;
	DWORD numberOfProcessors = getNumberOfProcessors();
	CHAR pathname[MAX_CHARS*4];
	HANDLE threads[MAX_SERVERS];

	if (argc < 3) {
		printf("Use > %s <service_name> <repository pathname>\n", argv[0]);
		name = "Service1";
		res = GetCurrentDirectory(MAX_CHARS, pathname);
		assert(res > 0);
		path = pathname;
		printf("Using > %s %s \"%s\"\n", argv[0], name, path);
	}
	else {
		name = argv[1];
		path = argv[2];
	}
	printf("Server app: Create service with name = %s. Repository name = %s\n", name, path);
	service = SearchCreate(name); assert(service != NULL);

	InitializeRequestsList(numberOfProcessors);

	PROCESS_CONTEXT ctx;
	ctx.path = path;
	void * pCtx = &ctx;
	for (int i = 0; i < MAX_SERVERS; i++) {
		ctx.i = i;
		threads[i] = (HANDLE)_beginthreadex(NULL, 0, server_thread, pCtx, 0, NULL);
	}

	res = WaitForSingleObject(service->stopServiceEvt, INFINITE); 
	assert(res != WAIT_FAILED);
	//res = WaitForMultipleObjects(MAX_SERVERS, threads, TRUE, INFINITE);
	printf("Server app: Close service name = %s and exit\n", name);
	SearchClose(service);

	return 0;
}
