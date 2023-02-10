#include "UdpSock.h"
#include "Manage.h"
#include "Error.h"
#include "Process.h"

string tmpIp;
uint32_t addrsize = sizeof(sockaddr_in);

RegistInfo* WorkSock::Read_requestHandle(pConn_u cptr)
{
	RegistInfo* caddr = new RegistInfo;
	memset(&(caddr->addr), 0, addrsize);
	caddr->timed = 0;
	caddr->port = cptr->port;

	cptr->recvLen = recvfrom(cptr->fd, cptr->bufptr, _PKG_MAX_LENGTH, 0,
		(struct sockaddr*)&(caddr->addr), &addrsize);
	//recv系统函数用于接收socket数据

	if (cptr->recvLen == 0)
	{
		//客户端正常关闭，回收连接
		Error_insert_File(LOG_NOTICE, "The client shuts down normally.");

		ActiveShutdown(cptr);
		delete caddr;

		return NULL;
	}

	//错误处理
	if (cptr->recvLen < 0)
	{
		if (errno == EINTR)
		{
			Error_insert_File(LOG_WARN, "receive data:> Signal reception interrupt.");
			delete caddr;

			return NULL;
		}

		if (errno == ENOBUFS)
		{
			Error_insert_File(LOG_WARN, "receive data:> Insufficient receive buffer memoty.");
			delete caddr;

			return NULL;
		}

		if (errno == ECONNRESET)
		{
			//对等放重置连接
			//由用户强制关闭导致的正常情况
			//do nothing
		}
		else
		{
			Error_insert_File(LOG_SERIOUS, "receive data:> %s", strerror(errno));
		}

		Error_insert_File(LOG_NOTICE, "The client abnormal shutdow.");

		ActiveShutdown(cptr);
		delete caddr;

		return NULL;
	}//end if

	return caddr;
}


void WorkSock::UserRequest_Handle(pConn_u cptr)
{
	uint32_t tmpid;
	RegistInfo* caddr;

	//读取socket缓冲区数据
	caddr = Read_requestHandle(cptr);
	if (caddr == NULL)
	{
		return;
	}

	tmpIp = inet_ntoa(caddr->addr.sin_addr);
	tmpIp += ":" + to_string(ntohs(caddr->addr.sin_port));

	m_idCheckLock.lock();
	if (cptr->csinfo.find(tmpIp) != cptr->csinfo.end())
	{
		delete caddr;

		//使用次数增加
		cptr->csinfo[tmpIp]->timed = time(NULL);

		//获取发送消息id
		tmpid = cptr->csinfo[tmpIp]->msgid;

	}
	else
	{
		cptr->msg_id = cptr->msg_id > MAX_ID_NUM ? 0 : cptr->msg_id;
		caddr->timed = time(NULL);

		//分配Id
		cptr->auxinfo[cptr->msg_id] = tmpIp;
		caddr->msgid = cptr->msg_id;
		cptr->csinfo[tmpIp] = caddr;
		m_timeCheckQueue.emplace_back(caddr);

		//获取发送消息id
		tmpid = cptr->msg_id;

		cptr->msg_id++;
	}
	m_idCheckLock.unlock();


	//将信息包放入转发队列中并处理
	Packaging_Send(cptr->bufptr, cptr->recvLen, cptr->admin_id, tmpid, cptr->port);

	return;
}


void WorkSock::ForwardRequest_Handle(pConn_u cptr)
{
	RegistInfo* caddr;

	caddr = Read_requestHandle(cptr);
	if (caddr == NULL)
	{
		return;
	}

	string tmpIp = inet_ntoa(caddr->addr.sin_addr);

	m_heartLock.lock();
	if (cptr->csinfo.find(tmpIp) != cptr->csinfo.end())
	{
		//更新时间
		m_now = chrono::system_clock::now();
		m_ms = std::chrono::duration_cast<std::chrono::milliseconds>(m_now.time_since_epoch());
		cptr->csinfo[tmpIp]->timed = m_ms.count();

		delete caddr;
	}
	else
	{
		cptr->msg_id = cptr->msg_id > MAX_ID_NUM ? 0 : cptr->msg_id;

		//分配Id
		cptr->auxinfo[cptr->msg_id] = tmpIp;
		caddr->msgid = cptr->msg_id;
		cptr->csinfo[tmpIp] = caddr;

		cptr->msg_id++;

		//时间赋值

		m_now = chrono::system_clock::now();
		m_ms = std::chrono::duration_cast<std::chrono::milliseconds>(m_now.time_since_epoch());
		caddr->timed = m_ms.count();
		m_heartQueue.emplace_back(caddr);
		m_heartLock.unlock();

		Error_insert_File(LOG_INFO, "Client %s registration succeeded.", tmpIp.c_str());
		return;
	}
	m_heartLock.unlock();

	PKG_Head* pPkgHead;
	pPkgHead = (PKG_Head*)cptr->bufptr;

	unsigned short pkgLen = ntohs(pPkgHead->pkgLen);
	//ntohs 将2字节的网络字节序转换为主机字节须

	//恶意包/错误包处理
	if (cptr->recvLen != (long)pkgLen)
	{
		cptr->recvLen = 0;
		return;
	}

	//为信息包分配内存 包头 + 包体
	char* auxBuffer = new char[pkgLen];

	//填写 包头 + 包体
	memcpy(auxBuffer, cptr->bufptr, pkgLen);

	//放入用户接收队列发送
	MsgSend(auxBuffer);

	return;
}


void WorkSock::HeartPackage_Check_Send(void* threadData)
{
	WorkSock* psock = static_cast<WorkSock*>(threadData);

	pConn_u cptr;
	vector<RegistInfo*>::iterator it;
	int64_t currtime;
	int64_t result;
	uint32_t addrsize = sizeof(sockaddr_in);

	while (!IsExit)
	{
		psock->m_heartLock.lock();

		psock->m_now = chrono::system_clock::now();
		psock->m_ms = std::chrono::duration_cast<std::chrono::milliseconds>
			(psock->m_now.time_since_epoch());

		currtime = psock->m_ms.count();

		for (it = psock->m_heartQueue.begin(); it != psock->m_heartQueue.end(); it++)
		{
			if ((*it)->timed == 0)
			{
				//删除已不存在注册信息
				delete (*it);
				psock->m_heartQueue.erase(it);

				it--;
				continue;
			}

			result = currtime - (*it)->timed;
			if (result >= 500)
			{
				cptr = psock->m_workconn[psock->m_ForwardPort];
				sendto(cptr->fd, NULL, 0, 0, (sockaddr*)&((*it)->addr), addrsize);
			}
		}

		psock->m_heartLock.unlock();

		usleep(500000);
	}
}


void WorkSock::Packaging_Send(char* buffer, size_t recvLen, string admin_id, uint32_t msgid, uint32_t port)
{
	size_t tmpsize = admin_id.size() + 1;
	char* sendBuf = new char[m_PKG_Hlen + tmpsize + recvLen];

	//拷贝消息头
	strcpy(sendBuf, admin_id.c_str());

	PKG_Head* pkghdr = (PKG_Head*)(sendBuf + tmpsize);

	//填充包头
	pkghdr->mesgCode = htonl(msgid);
	pkghdr->pkgLen = htons(m_PKG_Hlen + recvLen);

	//填充包体
	char* sendInfo = sendBuf + tmpsize + m_PKG_Hlen;

	memcpy(sendInfo, buffer, recvLen);

	//计算crc值
	pkghdr->crc32 = htonl(port);

	MsgForward(sendBuf);
}


void WorkSock::MsgForward(char* sendBuf)
{
	//防止滞留发送数据包过多
	m_forwardMsgMutex.lock();

	if (m_forwardQueue.size() > m_MAX_Send)
	{
		delete[]sendBuf;
		m_forwardMsgMutex.unlock();

		return;
	}

	//消息放入消息队列
	m_forwardQueue.push_back(sendBuf);

	m_forwardMsgMutex.unlock();

	//将信号量的值加一,让卡在sem_wait的线程继续执行
	m_forwardCond.notify_one();

	return;
}


void WorkSock::MsgSend(char* sendBuf)
{
	//防止滞留发送数据包过多
	m_sendMsgMutex.lock();

	if (m_sendQueue.size() > m_MAX_Send)
	{
		delete[]sendBuf;
		m_sendMsgMutex.unlock();

		return;
	}

	//消息放入消息队列
	m_sendQueue.push_back(sendBuf);

	m_sendMsgMutex.unlock();

	//将信号量的值加一,让卡在sem_wait的线程继续执行
	m_SendCond.notify_one();

	return;
}



void* WorkSock::MessageSend_Thread(void* threadData)
{
	//获取类指针
	WorkSock* psock = static_cast<WorkSock*>(threadData);

	char* pMsgbuf;	//用于接收发送队列成员

	PKG_Head* pkghdr;
	char* sendbuf;
	size_t sendsize;
	pConn_u	cptr;
	string tmpip;
	sockaddr_in* caddr;
	RegistInfo* regist;
	uint32_t addrsize = sizeof(sockaddr_in);

	while (!IsExit)
	{
		unique_lock<mutex>	slock(psock->m_sendMsgMutex);
		//使用条件变量量让线程等待
		while (psock->m_sendQueue.empty() && !IsExit)
		{
			psock->m_SendCond.wait(slock);
		}

		//若是程序要退出
		if (IsExit)
		{
			slock.unlock();
			break;
		}

		//获取消息队列中信息
		pMsgbuf = psock->m_sendQueue.front();
		psock->m_sendQueue.pop_front();
		
		slock.unlock();

		//获取发送信息
		pkghdr = (PKG_Head*)pMsgbuf;
		sendbuf = (char*)(pMsgbuf + m_PKG_Hlen);
		sendsize = ntohs(pkghdr->pkgLen) - m_PKG_Hlen;
		uint32_t msgid = ntohl(pkghdr->mesgCode);
		in_port_t msgport = ntohl(pkghdr->crc32);

		if (psock->m_workconn.find(msgport) == psock->m_workconn.end())
		{
			Error_insert_File(LOG_CRIT, "The connection form port[%lu] is not exist.", msgport);
			delete[]pMsgbuf;
			pMsgbuf = NULL;
			continue;
		}
		cptr = psock->m_workconn[msgport];

		//数据发送
		ssize_t n;
		if (cptr->auxinfo.find(msgid) == cptr->auxinfo.end())
		{
			Error_insert_File(LOG_CRIT, "The message form port[%lu] is not exist, msgid[%lu].", msgport, msgid);
			delete[]pMsgbuf;
			pMsgbuf = NULL;
			continue;
		}
		tmpip = cptr->auxinfo[msgid];

		if (cptr->csinfo.find(tmpip) == cptr->csinfo.end())
		{
			Error_insert_File(LOG_CRIT, "The message form %s is not exist.", tmpip.c_str());
			delete[]pMsgbuf;
			pMsgbuf = NULL;
			continue;
		}
		regist = cptr->csinfo[tmpip];
		caddr = &(regist->addr);

		n = sendto(cptr->fd, sendbuf, sendsize, 0, (struct sockaddr*)caddr, addrsize);
		if (n < 0)
		{
			Error_insert_File(LOG_ERR, "Send Message:> Error num : [%d], %s.", errno, strerror(errno));
		}

		delete[]pMsgbuf;
		pMsgbuf = NULL;

	}//end while (IsExit != 0)

	return (void*)0;
}


void WorkSock::MessageForward_thread(void* threadData)
{
	//获取类指针
	WorkSock* psock = static_cast<WorkSock*>(threadData);

	long sended;
	char* pMsgbuf;

	int i = 0;

	PKG_Head* pkghdr;
	pConn_u cptr;
	size_t tmpsize;
	string ahost;
	uint16_t sendsize;
	sockaddr_in* caddr;
	RegistInfo* regist;
	uint32_t addrsize = sizeof(sockaddr_in);
	
	while (!IsExit)
	{
		//加锁
		unique_lock<mutex> slock(psock->m_forwardMsgMutex);
		while (psock->m_forwardQueue.empty() && !IsExit)
		{
			psock->m_forwardCond.wait(slock);
		}

		//若是程序要退出
		if (IsExit)
		{
			slock.unlock();
			break;
		}

		pMsgbuf = psock->m_forwardQueue.front();
		psock->m_forwardQueue.pop_front();

		slock.unlock();//解锁

		//读取 pConnection 连接
		ahost = pMsgbuf;
		tmpsize = ahost.size() + 1;

		if (psock->m_workconn.find(psock->m_ForwardPort) == psock->m_workconn.end())
		{
			Error_insert_File(LOG_CRIT, "The client is not connected on port[%lu]", psock->m_ForwardPort);
			delete[]pMsgbuf;
			pMsgbuf = NULL;
			continue;
		}
		cptr = psock->m_workconn[psock->m_ForwardPort];

		if (cptr->csinfo.find(ahost) == cptr->csinfo.end())
		{
			Error_insert_File(LOG_CRIT, "The message form %s is not exist.", ahost.c_str());
			delete[]pMsgbuf;
			pMsgbuf = NULL;
			continue;
		}
		regist = cptr->csinfo[ahost];
		caddr = &(regist->addr);

		pkghdr = (PKG_Head*)(pMsgbuf + tmpsize);

		sendsize = ntohs(pkghdr->pkgLen);

		sended = sendto(cptr->fd, pkghdr, sendsize, 0, (struct sockaddr*)caddr, addrsize);
		if (sended < 0)
		{
			Error_insert_File(LOG_ERR, "Send Message:> Error num : [%d], %s.", errno, strerror(errno));
		}

		delete[]pMsgbuf;
		pMsgbuf = NULL;

	}//end while (IsExit != 0)

	return;
}