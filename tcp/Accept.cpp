#include "TcpSock.h"
#include "Error.h"

socklen_t socklen = sizeof(sockaddr_in);
sockaddr_in caddr;

//新建连接专用函数
void MSocket::Event_accept(pConnection old)
{
	struct sockaddr serv_sockaddr;//服务器socket地址
	int err;
	int level;
	int a_sock;
	static bool use_accept4 = true; //是否使用accept4函数
	
	pConnection newptr;


	Error_insert_File(LOG_DEBUG, "Debug:> A connect comming from prot [%d]", old->port);

	do
	{
		if (use_accept4)
		{
			//使用accept4函数可以直接将套接字设置为非阻塞
			a_sock = accept4(old->fd, &serv_sockaddr, &socklen, SOCK_NONBLOCK);
		}
		else
		{
			//使用accept函数需要在后期手动设置非阻塞
			a_sock = accept(old->fd, &serv_sockaddr, &socklen);
		}

		//连接错误处理
		if (a_sock == -1)
		{
			err = errno;

			if (err == EAGAIN)
			{
				//发生此错误表示accept没有准备好，直接退出
				return;
			}

			level = LOG_URGENT;

			//软件引起的连接终止
			if (err == ECONNABORTED)
			{
				level = LOG_ERR;
			}
			else if  (err == ENFILE || err == EMFILE)
			{
				//进程fd用尽
				level = LOG_CRIT;
			}

			Error_insert_File(level, "event accept failed:> %s", strerror(errno));

			if (use_accept4 && ENOSYS)
			{
				//系统不支持acceot4函数，使用accept函数再来一次
				use_accept4 = false;
				continue;
			}

			if (err == ECONNABORTED)
			{
				//可忽略错误 ： do nothing
			}

			if (err == EMFILE || err == ENFILE)
			{
				//........
			}

			return;
		}//end if (a_sock == -1)
		
		//----------------------------accept成功----------------------
		Error_insert_File(LOG_INFO, "accept success, port is [%d]", old->port);
		
		//工作连接数大于最大连接数，系统繁忙不再接收连接
		if (m_connection_work >= m_work_connections)
		{
			close(a_sock);
			return;
		}

		//为用户连接套接字申请连接池连接
		newptr = Get_connection(a_sock);

		//因为连接池空间不足，申请连接失败
		if (newptr == NULL)
		{
			//关闭连接
			if (close(a_sock) == -1)
			{
				Error_insert_File(LOG_URGENT, "epoll accept:> a_socket close failed.");
			}

			return;
		}

		//连接池中申请连接成功
		
		//将客户端地址拷贝至连接对象
		memcpy(&newptr->s_addr, &serv_sockaddr, socklen);

		//关联连接对象与监听对象
		newptr->port = old->port;

		//设置accept函数的非阻塞
		if (!use_accept4)
		{
			if (Set_NoBlock(a_sock) == false)
			{
				//设置非阻塞失败
				ActiveShutdown(newptr);
				return;
			}
		}
		
		if (newptr->port == m_logicPort)
			newptr->rHandle = &MSocket::AdminRequest_Handle;
		else if (newptr->port == m_forwardPort)
			newptr->rHandle = &MSocket::MessageForward_Handle;
		else
			newptr->rHandle = &MSocket::UserMessage_Handle;

		//可写标记
		newptr->wHandle = &MSocket::Write_requestHandle;

		//增加epoll事件
		if (Epoll_Add_event(a_sock, EPOLL_CTL_ADD, EPOLLIN  | EPOLLRDHUP, 0,
			newptr) == -1)
		{
			ActiveShutdown(newptr);
			return;
		}

		m_connection_work++;
		
		newptr->opentime = GetTime();
		Id_Distribute(newptr);

		break;

	} while (true);

	return;
}


void MSocket::Id_Distribute(pConnection cptr)
{
	memcpy(&caddr, &cptr->s_addr, socklen);

	if (cptr->port == m_logicPort)
	{
		m_connectMutex.lock();

		//主连接接入进行初始化
		cptr->admin = cptr->connectId = inet_ntoa(caddr.sin_addr);

		if (m_workconn.find(cptr->admin) != m_workconn.end())
		{
			m_connectMutex.unlock();
			ActiveShutdown(cptr);
			return;
		}

		cptr->forwardConn = cptr;
		m_workconn[cptr->admin] = cptr;
		m_unfullyConnect[cptr->admin] = NULL;

		m_connectMutex.unlock();
	}
	else if (cptr->port == m_forwardPort)
	{
		cptr->admin = inet_ntoa(caddr.sin_addr);

		if (m_unfullyConnect.find(cptr->admin) == m_unfullyConnect.end())
		{
			ActiveShutdown(cptr);
			return;
		}

		m_unfullyConnect[cptr->admin] = cptr;
	}
	else
	{
		//填入连接信息
		cptr->admin = m_admin[cptr->port]->adcptr->admin;
		cptr->connectId = inet_ntoa(caddr.sin_addr);
		cptr->connectId += ":" + to_string(ntohs(caddr.sin_port));

		//判断连接是否存在
		if (m_unfullyConnect.find(cptr->admin) == m_unfullyConnect.end())
		{
			Error_insert_File(LOG_CRIT, "Id_Distribute:> No such main connection named %s.", cptr->admin.c_str());
			ActiveShutdown(cptr);
			return;
		}
		
		//未完成转发连接数量不足
		if (m_unfullyConnect[cptr->admin] == NULL)
		{
			ActiveShutdown(cptr);
			return;
		}

		pConnection fcptr = m_unfullyConnect[cptr->admin];
		m_unfullyConnect[cptr->admin] = NULL;

		m_connectMutex.lock();

		//关联转发连接
		fcptr->connectId = cptr->connectId;
		cptr->forwardConn = fcptr;
		m_workconn[cptr->connectId] = cptr;

		m_memberLock.lock();
		m_admin[cptr->port]->opened.emplace_back(cptr);
		m_memberLock.unlock();

		m_connectMutex.unlock();

		//发送注册信息
		pConnection aptr = m_workconn[cptr->admin];
		PromptMsg_Send(cptr->connectId, aptr, cptr->port);
	}

	return;
}