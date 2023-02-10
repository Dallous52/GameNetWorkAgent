#include "Process.h"
#include "Crc32.h"
#include "Error.h"


void ProcessCtl::UserWork_Cycle(void* threadData)
{
	ProcessCtl* pctl = static_cast<ProcessCtl*>(threadData);

	if (!pctl->UserThread_Init())
	{
		exit(-2);
	}

	while (true)
	{
		udpsock.epoll_process_events(-1);
	}

	udpsock.WrokProcRelevent_Off();
}


bool ProcessCtl::UserThread_Init()
{
	if (!udpsock.WrokProcRelevent_Init())
	{
		Error_insert_File(LOG_URGENT, "User work thread initialize failed.");
		return false;
	}

	return true;
}