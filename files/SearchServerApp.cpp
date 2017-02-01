#include <Windows.h>
#include <process.h>
#include <stdio.h>
#include <assert.h>
#include "SearchService.h"

#define MAX_THREADS 16
#define INITIAL_LIST_SIZE 5
#define THREADS 5

static PSearchService service;
CRITICAL_SECTION cs;	//This critical section shall be used when the service will get the mutex for the file

//This structure is to avoid error 32
typedef struct {
	PCHAR name;
	//HANDLE file;	//from create file
	PCHAR mutexName;
	HANDLE mutex;	//mutex to handle the file
	//PCHAR eventName;
	//HANDLE evt;
}FILE_ENTRY, *PFILE_ENTRY;
typedef struct {
	DWORD res;
	PCHAR path;
	Entry* entry;
	DWORD id;
}CTX;
typedef struct {
	HANDLE thread;
}FILE_THREAD, *PFILE_THREAD;

PFILE_ENTRY files;
size_t fileListSize;
PFILE_THREAD fileHandlers;

VOID extendFilesList() {
	fileListSize *= 2;
	files = (PFILE_ENTRY)realloc(files, fileListSize);
}
PFILE_ENTRY WINAPI addFileEntry(PCHAR name) {
	PFILE_ENTRY aux = files;
	for (int cnt = 0; !aux->name ; cnt++) {
		if (cnt >= fileListSize) {
			extendFilesList();
			return addFileEntry(name);
		}
		aux++;
	}

	//HANDLE hFile = CreateFile(name, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	//assert(hFile != INVALID_HANDLE_VALUE);
	//aux->file = hFile;

	aux->mutexName = (PCHAR)malloc(sizeof(char)*MAX_CHARS);
	strcpy_s(aux->mutexName, MAX_CHARS, name);
	strcat_s(aux->mutexName, MAX_CHARS, "Mtx");
	HANDLE fileMtx = CreateMutex(NULL, false, aux->mutexName);
	assert(fileMtx != INVALID_HANDLE_VALUE);
	aux->mutex = fileMtx;

	//aux->eventName = (PCHAR)malloc(sizeof(char)*MAX_CHARS);
	//strcpy_s(aux->eventName, MAX_CHARS, name);
	//strcat_s(aux->eventName, MAX_CHARS, "Event");
	//HANDLE fileEvt = CreateEvent(NULL, TRUE, FALSE, aux->eventName); //evento de reset manual
	//assert(fileEvt != INVALID_HANDLE_VALUE);
	//aux->evt = fileEvt;

	aux->name = name;
	return aux;
}
VOID initFilesList(DWORD fileNumber) {
	files = (PFILE_ENTRY)malloc(sizeof(FILE_ENTRY)*fileNumber);
	fileListSize = fileNumber;
	PFILE_ENTRY aux = files;
	for (DWORD cnt = 0; cnt < fileListSize; cnt++) {
		//aux->file = INVALID_HANDLE_VALUE;
		//aux->mutex = INVALID_HANDLE_VALUE;
		aux->name = NULL;
		aux++;
	}
}
VOID WINAPI deleteFilesList() {
	for (DWORD cnt = 0; cnt < fileListSize; cnt++, files++) {
		files->name = "/0";
		//CloseHandle(files->file);
		free(files->mutexName);
		CloseHandle(files->mutex);
		//free(files->eventName);
		//CloseHandle(files->evt);
	}
	free(files);
}
PFILE_ENTRY WINAPI getFile(PCHAR fileName) {
	PFILE_ENTRY aux = files;
	for (DWORD cnt = 0; cnt < fileListSize; cnt++, aux++) {
		if (aux->name == fileName) {
			return aux;
		}
	}
	return addFileEntry(fileName);
}

DWORD getNumberOfProcessors() {
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwNumberOfProcessors;
}

/*-----------------------------------------------------------------------
This function allows the processing of a selected set of files in a directory
It uses the Windows functions for directory file iteration, namely
"FindFirstFile" and "FindNextFile"
*/
DWORD WINAPI processEntry(LPVOID arg) {
	CTX * ctx = (CTX*)arg;
	HANDLE iterator;
	WIN32_FIND_DATA fileData;
	TCHAR buffer[MAX_PATH];		// auxiliary buffer
	DWORD tokenSize;

	_tprintf(_T("(%d)Token to search: %s\n"), ctx->id, ctx->entry->value);

	// the buffer is needed to define a match string that guarantees 
	// a priori selection for all files
	_stprintf_s(buffer, _T("%s\\%s"), ctx->path, _T("*.*"));

	// start iteration
	if ((iterator = FindFirstFile(buffer, &fileData)) == INVALID_HANDLE_VALUE)
		goto error;

	// alloc buffer to hold bytes readed from file stream
	tokenSize = strlen(ctx->entry->value);
	PCHAR windowBuffer = (PCHAR)HeapAlloc(GetProcessHeap(), 0, tokenSize + 1);
	// set auxiliary vars
	PCHAR answer;
	PSharedBlock pSharedBlock = (PSharedBlock)service->sharedMem;
	answer = pSharedBlock->answers[ctx->entry->answIdx];
	memset(answer, 0, MAX_CHARS);

	// process only file entries
	do {
		if (fileData.dwFileAttributes == FILE_ATTRIBUTE_ARCHIVE) {
			CHAR c;
			DWORD res, bytesReaded;
			HANDLE mutex;

			_tprintf(_T("(%d)Search on file: %s\n"), ctx->id, fileData.cFileName);

			// file entry will only store mutex
			EnterCriticalSection(&cs);
			PFILE_ENTRY fileEntry = getFile(fileData.cFileName);
			LeaveCriticalSection(&cs);

			mutex = OpenMutex(MUTEX_ALL_ACCESS, FALSE, fileEntry->mutexName);
			// can't be in the fileEntry?
			HANDLE hFile = CreateFile(fileData.cFileName, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

			// clear windowBuffer
			memset(windowBuffer, 0, tokenSize + 1);

			res = ReadFile(hFile, &c, 1, &bytesReaded, NULL);
			while (res && bytesReaded == 1) {
				
				// slide window to accommodate new char
				memmove_s(windowBuffer, tokenSize, windowBuffer + 1, tokenSize - 1);
				windowBuffer[tokenSize - 1] = c;

				// test accumulated bytes with token
				if (memcmp(windowBuffer, ctx->entry->value, tokenSize) == 0) {
					
					// append filename to answer and go to next file
					strcat_s(answer, MAX_CHARS, fileData.cFileName);
					strcat_s(answer, MAX_CHARS, "\n");
					break;
				}
				res = ReadFile(hFile, &c, 1, &bytesReaded, NULL);
			}
			ReleaseMutex(mutex);
			CloseHandle(hFile);
		}
	} while (FindNextFile(iterator, &fileData));

	_tprintf(_T("Finished Search for: %s\n"), ctx->path);
	// sinalize client and finish answer
	SetEvent(ctx->entry->answReadyEvt);
	CloseHandle(ctx->entry->answReadyEvt);	//First thread might close this handle

	FindClose(iterator);
	HeapFree(GetProcessHeap(), 0, windowBuffer);

	return 0;
error:
	return -1;

}

DWORD WINAPI readEntry(LPVOID arg) {
	CTX * ctx = (CTX*)arg;
	HANDLE ht;
	Entry entry;

	ctx->entry = &entry;

	while(1) {
		ctx->res = SearchGet(service, ctx->entry);	//SearchGet will block, no need to block here
		if (ctx->res == FALSE)
			break;

		processEntry(ctx);
	}
	return 0;
}

DWORD WINAPI proccessFile(LPVOID arg) {
	//Processa um ficheiro que esteja na lista
	//Necessita de um stack {PCHAR com nome do ficheiro, 
	//	PCHAR com o nome da palavra a procurar, 
	//	BIT a dizer se tem ou não aquela palavra}
	//Necessita de um semaforo para o inicio e um semaforo para o limite
	
}

INT main(DWORD argc, PCHAR argv[]) {
	CTX ctx[THREADS];

	DWORD nop = getNumberOfProcessors();

	HANDLE ht[THREADS];
	PCHAR name;
	PCHAR path;
	DWORD res;
	CHAR pathname[MAX_CHARS*4];

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
	InitializeCriticalSection(&cs);
	initFilesList(INITIAL_LIST_SIZE);

	fileHandlers = (PFILE_THREAD)malloc(sizeof(FILE_THREAD)*getNumberOfProcessors());
	PFILE_THREAD aux = fileHandlers;

	//Threads to handle the files
	for (int i = 0; i < getNumberOfProcessors(); i++, aux++) {
		aux->thread = CreateThread(NULL, 0, readEntry, NULL, 0, NULL);
	}

	//Threads to handle the multiple clients
	for (int i = 0; i < THREADS; i++) {
		ctx[i].path = path;
		ctx[i].id = i;
		ht[i] = CreateThread(NULL, 0, readEntry, &ctx[i], 0, NULL);
	}

	//for (int i = 0; i < MAX_SERVERS; i++) {
	//	threads[i] = (HANDLE)_beginthreadex(NULL, 0, server_thread, (LPVOID)i, 0, NULL);
	//}

	//res = WaitForMultipleObjects(MAX_SERVERS, threads, TRUE, INFINITE);
	WaitForMultipleObjects(THREADS, ht, true, INFINITE);
	DeleteCriticalSection(&cs);
	deleteFilesList();
	printf("Server app: Close service name = %s and exit\n", name);
	SearchClose(service);
	
	return 0;
}


