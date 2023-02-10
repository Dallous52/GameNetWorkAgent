#include "TcpSock.h"
#include "Error.h"
#include "Crc32.h"

Connection::Connection()
{
}


Connection::~Connection()
{
}


void Connection::GetOne()
{
	bufptr = new char[_PKG_MAX_LENGTH];
	throwSendCount = 0;
	events = 0;

	floodCount = 0;
	floodCheckLast = 0;
	requestSent = 0;
	recvlen = 0;
	port = 0;
	connectId.clear();
	admin.clear();

	MsgInfo = NULL;
	sendptr = NULL;
	delptr = NULL;
	forwardConn = NULL;
}


void Connection::FreeOne()
{
	if (MsgInfo != NULL)
	{
		MsgInfo = NULL;
	}

    if (delptr != NULL)
	{
		delete[]delptr;
		delptr = NULL;
		sendptr = NULL;
	}

	throwSendCount = 0;
}


void MSocket::InitConnections()
{
	pConnection pConn;

	for (int i = 0; i < m_work_connections; i++)
	{
		pConn = new Connection;  

		pConn->GetOne();

		m_connection.push_back(pConn);
		m_fconnection.push_back(pConn);
	}

	m_connection_all = m_connection_free = (int)m_connection.size();
	
	return;
}


pConnection MSocket::Get_connection(int sock)
{
	lock_guard<mutex> lock(m_connectMutex);

	pConnection auxptr;

	//如果空闲链表中有链接
	if (!m_fconnection.empty())
	{
		auxptr = m_fconnection.front();
		m_fconnection.pop_front();
		--m_connection_free;

		auxptr->GetOne();
		auxptr->fd = sock;

		return auxptr;
	}

	//如果空闲链表中没有链接
	//创建一个返回

	auxptr = new Connection;

	memset(auxptr, 0, sizeof(Connection));
	auxptr->GetOne();
	m_connection.push_back(auxptr);
	++m_connection_all;

	auxptr->fd = sock;

	return auxptr;
}


bool MSocket::TestFlood(pConnection cptr)
{
	struct timeval sCurrent;
	uint64_t msCurrent;
	bool isFlood = false;

	//获取当前时间结构
	gettimeofday(&sCurrent, NULL);
	//时间结构转换为ms
	msCurrent = sCurrent.tv_sec * 1000 + sCurrent.tv_usec / 1000;

	if (msCurrent - cptr->floodCheckLast < m_floodInterval)
	{
		cptr->floodCount++;
		cptr->floodCheckLast = msCurrent;
	}
	else
	{
		cptr->floodCount = 0;
		cptr->floodCheckLast = msCurrent;
	}

	if (cptr->floodCount > m_floodPackages)
	{
		isFlood = true;
	}

	return isFlood;
}


void MSocket::ActiveShutdown(pConnection cptr)
{
	if (cptr->port == m_logicPort)
	{
		//关闭打开的udp tcp端口
		udpsock.Close_Sockets(cptr->admin);
		udpsock.RegiestInfo_Delete(cptr->admin);
		Close_Sockets(cptr->admin);
	}

	if (cptr->port != m_logicPort && cptr->forwardConn != NULL)
	{
		ActiveShutdown(cptr->forwardConn);
		cptr->forwardConn = NULL;
	}

	if (cptr->fd != -1 && close(cptr->fd) == -1)
	{
		Error_insert_File(LOG_URGENT, "Close accept connection:> socket close failed.");
	}

	cptr->fd = -1;

	if (cptr->throwSendCount > 0)
	{
		cptr->throwSendCount = 0;
	}

	Free_connection(cptr);

	return;
}


void MSocket::Free_connection(pConnection cptr)
{
	lock_guard<mutex> lock(m_connectMutex);

	cptr->FreeOne();

	if (cptr->port != m_forwardPort && !cptr->connectId.empty())
	{
		m_workconn.erase(cptr->connectId);
	}

	m_fconnection.push_back(cptr);

	//空闲节点数+1
	++m_connection_free;
	--m_connection_work;

	return;
}


void MSocket::ClearConnections()
{
	pConnection paux;

	while (!m_connection.empty())
	{
		paux = m_connection.front();
		m_connection.pop_front();

		paux->~Connection();

		delete paux;
	}

	return;
}