#include <Windows.h>
#include <process.h>
#include <stdio.h>
#include <assert.h>
#include "SearchService.h"

#define MAX_SERVERS	10

static PSearchService service;

//UINT __stdcall server_thread(LPVOID arg) {
//	INT res;
//	DWORD id = (DWORD)arg;
//	Entry entry;
//
//	printf("Server %d: start\n", id);
//
//	for (;;) {
//		res = FS_Get(service, &entry);
//		if (res == FALSE) break;
//		printf("Server %d: Do factorial(%d)\n", id, entry.value);
//		DWORD result = factorial(entry.value);
//		PSharedBlock block = (PSharedBlock)service->sharedMem;
//		block->answers[entry.answIdx] = result;
//		SetEvent(entry.answReady);
//		CloseHandle(entry.answReady);
//	}
//
//	printf("Server %d: end\n", id);
//
//	return 0;
//}

//VOID FindFiles(TCHAR *path,
//	TCHAR *fileMatch,
//	DirectoryProcessor dp,
//	FileProcessor fp,
//	LPVOID ctx,
//	PFINDFILESCTX pctx) {
//
//	HANDLE iterator;
//	WIN32_FIND_DATA fileData;
//	TCHAR buffer[MAX_PATH];		// auxiliary buffer
//
//
//	// the buffer is needed to define a match string that guarantees 
//	// a priori selection for all files
//	_stprintf_s(buffer, _T("%s%s"), path, _T("*.*"));
//
//	// start iteration
//	if ((iterator = FindFirstFile(buffer, &fileData)) == INVALID_HANDLE_VALUE)
//		goto error;
//
//	// process directory entries
//	do {
//		if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
//			// not processing "." and ".." files!
//			if (_tcscmp(fileData.cFileName, _T("."))
//				&& _tcscmp(fileData.cFileName, _T(".."))) {
//				_stprintf_s(buffer, _T("%s%s/"), path, fileData.cFileName);
//				// call DirectoryProcessor callback if there is one
//				if (dp != NULL)
//					dp(&fileData, path, ctx);
//
//				InterlockedAdd((LONG*)&pctx->nestedCounter, 1);
//
//				// Test if collection is full. In that case the current thread must support the work that it will be delegated to another thread. This way, it is impossible all Worker threads become bloqued.
//				// PROBLEMA do TEST and ACT: resolved when isFull allocs space too.
//				if (ThreadPoolIsFullAndAlloc(pctx->ppool))
//					// recusively process child directory
//					FindFiles(buffer, fileMatch, dp, fp, ctx, pctx);
//				else {
//					// delegate child directory to another thread
//					PWORKCTX pworkctx = (PWORKCTX)malloc(sizeof(WORKCTX));
//					pworkctx->pFindFilesCtx = pctx;
//					pworkctx->path = _tcsdup(buffer);
//
//					ThreadPoolPutWork(pctx->ppool, (UNITOFWORK_PF)FindFileBridge, pworkctx);
//				}
//
//			}
//		}
//		else {
//			if (_tstrmatch(fileMatch, fileData.cFileName))
//				// call FileProcessor callback if there is one
//			if (fp != NULL)
//				fp(&fileData, path, ctx);
//		}
//
//	} while (FindNextFile(iterator, &fileData));
//
//error:
//	FindClose(iterator);
//
//	if (InterlockedAdd((LONG*)&pctx->nestedCounter, -1) == 0) {
//		SetEvent(pctx->finished);
//	}
//}
//
//static VOID FindFileBridge(PWORKCTX ctx) {
//	FindFiles(ctx->path, ctx->pFindFilesCtx->fileMatch, ctx->pFindFilesCtx->dp, ctx->pFindFilesCtx->fp, ctx->pFindFilesCtx->ctx, ctx->pFindFilesCtx);
//	// Free resources
//	free(ctx->path);
//	free(ctx);
//}
//
///*
//unsigned __stdcall WorkerThread(PVOID arg) {
//PFINDFILESCTX pctx = (PFINDFILESCTX)arg;
//PQUEUE pqueue = pctx->pqueue;
//
//for (;;) {
//PQUEUE_ENTRY pentry = QueueGet(pqueue);
//FindFiles(pentry->path, pctx->fileMatch, pctx->dp, pctx->fp, pctx->ctx, pctx);
//
//free(pentry->path);
//free(pentry);
//
//if (InterlockedAdd((LONG*)&pctx->nestedCounter, -1) == 0) {
//printf("Force terminate\n");
//}
//
//
//}
//
//return 0;
//}
//*/
//
//VOID FindFilesPar(TCHAR *path,
//	TCHAR *fileMatch,
//	DirectoryProcessor dp,
//	FileProcessor fp,
//	LPVOID ctx) {
//
//	// Cria coleção
//	// Prepara contexto (do serviço)
//	// Cria pool de threads (workerThreads) passando-lhe o contexto do serviço
//	// Coloca as workerThreads a aguardarem trabalho
//	// Nested igual a 1
//	// Chama a implementação do FindFiles (que permite ser chamada recursivamente)
//	PTHREAD_POOL ppool = ThreadPoolCreate(0, 0);
//	HANDLE finishEvt = CreateEvent(NULL, true, false, NULL);
//	FINDFILESCTX ffctx = { fileMatch, dp, fp, ctx, 1, ppool, finishEvt, 0 };
//
//	// The main thread will be another worker thread too.
//	FindFiles(path, fileMatch, dp, fp, ctx, &ffctx);
//
//	WaitForSingleObject(finishEvt, INFINITE);
//
//}

/*-----------------------------------------------------------------------
This function allows the processing of a selected set of files in a directory tree
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

INT main(DWORD argc, PCHAR argv[]) {
	PCHAR name;
	PCHAR path;
	DWORD res;
	CHAR pathname[MAX_CHARS];

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

	Entry entry;
	while (1) {
		res = SearchGet(service, &entry);
		if (res == FALSE)
			break;

		processEntry(path, &entry);
	}
	//for (int i = 0; i < MAX_SERVERS; i++) {
	//	threads[i] = (HANDLE)_beginthreadex(NULL, 0, server_thread, (LPVOID)i, 0, NULL);
	//}

	//res = WaitForMultipleObjects(MAX_SERVERS, threads, TRUE, INFINITE);
	printf("Server app: Close service name = %s and exit\n", name);
	SearchClose(service);

	return 0;
}
