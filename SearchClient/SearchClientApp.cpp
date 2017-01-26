#include <Windows.h>
#include <process.h>
#include <stdio.h>
#include <assert.h>
#include "SearchService.h"

static PSearchService service;

//UINT __stdcall client_thread(LPVOID arg) {
//	const DWORD size = sizeof(numbers) / sizeof(*numbers);
//	INT res, i;
//	DWORD result, id = (DWORD)arg;
//	Entry entry;
//
//	printf("Client %d: start\n", id);
//
//	for (i = 0; i < size; i++) {
//		entry.value = numbers[i];
//		res = SearchPut(service, &entry);
//		if (res == FALSE) break;
//		res = SearchWaitFor(service, &entry, &result);
//		if (res == FALSE) break;
//		
//		printf("Client %d: factorial(%d) = %d\n", id, entry.value, result);
//	}
//
//	if (i < size)
//		printf("Client %d: the service was stopped prematurely\n");
//	
//	printf("Client %d: end\n", id);
//
//	return 0;
//}

INT main(DWORD argc, PCHAR argv[]) {
	PCHAR name;
	PCHAR sentence;
//	HANDLE threads[MAX_CLIENTS];
//	DWORD res;

	if (argc < 3) {
		printf("Use > %s <service_name> <sentence to search>\n", argv[0]);
		name = "Service1";
		sentence = "#include \"SearchService.h\"";
		printf("Using > %s %s \"%s\"\n", argv[0], name, sentence);
	}
	else {
		name = argv[1];
		sentence = argv[2];
	}

	// open service and put search entry
	printf("Client app: Open service name = %s\n", name);
	service = SearchOpen(name); assert(service != NULL);
	Entry entry;
	strcpy_s(entry.value, MAX_CHARS, sentence);
	SearchPut(service, &entry);

	// do other stufs here

	// no more things to do. Wait for and show answer.
	DWORD resultCap = MAX_CHARS;
	CHAR result[MAX_CHARS];
	SearchWaitFor(service, &entry, result, resultCap);
	printf("Client app: result:\n%s\n", result);
	
	printf("Client app: Close service name = %s\n", name);
	SearchClose(service);

	return 0;
}