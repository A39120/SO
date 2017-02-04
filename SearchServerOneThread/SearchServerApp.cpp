#include <Windows.h>
#include <process.h>
#include <stdio.h>
#include <assert.h>
#include "SearchService.h"
#include "SearchServerApp.h"

static PSearchService service;
REQUEST_LIST requests;
FILE_LOCK_LIST files;

VOID ErrorExit(LPSTR lpszMessage) {
	fprintf(stderr, "%s\n", lpszMessage);
	ExitProcess(0);
}

VOID InitializeFileLockList() {
	files = { 0 };
	files.lock = (PFILE_LOCK)malloc(sizeof(FILE_LOCK) * MAX_ENTRIES);
	memset(files.lock, 0, sizeof(FILE_LOCK) * MAX_ENTRIES);
	InitializeCriticalSection(&files.file_register);
}

PFILE_LOCK RegisterFileLock(PCHAR file) {
	EnterCriticalSection(&files.file_register);
	PCHAR fileReg = (files.lock + files.count)->file;
	strcpy_s(fileReg, MAX_CHARS, file);

	WCHAR tmp[MAX_CHARS];
	_snwprintf_s(tmp, _countof(tmp), L"%sFileMtx", fileReg);
	(files.lock + files.count)->mutex = CreateMutexW(NULL, FALSE, tmp);

	files.count += 1;
	LeaveCriticalSection(&files.file_register);

	return files.lock + (files.count - 1);
}

PFILE_LOCK GetFileLock(PCHAR file) {
	EnterCriticalSection(&files.file_register);
	for (DWORD i = 0; i < files.count; i++) {
		if (strcmp(file, (files.lock + i)->file) == 0) {
			LeaveCriticalSection(&files.file_register);
			return files.lock + i;
		}
	}
	LeaveCriticalSection(&files.file_register);
	return RegisterFileLock(file);
}

VOID CloseFileLocks() {
	for (DWORD i = 0; i < files.count; i++) {
		CloseHandle((files.lock + i)->mutex);
	}
	DeleteCriticalSection(&files.file_register);
	free(files.lock);
}


VOID InitializeRequestList(DWORD size) {
	requests = { 0 };
	requests.files = (PREQUEST)malloc(sizeof(REQUEST) * size);
	for (int i = 0; i < size; i++) {
		(requests.files + i)->completed = TRUE;
		(requests.files + i)->thread = INVALID_HANDLE_VALUE;
	}

	requests.emptySem = CreateSemaphore(NULL, 0, size, NULL);
	requests.fullSem = CreateSemaphore(NULL, size, size, NULL);
	requests.size = size;
	InitializeCriticalSection(&requests.request_section);
}
VOID CloseRequestList() {
	CloseHandle(requests.emptySem);
	CloseHandle(requests.fullSem);
	DeleteCriticalSection(&requests.request_section);
	free(requests.files);
}
PREQUEST GetRequest() {
	PREQUEST req;
	for (int get = 0; get < requests.size; get++) {
		req = requests.files + get;
		
		if (!req->completed && req->thread == INVALID_HANDLE_VALUE) {
			//Não tem thread, ou seja, ainda não está a ser tratado
			return req;
		}
	}
	ErrorExit("Requests list is broken. Should be impossible to reach here.");
}
PREQUEST PutRequest(PCHAR file, PEntry entry, HANDLE answer) {
	DWORD res, put;
	res = WaitForSingleObject(requests.fullSem, INFINITE);
	assert(res != WAIT_FAILED);

	EnterCriticalSection(&requests.request_section);
	PREQUEST req = requests.files;
	for (int put = 0; put < requests.size; put++) {
		req = requests.files + put;
		if (req->completed) 
			goto complete;
	}
	LeaveCriticalSection(&requests.request_section);
	ErrorExit("Not found. Should be impossible with semaphores.");
	return 0;
complete: 
	req->threadEvt = CreateEvent(NULL, TRUE, FALSE, NULL);
	req->thread = INVALID_HANDLE_VALUE;
	req->completed = FALSE;
	req->answEvt = answer;
	strcpy_s(req->file, MAX_CHARS, file);
	req->entry = entry;
	LeaveCriticalSection(&requests.request_section);
	ReleaseSemaphore(requests.emptySem, 1, NULL);
	return req;
}


/* 
 * Gets the number of processors in the system
 */
DWORD getNumberOfProcessors() {
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwNumberOfProcessors;
}

/* -----------------------------------------------------------------------
 * This function allows the processing of a selected set of files in a 
 * directory.
 * It uses the Windows functions for directory file iteration, namely
 * "FindFirstFile" and "FindNextFile"
 * @param path
 * @param entry
 */
VOID processEntry(PCHAR path, PEntry entry) {
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

	HANDLE evt = CreateEvent(NULL, TRUE, FALSE, NULL);
	HANDLE answEvt = CreateEvent(NULL, FALSE, TRUE, NULL);
	LONG count = 0, total = 0;
	LONG hhSize = MAX_ENTRIES;
	PHANDLE hh = (PHANDLE)malloc(sizeof(HANDLE) * hhSize);
	// process only file entries
	do {
		if (fileData.dwFileAttributes == FILE_ATTRIBUTE_ARCHIVE) {
			if (total > hhSize) {
				hh = (PHANDLE)realloc(hh, sizeof(HANDLE)* total * 2);
				hhSize = total * 2;
			}
			PREQUEST req = PutRequest(fileData.cFileName, entry, answEvt);
			WaitForSingleObject(req->threadEvt, INFINITE);
			hh[total] = req->thread;	//Isto está apenas a copiar os handles das threads!!!
			total += 1;
		}
	} while (FindNextFile(iterator, &fileData));

	//Wait for all the threads that did work
	res = WaitForMultipleObjects(total, hh, TRUE, INFINITE);
	assert(res != WAIT_FAILED);

	// signal the client and finish answer
	SetEvent(entry->answReadyEvt);
	CloseHandle(entry->answReadyEvt);
	FindClose(iterator);
}

/* -----------------------------------------------------------------------
 * Thread responsible for reading files.
 */
unsigned __stdcall file_thread(void* arg) {
	//WaitForSingleObject(requests.emptySem, INFINITE);
	//PREQUEST req = GetRequest();
	PREQUEST req = (PREQUEST)arg;
	//Read the file
	// alloc buffer to hold bytes readed from file stream
	DWORD tokenSize = strlen(req->entry->value);
	PCHAR windowBuffer = (PCHAR)HeapAlloc(GetProcessHeap(), 0, tokenSize + 1);

	CHAR c;
	DWORD bytesReaded, res;

	_tprintf(_T("Search on file: %s\n"), req->file);

	HANDLE mutex = GetFileLock(req->file);
	WaitForSingleObject(mutex, INFINITE);
	HANDLE hFile = CreateFile(req->file, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) 
		goto error;
	//assert(hFile != INVALID_HANDLE_VALUE);

	// clear windowBuffer
	memset(windowBuffer, 0, tokenSize + 1);

	res = ReadFile(hFile, &c, 1, &bytesReaded, NULL);
	while (res && bytesReaded == 1) {
		// slide window to accommodate new char
		memmove_s(windowBuffer, tokenSize, windowBuffer + 1, tokenSize - 1);
		windowBuffer[tokenSize - 1] = c;
		// test accumulated bytes with token
		if (memcmp(windowBuffer, req->entry->value, tokenSize) == 0) {
			// append filename to answer and go to next file
			WaitForSingleObject(req->answEvt, INFINITE);
			// set auxiliary vars
			PSharedBlock pSharedBlock = (PSharedBlock)service->sharedMem;
			PCHAR answer = pSharedBlock->answers[req->entry->answIdx];
			//memset(answer, 0, MAX_CHARS);
			strcat_s(answer, MAX_CHARS, req->file);
			strcat_s(answer, MAX_CHARS, "\n");
			SetEvent(req->answEvt);
			break;
		}
		res = ReadFile(hFile, &c, 1, &bytesReaded, NULL);
	}
	CloseHandle(hFile);
	ReleaseMutex(mutex);
	HeapFree(GetProcessHeap(), 0, windowBuffer);
	EnterCriticalSection(&requests.request_section);
	req->completed = TRUE;
	LeaveCriticalSection(&requests.request_section);
	//Release Semaphores
	ReleaseSemaphore(requests.fullSem, 1, NULL);
	printf("Search completed for: %s\n", req->file);
	return 1;
error:
	EnterCriticalSection(&requests.request_section);
	req->completed = TRUE;
	LeaveCriticalSection(&requests.request_section);
	ReleaseSemaphore(requests.fullSem, 1, NULL);
	printf("Error reading file: %s (Error code:%d)\n", req->file, GetLastError());
	return 0;
}

/*--------------------------------------------------------------------------
 * This function corresponds to a thread created on main. This function will 
 * run in a loop taking requests from SearchClient. It uses the global var   
 * PSearchService to get the request. It will only stop when SearchStopper 
 * is called.
 */
unsigned __stdcall server_thread(void* arg) {
	bool res;

	Entry entry;
	PPROCESS_CONTEXT ctx = (PPROCESS_CONTEXT)arg;

	while (1) {
		res = SearchGet(service, &entry);	
		if (res == FALSE) //If false it'll end the program because of SearchStopEvent
			break;
	
		processEntry(ctx->path, &entry);
	}
	return 1;
}

unsigned __stdcall request_thread(void* arg) {
	DWORD res;
	HANDLE hh[] = { (HANDLE)arg, requests.emptySem };
	while (1) {
		res = WaitForMultipleObjects(2, hh, FALSE, INFINITE);
		if (res - WAIT_OBJECT_0 == 0) return 0;

		EnterCriticalSection(&requests.request_section);
		PREQUEST req = GetRequest();
		req->thread = (HANDLE)_beginthreadex(NULL, 0, file_thread, req, 0, NULL);
		SetEvent(req->threadEvt);
		LeaveCriticalSection(&requests.request_section);
	}

	return 1;
error: 
	return 0;
}

/*------------------------------------------------------------------------
 * Main thread, creates the server threads.
 * @param: argv[1] - Service name, Service1 by default;
 * @param: argv[2] - Service directory, it's a debug directory by default;
 */
INT main(DWORD argc, PCHAR argv[]) {
	PCHAR name;
	PCHAR path;
	DWORD res;

	//Get the number of CPUs
	DWORD numberOfProcessors = getNumberOfProcessors();
	CHAR pathname[MAX_CHARS*4];
	HANDLE threads[MAX_SERVERS];
	HANDLE requestThread;
	HANDLE requestEvt = CreateEvent(NULL, TRUE, FALSE, NULL);

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

	InitializeFileLockList();
	InitializeRequestList(numberOfProcessors);
	requestThread = (HANDLE)_beginthreadex(NULL, 0, request_thread, requestEvt, 0, NULL);

	//Create the threads that get requests;
	PROCESS_CONTEXT ctx;
	ctx.path = path;
	void * pCtx = &ctx;
	for (int i = 0; i < MAX_SERVERS; i++) {
		ctx.i = i;
		threads[i] = (HANDLE)_beginthreadex(NULL, 0, server_thread, pCtx, 0, NULL);
	}

	//Waits for the services to finish, they'll await for the stop service event
	res = WaitForMultipleObjects(MAX_SERVERS, threads, TRUE, INFINITE);
	assert(res != WAIT_FAILED);

	printf("Server app: Close service name = %s and exit\n", name);

	//Closes the threads handle
	for (int i = 0; i < MAX_SERVERS; i++) {
		CloseHandle(threads[i]);
	}

	CloseRequestList();

	SetEvent(requestEvt); //Signal the request thread to end
	res = WaitForSingleObject(requestThread, INFINITE);
	assert(res != WAIT_FAILED);
	CloseHandle(requestEvt);
	CloseHandle(requestThread);
	CloseFileLocks();

	SearchClose(service);
	return 0;
}
