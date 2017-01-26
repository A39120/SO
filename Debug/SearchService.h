#ifndef SEARCH_SERVICE_H
#define SEARCH_SERVICE_H

#include <Windows.h>
#include <tchar.h>

#ifdef DLL_SEARCH_SERVICE_EXPORTS
#define SEARCH_SERVICE_API __declspec(dllexport)
#else
#define SEARCH_SERVICE_API __declspec(dllimport)
#endif

typedef struct {
	HANDLE accessMtx;
	HANDLE itemsAvailableSem;
	HANDLE spaceAvailableSem;
	HANDLE sharedMap;
	LPVOID sharedMem;
	HANDLE answSpaceSem;
	HANDLE stopServiceEvt;
} SearchService, *PSearchService;

#define MAX_CHARS	256 * 10

typedef struct {
	CHAR   value[MAX_CHARS];
	HANDLE answReadyEvt;
	DWORD  answIdx;
} Entry, * PEntry;

#define MAX_ENTRIES	100
typedef struct {
	DWORD serverProcId;
	DWORD get;
	DWORD put;
	DWORD count;
	Entry entries[MAX_ENTRIES];
	DWORD answBitMsk;
	CHAR  answers[MAX_ENTRIES][MAX_CHARS];
} SharedBlock, *PSharedBlock;

#ifdef __cplusplus
extern "C" {
#endif

#ifdef UNICODE
#define SearchCreate SearchCreateW
#define SearchOpen SearchOpenW
#else
#define SearchCreate SearchCreateA
#define SearchOpen SearchOpenA
#endif

	// Create version for servers and Open version for clients
	// ASCIICharacterSystem
//	SEARCH_SERVICE_API SearchService * SearchCreateA(PCHAR name, PCHAR dir);
	SEARCH_SERVICE_API SearchService * SearchCreateA(PCHAR name);
	SEARCH_SERVICE_API SearchService * SearchOpenA(PCHAR name);
	// UnicodeCharacterSystem
//	SEARCH_SERVICE_API SearchService * SearchCreateW(PWCHAR name, PWCHAR dir);
	SEARCH_SERVICE_API SearchService * SearchCreateW(PWCHAR name);
	SEARCH_SERVICE_API SearchService * SearchOpenW(PWCHAR name);

	// Called by servers
	SEARCH_SERVICE_API BOOL	 SearchPut(PSearchService service, PEntry entry);
	// Called by clients
	SEARCH_SERVICE_API BOOL	 SearchGet(PSearchService service, PEntry entry);
	SEARCH_SERVICE_API BOOL	 SearchWaitFor(PSearchService service, PEntry entry, CHAR *result, DWORD cap);
	// Called by clients and servers
	SEARCH_SERVICE_API VOID	 SearchClose(PSearchService service);
	// Called by manager to stop service
	SEARCH_SERVICE_API VOID	 SearchStop(PSearchService service);


#ifdef __cplusplus
}
#endif
#endif/*SEARCH_SERVICE_H*/