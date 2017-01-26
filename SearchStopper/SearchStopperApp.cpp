#include <Windows.h>
#include <stdio.h>
#include <assert.h>
#include "SearchService.h"

DWORD main(DWORD argc, PCHAR argv[]) {
	PCHAR name;

	if (argc < 2) {
		printf("Use > %s <service name>\n", argv[0]);
		name = "Service1";
	}
	else name = argv[1];
	printf("Stopper app: Open service name = %s\n", name);
	SearchService * service = SearchOpen(name); assert(service != NULL);

	SearchStop(service);
	printf("Stopper app: Close service name = %s\n", name);
	SearchClose(service);

	return 0;
}