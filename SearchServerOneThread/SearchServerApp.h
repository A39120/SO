#pragma once

#define MAX_SERVERS 4
#define MAX_FILES 4
#define HANDLE_HAS_NOT_BEEN_SET -2
typedef struct {
	PCHAR path;	//server path
	DWORD i;	//server number, not used
}PROCESS_CONTEXT, *PPROCESS_CONTEXT;

typedef struct {
	CHAR file[MAX_CHARS];
	PEntry entry;
	BOOL completed;
	HANDLE answEvt;	//Avoid lost updates on answer
	HANDLE threadEvt;
	HANDLE thread;
}REQUEST, *PREQUEST;

typedef struct {
	PREQUEST files;		//Requests 	
	HANDLE emptySem;	//Blocks if files is empty
	HANDLE fullSem;		//Blocks if files is full
	DWORD size;
	CRITICAL_SECTION request_section;
}REQUEST_LIST, *PREQUEST_LIST;

typedef struct {
	CHAR file[MAX_CHARS];
	HANDLE mutex;
}FILE_LOCK, *PFILE_LOCK;

typedef struct {
	PFILE_LOCK lock;
	DWORD count;
	CRITICAL_SECTION file_register;
}FILE_LOCK_LIST, *PFILE_LOCK_LIST;

