#include "UdpSock.h"
#include "ProfileCtl.h"
#include "Error.h"

WorkSock::WorkSock()
{
	m_work_connections = 1;

	//epoll相关成员变量初始化
	m_epollHandle = -1;

	//数值类型初始化
	m_connection_all = 0;
	m_connection_free = 0;

	m_work_connections = 1024;
	m_MAX_Send = 1000;

	return;
}


bool WorkSock::WrokProcRelevent_Init()
{
	m_ForwardPort = stoi(findProVar("message_forward_port"));

	//数据发送线程
	thread* sendConn = new thread(&WorkSock::MessageSend_Thread, this);
	m_auxthread.emplace_back(sendConn);

	//无用连接删除线程
	thread* deleteConn = new thread(&WorkSock::UselessDelete_Thread, this);
	m_auxthread.emplace_back(deleteConn);

	//数据转发线程
	thread* msgSend = new thread(&WorkSock::MessageForward_thread, this);
	m_auxthread.emplace_back(msgSend);

	//线路维护线程
	thread* heartCheck = new thread(&WorkSock::HeartPackage_Check_Send, this);
	m_auxthread.emplace_back(heartCheck);

	//创建epoll对象
	m_epollHandle = epoll_create(m_work_connections);
	if (m_epollHandle == -1)
	{
		Error_insert_File(LOG_FATAL, "epoll_init():> Epoll create failed.");
		return false;
	}

	//初始化连接池
	InitConnections();

	Establish_Socket(m_ForwardPort, SYS_FORWARD);

	return true;
}


string WorkSock::Establish_Socket(uint16_t port, string admin_id)
{
	int auxSocket;
	string ret;

	struct sockaddr_in serv_addr;//服务器地址结构体
	memset(&serv_addr, 0, sizeof(serv_addr));

	//设置本地服务器要监听的地址和端口
	serv_addr.sin_family = AF_INET;							//选择协议族为IPV4
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);			//监听本地所有IP地址

	//第一个参数：使用IPV4协议
	//第二个参数：使用UDP连接
	//第三个参数：不知道。。。
	auxSocket = socket(AF_INET, SOCK_DGRAM, 0);
	//建立socket套接字
	if (auxSocket == -1)
	{
		//套接字建立失败记录日志；
		ret = "The udp port " + to_string(port) + " socket establish failed:> " + strerror(errno);
		Error_insert_File(LOG_FATAL, ret.c_str());

		return ret;
	}

	//设置要监听的地址与端口
	serv_addr.sin_port = htons((in_port_t)port);

	//绑定服务器地址结构
	if (bind(auxSocket, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
	{
		//服务器地址结构绑定失败，记录日志
		ret = "The udp port " + to_string(port) + " bind failed:> " + strerror(errno);
		Error_insert_File(LOG_URGENT, ret.c_str());

		return ret;
	}

	pConn_u head = NULL;
	//连接池数组首地址

	head = Get_connection(auxSocket);
	if (head == NULL)
	{
		//在Get_connection函数中已经记录了错误，此处不再记录
		ret = "Get connection pointer failed.";

		return ret;
	}

	//对监听端口的读事件建立处理方案
	if (admin_id == SYS_FORWARD)
		head->rHandle = &WorkSock::ForwardRequest_Handle;
	else
		head->rHandle = &WorkSock::UserRequest_Handle;

	if (Epoll_Add_event(auxSocket, EPOLL_CTL_ADD, EPOLLIN | EPOLLRDHUP, 0,
		head) == -1)
	{
		ret = "The udp port " + to_string(port) + " open failed:> Epoll_Add_event failed.";
		Error_insert_File(LOG_NOTICE, ret.c_str());
		
		return ret;
	}

	//监听端口设置成功，记录日志
	Error_insert_File(LOG_NOTICE, "Open udp port %d:> Succeeded.",
		port);

	head->admin_id = admin_id;
	head->opentime = GetTime();
	head->port = port;

	m_connectMutex.lock();
	m_workconn[port] = head;
	m_connectMutex.unlock();

	return ret;
}


int WorkSock::epoll_process_events(int time)
{
	//等待并获取事件
	int event = epoll_wait(m_epollHandle, m_events, MAX_EVENTS, time);
	//event 返回事件数量

	//有错误发生处理错误
	if (event == -1)
	{
		//....
		if (errno == EINTR)
		{
			Error_insert_File(LOG_WARN, "epoll_wait():> interrupted system call Udp_142.");
			//错误产生正常，返回1
			return 1;
		}
		else
		{
			Error_insert_File(LOG_ERR, "epoll_wait():> %s", strerror(errno));

			//异常错误
			return 0;
		}
	}

	//等待时间用尽
	if (event == 0)
	{
		//等待时间正常用尽
		if (time != -1)
		{
			return 1;
		}
		else
		{
			//异常状况：无限等待时间用尽
			Error_insert_File(LOG_ERR, "epoll_wait():> infinite waiting time exhausted.");
			return 0;
		}
	}

	//事件正常到达，进行处理
	pConn_u cptr;
	uint32_t revents;

	for (int i = 0; i < event; i++)
	{
		//将先前存入连接池的连接提取出来
		cptr = (pConn_u)(m_events[i].data.ptr);

		revents = m_events[i].events;

		//接收到读事件
		if (revents & EPOLLIN)
		{
			//调用读事件处理函数Read_requestHandle
			(this->*(cptr->rHandle))(cptr);
		}
	}//end for

	return 1;
}


int WorkSock::Epoll_Add_event(int sock, uint32_t eventTp,
	uint32_t flag, int addAction, pConn_u cptr)
{
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));

	if (eventTp == EPOLL_CTL_ADD)
	{
		//epoll中增加节点
		//ev.data.ptr = (void*)(cptr);
		ev.events = flag;
		cptr->events = flag;
	}
	else if (eventTp == EPOLL_CTL_MOD)
	{
		//修改节点信息
		ev.events = cptr->events;

		if (addAction == 0)
		{
			//增加标记
			ev.events |= flag;
		}
		else if (addAction == 1)
		{
			//删除标记
			ev.events &= ~flag;
		}
		else
		{
			//完全覆盖标记
			ev.events = flag;
		}

		cptr->events = ev.events;//更新记录标记
	}
	else
	{
		//删除节点
		//....
		return 1;
	}

	ev.data.ptr = (void*)(cptr);

	cerr << m_epollHandle << endl;

	if (epoll_ctl(m_epollHandle, eventTp, sock, &ev) == -1)
	{
		Error_insert_File(LOG_FATAL, "Epoll event addition failed.");
		return -1;
	}

	return 1;
}


string WorkSock::Close_Sockets(string admin_id)
{
	size_t i = 0;
	string ret;
	vector<pConn_u> delConn;
	
	if (m_workconn.size() == 0)
	{
		ret = "no port is openning";
		return ret;
	}

	for (auto& tmp : m_workconn)
	{
		if (tmp.second->admin_id == admin_id)
		{
			delConn.emplace_back(tmp.second);
		}

		if (++i == m_workconn.size())
			break;
	}

	for (auto& cptr : delConn)
	{
		ActiveShutdown(cptr);
	}

	return ret;
}


void WorkSock::RegiestInfo_Delete(string& adminId)
{
	pConn_u cptr = m_workconn[m_ForwardPort];

	if (cptr->csinfo.find(adminId) == cptr->csinfo.end())
	{
		return;
	}

	m_heartLock.lock();
	RegistInfo* info = cptr->csinfo[adminId];

	cptr->auxinfo.erase(info->msgid);
	cptr->csinfo.erase(adminId);

	info->timed = 0;

	m_heartLock.unlock();

	return;
}


void WorkSock::ClearSendQueue()
{
	list<char*>::iterator it;

	for (it = m_sendQueue.begin(); it != m_sendQueue.end(); it++)
	{
		delete[](*it);
	}

	m_sendQueue.clear();

	return;
}


string WorkSock::Close_Socket(uint16_t port, string admin_id)
{
	string ret;

	if (m_workconn.find(port) == m_workconn.end())
	{
		ret = "this port is not openning";
		return ret;
	}

	pConn_u cptr = m_workconn[port];
	if (cptr->admin_id != admin_id)
	{
		ret = "this is not the port you openned";
		return ret;
	}
	
	ActiveShutdown(cptr);

	return ret;
}


void WorkSock::ActiveShutdown(pConn_u cptr)
{
	if (cptr->fd != -1)
	{
		close(cptr->fd);
		cptr->fd = -1;
	}

	Free_connection(cptr);

	return;
}


void WorkSock::WrokProcRelevent_Off()
{
	IsExit = true;

	//等待所有线程终止
	for (auto temp : m_auxthread)
	{
		temp->join();
	}

	//释放线程内存
	for (auto temp : m_auxthread)
	{
		if (temp != NULL)
		{
			delete temp;
			temp = NULL;
		}
	}
	m_auxthread.clear();

	//清空发送队列
	ClearSendQueue();

	//释放连接池
	ClearConnections();

	return;
}


//析构函数释放内存
WorkSock::~WorkSock()
{
	return;
}