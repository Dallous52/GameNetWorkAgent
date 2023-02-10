#include "TcpSock.h"
#include "Manage.h"
#include "Error.h"
#include "Process.h"
#include "Crc32.h"

bool MSocket::Read_requestHandle(pConnection cptr)
{
	cptr->recvlen = ReceiveProc(cptr, cptr->bufptr, _PKG_MAX_LENGTH);

	if (cptr->recvlen <= 0)
	{
		ActiveShutdown(cptr);
		return false;
	}

	if (m_floodCheckStart && TestFlood(cptr))
	{
		Error_insert_File(LOG_CRIT, "Flooding attack detected, close connection");
		ActiveShutdown(cptr);
		return false;
	}
	
	return true;
}


ssize_t MSocket::ReceiveProc(pConnection cptr, char* buffer, ssize_t buflen)
{
	ssize_t n = recv(cptr->fd, buffer, buflen, 0);
	//recv系统函数用于接收socket数据

	if (n == 0)
	{
		//客户端正常关闭，回收连接
		Error_insert_File(LOG_NOTICE, "The client shuts down normally.");
		return -1;
	}

	//错误处理
	if (n < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			//在ET模式下可能出现，表示没有收到数据
			Error_insert_File(LOG_WARN, "receive data:> no data in Socket buffer.");
			return -1;
		}

		if (errno == EINTR)
		{
			Error_insert_File(LOG_WARN, "receive data:> interrupted system call.");
			return -1;
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

		return -1;
	}//end if

	return n;
}


void MSocket::AdminRequest_Handle(pConnection cptr)
{
	if (!Read_requestHandle(cptr))
	{
		return;
	}

	PKG_Head* pPkgHead;
	pPkgHead = (PKG_Head*)cptr->bufptr;

	unsigned short pkgLen = ntohs(pPkgHead->pkgLen);
	//ntohs 将2字节的网络字节序转换为主机字节须

	//恶意包/错误包处理
	if (cptr->recvlen != (long)pkgLen)
	{
		cptr->recvlen = 0;
		return;
	}

	//为信息包分配内存 连接头 + 包头 + 包体
	size_t tmpsize = cptr->connectId.size() + 1;
	char* auxBuffer = new char[tmpsize + pkgLen];
	cptr->MsgInfo = auxBuffer;

	//填写 连接头
	strcpy(cptr->MsgInfo, cptr->connectId.c_str());

	//填写 包头 + 包体
	auxBuffer += tmpsize;
	memcpy(auxBuffer, cptr->bufptr, pkgLen);

	//将信息包放入信息队列中并处理
	mthreadpool.GetMassage_And_Proc(cptr->MsgInfo);

	//完成一个包的接收，还原为初始值
	cptr->MsgInfo = NULL;
	cptr->recvlen = 0;

	return;
}


void MSocket::MessageForward_Handle(pConnection cptr)
{
	if (!Read_requestHandle(cptr))
	{
		return;
	}
	
	PKG_Head* pPkgHead;
	pPkgHead = (PKG_Head*)cptr->bufptr;

	unsigned short pkgLen = ntohs(pPkgHead->pkgLen);
	//ntohs 将2字节的网络字节序转换为主机字节须
	
	//恶意包/错误包处理
	if (cptr->recvlen != (long)pkgLen)
	{
		cptr->recvlen = 0;
		return;
	}

	//为信息包分配内存
	size_t tmpsize = cptr->connectId.size() + 1;
	char* auxBuffer = new char[tmpsize + pkgLen];

	//封装获取连接信息
	strcpy(auxBuffer, cptr->connectId.c_str());

	//填写 包头 + 包体
	memcpy(auxBuffer + tmpsize, cptr->bufptr, pkgLen);

	//放入用户接收队列发送
	MsgSend_StU(auxBuffer);

	//完成一个包的接收，还原为初始值
	cptr->recvlen = 0;

	return;
}


void MSocket::UserMessage_Handle(pConnection cptr)
{
	if (!Read_requestHandle(cptr))
	{
		return;
	}

	size_t tmpsize = cptr->connectId.size() + 1;
	char* sendbuf = new char[m_PKG_Hlen + tmpsize + cptr->recvlen];
	
	//封装获取连接信息
	strcpy(sendbuf, cptr->connectId.c_str());

	//填充包头
	PKG_Head* pkghdr = (PKG_Head*)(sendbuf + tmpsize);
	pkghdr->mesgCode = htonl(0);
	pkghdr->pkgLen = htons(m_PKG_Hlen + cptr->recvlen);
	
	//填充包体
	char* sendInfo = sendbuf + tmpsize + m_PKG_Hlen;

	memcpy(sendInfo, cptr->bufptr, cptr->recvlen);

	//填入端口号
	pkghdr->crc32 = htonl(cptr->port);

	MsgSend_StS(sendbuf);

	return;
}


bool MSocket::SendProc(pConnection cptr, char* buffer, ssize_t buflen)
{
	ssize_t sended = send(cptr->fd, buffer, buflen, 0);

	if (sended > 0)
	{
		if (sended == buflen)
		{
			delete[]cptr->delptr;
			cptr->delptr = NULL;
			cptr->sendptr = NULL;
		}
		else
		{
			//更新发送零量
			cptr->sendptr = cptr->sendptr + sended;
			cptr->sendLen -= sended;

			//依靠系统通知发送信息
			++cptr->throwSendCount;

			if (Epoll_Add_event(cptr->fd, EPOLL_CTL_MOD, EPOLLOUT,
				0, cptr) == -1)
			{
				Error_insert_File(LOG_CRIT,
					"Send message:> epoll mod add EPOLLOUT failed.");
			}

			Error_insert_File(LOG_INFO,
				"Send message:> send buffer is full, use system call.");
		}//end if

		return true;
	}//end if (sended > 0)

	else if (sended == 0)
	{
		//预料之外的异常错误
		Error_insert_File(LOG_ERR, "Send message:> sended is equal 0.");

		delete[]cptr->delptr;
		cptr->delptr = NULL;
		cptr->sendptr = NULL;
		return false;
	}

	else if (errno == EAGAIN)
	{
		//缓冲区满，依靠系统通知发送信息
		++cptr->throwSendCount;

		if (Epoll_Add_event(cptr->fd, EPOLL_CTL_MOD, EPOLLOUT,
			0, cptr) == -1)
		{
			Error_insert_File(LOG_CRIT,
				"Send message:> epoll mod EPOLLOUT failed.");
		}

		Error_insert_File(LOG_INFO,
			"Send message:> send buffer is full, use system call.");

		return true;
	}
	else
	{
		//通常为对端断开,返回为-2
		delete[]cptr->delptr;
		cptr->delptr = NULL;
		cptr->sendptr = NULL;

		return false;
	}
}


void MSocket::Write_requestHandle(pConnection cptr)
{
	ssize_t sended = SendProc(cptr, cptr->sendptr, cptr->sendLen);

	if (sended > 0 && sended != cptr->sendLen)
	{
		//数据没有发送完
		cptr->sendptr = cptr->sendptr + sended;
		cptr->sendLen -= sended;
		return;
	}
	else if (sended == -1)
	{
		//系统通知可以发送数据，缓冲区却满
		//意料外异常
		Error_insert_File(LOG_ERR,
			"Write request:> send buffer is full.");
		return;
	}

	if (sended > 0 && sended == cptr->sendLen)
	{
		//数据发送完毕，EPOLLOUT标记清空
		if (Epoll_Add_event(cptr->fd, EPOLL_CTL_MOD, EPOLLOUT,
			1, cptr) == -1)
		{
			Error_insert_File(LOG_CRIT,
				"Send message:> epoll mod delete EPOLLOUT failed.");
		}

		//cerr << "Notice:> Detention message send compelete." << endl;
	}

	delete[]cptr->delptr;
	cptr->delptr = NULL;
	cptr->sendptr = NULL;
	--cptr->throwSendCount;
	
	return;
}


void MSocket::MsgSend_StS(char* sendBuf)
{
	//读取 pConnection 连接
	string ahost = sendBuf;
	if (m_workconn.find(ahost) == m_workconn.end())
	{
		delete[]sendBuf;
		sendBuf = NULL;
		return;
	}
	pConnection cptr = m_workconn[ahost];

	//单个连接滞留的未发送数据过多
	if (cptr->requestSent > m_MAX_rqSend)
	{
		delete[]sendBuf;
		sendBuf = NULL;
		ActiveShutdown(cptr);

		return;
	}

	m_StSMutex.lock();

	//防止滞留发送数据包过多
	if (m_StSQueue.size() > m_MAX_Send)
	{
		delete[]sendBuf;
		sendBuf = NULL;
		return;
	}

	//消息放入消息队列
	++cptr->requestSent;
	m_StSQueue.push_back(sendBuf);

	m_StSMutex.unlock();
	
	//让卡在m_sendCond的线程继续执行
	m_StSCond.notify_one();

	return;
}


void MSocket::MsgSend_StU(char* sendBuf)
{
	//读取 pConnection 连接
	string ahost = sendBuf;
	if (m_workconn.find(ahost) == m_workconn.end())
	{
		delete[]sendBuf;
		sendBuf = NULL;
		return;
	}
	pConnection cptr = m_workconn[ahost];

	//单个连接滞留的未发送数据过多
	if (cptr->requestSent > m_MAX_rqSend)
	{
		delete[]sendBuf;
		sendBuf = NULL;
		ActiveShutdown(cptr);

		return;
	}

	m_StUMutex.lock();

	//防止滞留发送数据包过多
	if (m_StUQueue.size() > m_MAX_Send)
	{
		delete[]sendBuf;
		sendBuf = NULL;
		return;
	}

	//消息放入消息队列
	++cptr->requestSent;
	m_StUQueue.push_back(sendBuf);

	m_StUMutex.unlock();

	//让卡在m_sendCond的线程继续执行
	m_StUCond.notify_one();

	return;
}


void MSocket::MessageStS_Thread(void* threadData)
{
	//获取类指针
	MSocket* psock = static_cast<MSocket*>(threadData);

	char* pMsgbuf;

	PKG_Head* pkghdr;
	pConnection cptr;
	size_t tmpsize;

	while (!IsExit)
	{
		//加锁
		unique_lock<mutex> slock(psock->m_StSMutex);
		while (psock->m_StSQueue.empty() && !IsExit)
		{
			psock->m_StSCond.wait(slock);
		}

		//若是程序要退出
		if (IsExit)
		{
			slock.unlock();
			break;
		}

		pMsgbuf = psock->m_StSQueue.front();
		psock->m_StSQueue.pop_front();

		slock.unlock();//解锁

		//读取 pConnection 连接
		string ahost = pMsgbuf;
		if (psock->m_workconn.find(ahost) == psock->m_workconn.end())
		{
			Error_insert_File(LOG_CRIT, "MessageStS_Thread:> No such connection named %s.", ahost.c_str());

			delete[]pMsgbuf;
			pMsgbuf = NULL;
			continue;
		}
		cptr = psock->m_workconn[ahost]->forwardConn;
		tmpsize = ahost.size() + 1;

		if (cptr->throwSendCount > 0)
		{
			//拥有该标记的信息由系统信号激发发送,将信息包保留
			psock->m_StSMutex.lock();
			
			psock->m_StSQueue.emplace_back(pMsgbuf);

			psock->m_StSMutex.unlock();
			continue;
		}
		
		//以下为消息发送
		--cptr->requestSent;

		pkghdr = (PKG_Head*)(pMsgbuf + tmpsize);

		cptr->delptr = pMsgbuf;

		cptr->sendptr = (char*)pkghdr;
		cptr->sendLen = ntohs(pkghdr->pkgLen);

		psock->SendProc(cptr, cptr->sendptr, cptr->sendLen);

	}//end while (IsExit != 0)

	return;
}

void MSocket::MessageStU_Thread(void* threadData)
{
	//获取类指针
	MSocket* psock = static_cast<MSocket*>(threadData);

	char* pMsgbuf;

	PKG_Head* pkghdr;
	pConnection cptr;
	size_t tmpsize;

	while (!IsExit)
	{
		//加锁
		unique_lock<mutex> slock(psock->m_StUMutex);
		while (psock->m_StUQueue.empty() && !IsExit)
		{
			psock->m_StUCond.wait(slock);
		}

		//若是程序要退出
		if (IsExit)
		{
			slock.unlock();
			break;
		}

		pMsgbuf = psock->m_StUQueue.front();
		psock->m_StUQueue.pop_front();

		slock.unlock();//解锁

		//读取 pConnection 连接
		string ahost = pMsgbuf;
		if (psock->m_workconn.find(ahost) == psock->m_workconn.end())
		{
			Error_insert_File(LOG_CRIT, "MessageStU_Thread:> No such connection named %s.", ahost.c_str());

			delete[]pMsgbuf;
			pMsgbuf = NULL;
			continue;
		}
		cptr = psock->m_workconn[ahost];
		tmpsize = ahost.size() + 1;

		if (cptr->throwSendCount > 0)
		{
			//拥有该标记的信息由系统信号激发发送,将信息包保留
			psock->m_StUMutex.lock();

			psock->m_StUQueue.emplace_back(pMsgbuf);

			psock->m_StUMutex.unlock();
			continue;
		}

		//以下为消息发送
		--cptr->requestSent;

		pkghdr = (PKG_Head*)(pMsgbuf + tmpsize);

		cptr->delptr = pMsgbuf;
		
		cptr->sendptr = (char*)(pkghdr + m_PKG_Hlen);
		cptr->sendLen = ntohs(pkghdr->pkgLen) - m_PKG_Hlen;
		
		psock->SendProc(cptr, cptr->sendptr, cptr->sendLen);

	}//end while (IsExit != 0)

	return;
}


void MSocket::ThreadReceive_Proc(char* msgInfo)
{
	//读取 pConnection 连接
	string ahost = msgInfo;
	if (m_workconn.find(ahost) == m_workconn.end())
	{
		return;
	}
	pConnection cptr = m_workconn[ahost];
	size_t tmpsize = ahost.size() + 1;

	PKG_Head* pPkghdr = (PKG_Head*)(msgInfo + tmpsize);
	//指向包头指针

	char* pkgBodyInfo = NULL;
	//指向包体指针
	
	unsigned short pkglen = ntohs(pPkghdr->pkgLen);
	uint32_t pkgCrc32 = ntohl(pPkghdr->crc32);

	if (m_PKG_Hlen == pkglen)
	{
		//只有包头没有包体
		if (pkgCrc32 != 0)
		{
			//无包体的包校验码为0
			//校验码错误直接丢弃
			return;
		}
	}
	else
	{
		pkgBodyInfo = msgInfo + tmpsize + m_PKG_Hlen;

		uint32_t mcrc32 = crc32((unsigned char*)pkgBodyInfo, pkglen - m_PKG_Hlen);
		//计算crc校验值

		if (pkgCrc32 != mcrc32)
		{
			//cerr << "The crc32 check code is differnet, package discard." << endl;
			return;
		}
	}

	Handle_Command(cptr, pkgBodyInfo, pkglen - m_PKG_Hlen);

	return;
}


