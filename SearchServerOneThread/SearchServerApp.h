#pragma once


#define MAX_SERVERS 4
#define MAX_FILES 4


typedef struct {
	PCHAR path;	//server path
	DWORD i;	//server number, not used
}PROCESS_CONTEXT, *PPROCESS_CONTEXT;

typedef struct {
	CHAR name[MAX_CHARS];	//name of file
	HANDLE hFile;			//file handle
	CHAR lockName[MAX_CHARS];//lock id
	HANDLE fileLock;		//file mutex, can be replaced for a critical section/event
}FILE_NODE, *PFILE_NODE;

typedef struct {
	PFILE_NODE list;	//List of files, their handles and their access lock
	DWORD numberOfFiles;//Number of files in the list
	CRITICAL_SECTION cs;//Critical section to access the list
}FILE_LIST, *PFILE_LIST;

typedef struct {
	CHAR file[MAX_CHARS];
	CHAR searchId[MAX_CHARS];
	DWORD tlsId;
}REQUEST_NODE, *PREQUEST_NODE;

typedef struct {
	PREQUEST_NODE files;//Requests 	
	HANDLE emptySem;	//Blocks if PFILE_NODE is empty
	HANDLE fullSem;		//Blocks if PFILE_NODE is full
	DWORD get;
	DWORD put;
	//Entry entry;		//entry from SearchService/client
}REQUEST_LIST, *PREQUEST_LIST;

typedef struct {
	Entry entry;
	DWORD fileCount;
	HANDLE beginEvt;
	HANDLE endEvt;
	//CRITICAL_SECTION count_iterator; //replaced by interlock increment
}THREAD_LOCAL, *PTHREAD_LOCAL;