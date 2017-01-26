#include <Windows.h>
#include <process.h>
#include <stdio.h>
#include <assert.h>
#include "SearchService.h"
#include "SearchServerApp.h"

static PSearchService service;
DWORD dwTlsIndex[MAX_SERVERS];

FILE_LIST fileList;

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
	PFILE_NODE fn = fileList.list + numberOfFiles;
	fn = (PFILE_NODE)malloc(sizeof(FILE_NODE));
	
	strcpy(fn->name, file);
	sprintf(fn->lockName, L"%sMtx", file);
	fn->fileLock = (HANDLE)CreateMutex(NULL, FALSE, mtxName);
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
	for (int i = 0; i < fileList.numberOfFiles; i++) {
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
	for (int i = 0; i < fileList.numberOfFiles; i++) {
		PFILE_NODE fn = fileList + i;
		CloseHandle(fn->fileLock);
	}
	free(fileList.list);
}

/*-----------------------------------------------------------------------
This function allows the processing of a selected set of files in a directory
It uses the Windows functions for directory file iteration, namely
"FindFirstFile" and "FindNextFile"
*/
VOID processEntry(PCHAR path, PEntry entry) {
	HANDLE iterator;
	WIN32_FIND_DATA fileData;
	TCHAR buffer[MAX_PATH];		// auxiliary buffer
	DWORD tokenSize;

	_tprintf(_T("Token to search: %s\n"), entry->value);

	// the buffer is needed to define a match string that guarantees 
	// a priori selection for all files
	_stprintf_s(buffer, _T("%s\\%s"), path, _T("*.*"));

	// start iteration
	if ((iterator = FindFirstFile(buffer, &fileData)) == INVALID_HANDLE_VALUE)
		goto error;

	// alloc buffer to hold bytes readed from file stream
	tokenSize = strlen(entry->value);
	PCHAR windowBuffer = (PCHAR)HeapAlloc(GetProcessHeap(), 0, tokenSize + 1);
	// set auxiliary vars
	PCHAR answer;
	PSharedBlock pSharedBlock = (PSharedBlock)service->sharedMem;
	answer = pSharedBlock->answers[entry->answIdx];
	memset(answer, 0, MAX_CHARS);

	// process only file entries
	do {
		if (fileData.dwFileAttributes == FILE_ATTRIBUTE_ARCHIVE) {
			CHAR c;
			DWORD res, bytesReaded;

			_tprintf(_T("Search on file: %s\n"), fileData.cFileName);
			HANDLE hFile = CreateFile(fileData.cFileName, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			assert(hFile != INVALID_HANDLE_VALUE);

			// clear windowBuffer
			memset(windowBuffer, 0, tokenSize + 1);

			res = ReadFile(hFile, &c, 1, &bytesReaded, NULL);
			while (res && bytesReaded == 1) {
				
				// slide window to accommodate new char
				memmove_s(windowBuffer, tokenSize, windowBuffer + 1, tokenSize - 1);
				windowBuffer[tokenSize - 1] = c;

				// test accumulated bytes with token
				if (memcmp(windowBuffer, entry->value, tokenSize) == 0) {
					
					// append filename to answer and go to next file
					strcat_s(answer, MAX_CHARS, fileData.cFileName);
					strcat_s(answer, MAX_CHARS, "\n");
					break;
				}
				res = ReadFile(hFile, &c, 1, &bytesReaded, NULL);
			}
			CloseHandle(hFile);
		}
	} while (FindNextFile(iterator, &fileData));

	// sinalize client and finish answer
	SetEvent(entry->answReadyEvt);
	CloseHandle(entry->answReadyEvt);

	FindClose(iterator);
	HeapFree(GetProcessHeap(), 0, windowBuffer);


error:
	;

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

INT main(DWORD argc, PCHAR argv[]) {
	PCHAR name;
	PCHAR path;
	DWORD res;
	DWORD numberOfProcessors;
	CHAR pathname[MAX_CHARS*4];
	HANDLE threads[MAX_SERVERS];

	for (int i = 0; i < MAX_SERVERS; i++) {
		//each server has it's own storage
		if ((dwTlsIndex[i] = TlsAlloc()) == TLS_OUT_OF_INDEXES)
			ErrorExit("TlsAlloc failed");
	}
	
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

	PROCESS_CONTEXT ctx;
	ctx.path = path;
	void * pCtx = &ctx;
	for (int i = 0; i < MAX_SERVERS; i++) {
		ctx.i = i;
		threads[i] = (HANDLE)_beginthreadex(NULL, 0, server_thread, pCtx, 0, NULL);
	}

	res = WaitForMultipleObjects(MAX_SERVERS, threads, TRUE, INFINITE);
	printf("Server app: Close service name = %s and exit\n", name);
	SearchClose(service);

	return 0;
}
