#include "UdpSock.h"
#include "Error.h"

void uConnection::GetOne()
{
	bufptr = new char[_PKG_MAX_LENGTH];
	events = 0;
	msg_id = 0;
}


void uConnection::FreeOne()
{
	if (bufptr != NULL)
	{
		delete[]bufptr;
		bufptr = NULL;
	}
}


void WorkSock::InitConnections()
{
	pConn_u pConn;

	//分配空间创建连接池
	for (int i = 0; i < m_work_connections; i++)
	{
		pConn = new uConn;

		pConn->GetOne();

		m_connection.push_back(pConn);
		m_fconnection.push_back(pConn);
	}

	m_connection_all = m_connection_free = (int)m_connection.size();

	return;
}


pConn_u WorkSock::Get_connection(int sock)
{
	std::lock_guard<std::mutex> lock(m_connectMutex);

	pConn_u auxptr;

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

	auxptr = new uConn;

	memset(auxptr, 0, sizeof(uConn));
	auxptr->GetOne();
	m_connection.push_back(auxptr);
	++m_connection_all;

	auxptr->fd = sock;

	return auxptr;
}


void WorkSock::UselessDelete_Thread(void* threadData)
{
	//获取类指针
	WorkSock* psock = static_cast<WorkSock*>(threadData);

	time_t currtime;
	pConn_u cptr;
	string tmpip;
	vector<RegistInfo*>::iterator it;

	while (!IsExit)
	{
		psock->m_idCheckLock.lock();

		currtime = time(NULL);
		for (it = psock->m_timeCheckQueue.begin(); it != psock->m_timeCheckQueue.end(); it++)
		{
			if (currtime - (*it)->timed > 120)
			{
				cptr = psock->m_workconn[(*it)->port];
				tmpip = cptr->auxinfo[(*it)->msgid];
				cptr->csinfo.erase(tmpip);
				cptr->auxinfo.erase((*it)->msgid);
				delete (*it);
				psock->m_timeCheckQueue.erase(it);

				it--;
			}
		}

		psock->m_idCheckLock.unlock();

		sleep(120);
	}
}


void WorkSock::Free_connection(pConn_u cptr)
{
	lock_guard<mutex> lock(m_connectMutex);

	cptr->FreeOne();

	m_workconn.erase(cptr->port);

	m_fconnection.push_back(cptr);

	//空闲节点数+1
	++m_connection_free;

	Error_insert_File(LOG_NOTICE, "udp port %d is closed.", cptr->port);

	return;
}


void WorkSock::ClearConnections()
{
	pConn_u paux;

	while (!m_connection.empty())
	{
		paux = m_connection.front();
		m_connection.pop_front();

		paux->~uConnection();

		delete paux;
	}

	return;
}