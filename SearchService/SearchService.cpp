#include <Windows.h>
#include <assert.h>
#include <malloc.h>
#include <stdio.h>
#include <intrin.h>

#include "SearchService.h"

SearchService * SearchCreateA(PCHAR name) {
	const DWORD len_name = strlen(name);
	WCHAR new_name[MAX_CHARS];
	assert(len_name < MAX_CHARS);
	DWORD new_len_name = MultiByteToWideChar(CP_ACP, 0, name, len_name, new_name, _countof(new_name));
	new_name[new_len_name] = L'\0';
	SearchService * service = SearchCreateW(new_name);
	return service;
}
SearchService * SearchOpenA(PCHAR name) {
	const DWORD len = strlen(name);
	WCHAR new_name[MAX_CHARS];
	assert(len < MAX_CHARS);
	DWORD new_len = MultiByteToWideChar(CP_ACP, 0, name, len, new_name, _countof(new_name));
	new_name[new_len] = L'\0';
	SearchService * service = SearchOpenW(new_name);
	return service;
}
SearchService * SearchCreateW(PWCHAR name) {
//SearchService * SearchCreateW(PWCHAR name, PWCHAR dir) {
	PSearchService service = (PSearchService)malloc(sizeof(SearchService));
	WCHAR tmp[MAX_CHARS];
	HANDLE hd;
	_snwprintf_s(tmp, _countof(tmp), L"%sSearchSharedBlockMap", name);
	hd = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(SharedBlock), tmp);
	assert(hd != NULL);
	service->sharedMap = hd;

	hd = (HANDLE)MapViewOfFile(hd, FILE_MAP_ALL_ACCESS, 0, 0, 0);
	assert(hd != NULL);
	service->sharedMem = hd;
	PSharedBlock block = (PSharedBlock)hd;
	if (block->serverProcId != 0) {
		// There are another server running. Return NULL.
		UnmapViewOfFile(service->sharedMem);
		CloseHandle(service->sharedMap);
		return NULL;
	}

	_snwprintf_s(tmp, _countof(tmp), L"%sSearchAccessMtx", name);
	hd = CreateMutexW(NULL, FALSE, tmp);
	assert(hd != NULL);
	service->accessMtx = hd;

	_snwprintf_s(tmp, _countof(tmp), L"%sSearchItemsAvailableSem", name);
	hd = CreateSemaphoreW(NULL, 0, MAX_ENTRIES, tmp);
	assert(hd != NULL);
	service->itemsAvailableSem = hd;

	_snwprintf_s(tmp, _countof(tmp), L"%sSearchSpaceAvailableSem", name);
	hd = CreateSemaphoreW(NULL, MAX_ENTRIES, MAX_ENTRIES, tmp);
	assert(hd != NULL);
	service->spaceAvailableSem = hd;

	_snwprintf_s(tmp, _countof(tmp), L"%sSearchAnswSpaceAvailableSem", name);
	hd = CreateSemaphoreW(NULL, MAX_ENTRIES, MAX_ENTRIES, tmp);
	assert(hd != NULL);
	service->answSpaceSem = hd;

	_snwprintf_s(tmp, _countof(tmp), L"%sSearchStopServiceEvt", name);
	hd = CreateEventW(NULL, TRUE, FALSE, tmp); assert(hd != NULL);
	service->stopServiceEvt = hd;

	// Initiate shared block
	block->serverProcId = GetCurrentProcessId();
	block->get = block->put = 0;
	block->answBitMsk = 0;
	return service;
}
SearchService * SearchOpenW(PWCHAR name) {
	PSearchService service = (PSearchService)malloc(sizeof(SearchService));
	WCHAR tmp[100];
	HANDLE hd;
	_snwprintf_s(tmp, _countof(tmp), L"%sSearchSharedBlockMap", name);
	hd = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, tmp); assert(hd != NULL);
	service->sharedMap = hd;

	hd = (HANDLE)MapViewOfFile(hd, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBlock));
	if (hd == NULL)
		printf("GetLastError = %d\n", GetLastError());
	assert(hd != NULL);
	service->sharedMem = hd;
	PSharedBlock block = (PSharedBlock)hd;
	if (block->serverProcId == 0) {
		// There are no server running. Return NULL.
		UnmapViewOfFile(service->sharedMem);
		CloseHandle(service->sharedMap);
		return NULL;
	}

	_snwprintf_s(tmp, _countof(tmp), L"%sSearchAccessMtx", name);
	hd = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, tmp); assert(hd != NULL);
	service->accessMtx = hd;

	_snwprintf_s(tmp, _countof(tmp), L"%sSearchItemsAvailableSem", name);
	hd = OpenSemaphoreW(SEMAPHORE_ALL_ACCESS, FALSE, tmp); assert(hd != NULL);
	service->itemsAvailableSem = hd;

	_snwprintf_s(tmp, _countof(tmp), L"%sSearchSpaceAvailableSem", name);
	hd = OpenSemaphoreW(SEMAPHORE_ALL_ACCESS, FALSE, tmp); assert(hd != NULL);
	service->spaceAvailableSem = hd;

	_snwprintf_s(tmp, _countof(tmp), L"%sSearchAnswSpaceAvailableSem", name);
	hd = OpenSemaphoreW(SEMAPHORE_ALL_ACCESS, FALSE, tmp); assert(hd != NULL);
	service->answSpaceSem = hd;

	_snwprintf_s(tmp, _countof(tmp), L"%sSearchStopServiceEvt", name);
	hd = OpenEventW(EVENT_ALL_ACCESS, FALSE, tmp); assert(hd != NULL);
	service->stopServiceEvt = hd;

	return service;

}

/* No thread safe */
static DWORD get_free_answ_entry(DWORD bitmask) {
	DWORD idx;
	DWORD res;
	bitmask = ~bitmask; // Invert bit value meaning: 1 busy -> 0; 0 free -> 1
	res = _BitScanForward(&idx, bitmask); assert(res == TRUE);
	return idx;
}
/* No thread safe */
static VOID set_free_answ_entry(DWORD *bitmask, int idx) {
	DWORD val = *bitmask;
	val = val & (~(1 << idx));
	*bitmask = val;
}
/* No thread safe */
static VOID set_busy_answ_entry(DWORD *bitmask, int idx) {
	DWORD val = *bitmask;
	val = val | (1 << idx);
	*bitmask = val;
}
BOOL SearchPut(PSearchService service, PEntry entry) {
	DWORD res;
	HANDLE hh[] = { service->spaceAvailableSem, service->stopServiceEvt };

	PSharedBlock block = (PSharedBlock)service->sharedMem;
	if (block->serverProcId == 0) return FALSE;

	// Wait for space to put item
	res = WaitForMultipleObjects(2, hh, FALSE, INFINITE); assert(res != WAIT_FAILED);
	if (res - WAIT_OBJECT_0 == 1) return FALSE;
	hh[0] = service->answSpaceSem; hh[1] = service->stopServiceEvt;
	// Wait for space to answer
	res = WaitForMultipleObjects(2, hh, FALSE, INFINITE); assert(res != WAIT_FAILED);
	if (res - WAIT_OBJECT_0 == 1) return FALSE;
	hh[0] = service->accessMtx; hh[1] = service->stopServiceEvt;
	// Enter critical section
	res = WaitForMultipleObjects(2, hh, FALSE, INFINITE); assert(res != WAIT_FAILED);
	if (res - WAIT_OBJECT_0 == 1) return FALSE;

	// Allocate answer entry for result
	DWORD answBitMsk = block->answBitMsk;
	DWORD idx = get_free_answ_entry(answBitMsk);
	set_busy_answ_entry(&block->answBitMsk, idx);
	entry->answIdx = idx;
	// Create Event to get answer ready notification (may be outside of the critical section)
	HANDLE evt = CreateEvent(NULL, FALSE, FALSE, NULL); assert(evt != NULL);
	entry->answReadyEvt = evt;

	// Allocate entry from ringbuffer
	DWORD put = block->put;
	PEntry sharedEntry = &block->entries[put];
	put = (put + 1) % MAX_ENTRIES;
	block->put = put;
	block->count += 1;

	// Initiate shared entry
	strcpy_s(sharedEntry->value, MAX_CHARS, entry->value);
	sharedEntry->answIdx = idx;
	// Duplicate event handle to server
	DWORD procId = block->serverProcId;
	HANDLE serverEvt;
	HANDLE serverProc = OpenProcess(PROCESS_DUP_HANDLE, FALSE, procId); assert(serverProc != NULL);
	res = DuplicateHandle(GetCurrentProcess(), evt, serverProc, &serverEvt, DUPLICATE_SAME_ACCESS, FALSE, 0); assert(serverEvt != NULL);
	sharedEntry->answReadyEvt = serverEvt;

	// Leave critical section
	res = ReleaseMutex(service->accessMtx); assert(res == TRUE);

	// Signal item available to server
	res = ReleaseSemaphore(service->itemsAvailableSem, 1, NULL); assert(res == TRUE);

	return TRUE;
}
BOOL SearchGet(PSearchService service, PEntry entry) {
	DWORD res;

	PSharedBlock block = (PSharedBlock)service->sharedMem;
	HANDLE hh[] = { service->itemsAvailableSem, service->stopServiceEvt };
	// Wait for item available 
	res = WaitForMultipleObjects(2, hh, FALSE, INFINITE); assert(res != WAIT_FAILED);
	//if (block->count == 0 && res - WAIT_OBJECT_0 == 1) return FALSE;
	if (res - WAIT_OBJECT_0 == 1) return FALSE;

	hh[0] = service->accessMtx; hh[1] = service->stopServiceEvt;
	// Enter critical section
	res = WaitForMultipleObjects(2, hh, FALSE, INFINITE); assert(res != WAIT_FAILED);
	//if (block->count == 0 && res - WAIT_OBJECT_0 == 1) return FALSE;
	if (res - WAIT_OBJECT_0 == 1) return FALSE;
	
	// Free ringbuffer entry
	DWORD get = block->get;
	PEntry sharedEntry = &block->entries[get];
	get = (get + 1) % MAX_ENTRIES;
	block->get = get;
	block->count -= 1;

	// Copy ringbuffer entry to entry argument
	memcpy_s(entry, sizeof(Entry), sharedEntry, sizeof(Entry));

	// Leave critical section
	res = ReleaseMutex(service->accessMtx); assert(res == TRUE);

	// Release unit to ringbuffer space available
	res = ReleaseSemaphore(service->spaceAvailableSem, 1, NULL); assert(res == TRUE);
	return TRUE;
}
BOOL SearchWaitFor(PSearchService service, PEntry entry, CHAR *result, DWORD cap) {
	DWORD res;

	HANDLE hh[] = { entry->answReadyEvt, service->stopServiceEvt };
	// Waits for answer ready
	res = WaitForMultipleObjects(2, hh, FALSE, INFINITE); assert(res != WAIT_FAILED);
	if (res - WAIT_OBJECT_0 == 1) return FALSE;

	// Copy result to local variable
	PSharedBlock block = (PSharedBlock)service->sharedMem;
	DWORD idx = entry->answIdx;
	strcpy_s(result, cap, block->answers[idx]);

	hh[0] = service->accessMtx; hh[1] = service->stopServiceEvt;
	// Enter critical section
	res = WaitForMultipleObjects(2, hh, FALSE, INFINITE); assert(res != WAIT_FAILED);
	if (res - WAIT_OBJECT_0 == 1) return FALSE;
	// Free answer entry
	set_free_answ_entry(&block->answBitMsk, idx);
	// Leave critical section
	ReleaseMutex(service->accessMtx);
	// Release unit to answer space semaphore
	ReleaseSemaphore(service->answSpaceSem, 1, NULL);
	// Free answer ready event object
	CloseHandle(entry->answReadyEvt);

	//*result = answer;
	return TRUE;
}
VOID SearchClose(PSearchService service) {
	UnmapViewOfFile(service->sharedMem);
	CloseHandle(service->sharedMap);
	CloseHandle(service->answSpaceSem);
	CloseHandle(service->itemsAvailableSem);
	CloseHandle(service->accessMtx);
	CloseHandle(service->spaceAvailableSem);
	CloseHandle(service->stopServiceEvt);

	free(service);
}

VOID SearchStop(PSearchService service) {
	DWORD res;
	PSharedBlock block = (PSharedBlock)service->sharedMem;
	if (block->serverProcId == 0) return;

	res = WaitForSingleObject(service->accessMtx, INFINITE); assert(res != WAIT_FAILED);
	block->serverProcId = 0;
	SetEvent(service->stopServiceEvt);
	ReleaseMutex(service->accessMtx);
}
