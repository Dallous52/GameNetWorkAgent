#include "TcpSock.h"
#include "ProfileCtl.h"
#include "Error.h"

MSocket::MSocket()
{
	m_work_connections = 1;

	//epoll相关成员变量初始化
	m_epollHandle = -1;

	m_PKG_Hlen = sizeof(PKG_Head);

	//数值类型初始化
	m_connection_all = 0;
	m_connection_free = 0;
	m_connection_work = 0;
	m_MAX_Send = 0;
	m_fullyGet = false;

	return;
}

bool MSocket::Initialize()
{
	Command_Init();

	m_logicPort = stoul(findProVar("programming_control_port"));

	m_forwardPort = stoul(findProVar("message_forward_port"));

	m_work_connections = stoi(findProVar("work_connections"));

	m_floodCheckStart = stoi(findProVar("flood_check_start"));

	m_floodInterval = stoul(findProVar("flood_interval"));

	m_floodPackages = stoul(findProVar("flood_packages"));

	m_MAX_Send = stoi(findProVar("max_send"));

	m_MAX_rqSend = stoi(findProVar("max_request_send"));

	m_rangeOf.first = stoi(findProVar("range_of_min"));
	m_rangeOf.second = stoi(findProVar("range_of_max"));

	return true;
}


bool MSocket::WrokProcRelevent_Init()
{
	//数据发送线程
	thread* sendConn = new thread(&MSocket::MessageStS_Thread, this);
	m_auxthread.push_back(sendConn);

	//数据转发线程
	thread* forwardConn = new thread(&MSocket::MessageStU_Thread, this);
	m_auxthread.push_back(forwardConn);

	//创建epoll对象
	m_epollHandle = epoll_create(m_work_connections);
	if (m_epollHandle == -1)
	{
		Error_insert_File(LOG_FATAL, "epoll_init():> Epoll create failed.");
		return false;
	}

	//分配内存，创建连接池
	InitConnections();

	Establish_listenSocket(m_logicPort, SYS_CTRL);
	Establish_listenSocket(m_forwardPort, SYS_FORWARD);

	return true;
}


string MSocket::Establish_listenSocket(in_port_t port, string admin_id)
{
	int auxSocket;
	string ret;

	struct sockaddr_in serv_addr;//服务器地址结构体
	memset(&serv_addr, 0, sizeof(serv_addr));

	//设置本地服务器要监听的地址和端口
	serv_addr.sin_family = AF_INET;							//选择协议族为IPV4
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);//监听本地所有IP地址

	//第一个参数：使用IPV4协议
	//第二个参数：使用TCP连接
	//第三个参数：协议类型
	auxSocket = socket(AF_INET, SOCK_STREAM, 0);
	//建立socket套接字
	if (auxSocket == -1)
	{
		//套接字建立失败记录日志；
		ret = "The tcp port " + to_string(port) + " socket establish failed:> " + strerror(errno);
		Error_insert_File(LOG_FATAL, ret.c_str());

		return ret;
	}

	//setsockopt():设置套接字参数
	//第二个参数和第三个参数配套使用,一一对应
	//此处SO_REUSEADDR允许同一个端口上套接字的复用
	//用于解决bind函数因TCP状态TIME_WAIT而出错的问题
	//int reuseaddr = 1;
	//if (setsockopt(auxSocket, SOL_SOCKET, SO_REUSEADDR,
	//	(const void*)&reuseaddr, sizeof(int)) == -1)
	//{
	//	//套接字复用设置失败，记录日志
	//	Error_insert_File(LOG_URGENT, "The %u port socket of process %d reuse address setting failed:> %s.",
	//		port, this_pid, strerror(errno));
	//	return false;
	//}

	//int reuseport = 1;
	//if (setsockopt(auxSocket, SOL_SOCKET, SO_REUSEPORT,
	//	(const void*)&reuseport, sizeof(int)) == -1)
	//{
	//	//端口复用设置失败，记录日志
	//	Error_insert_File(LOG_URGENT, "The %u port socket of process %d reuse port setting failed:> %s.",
	//		port, this_pid, strerror(errno));
	//}

	if (Set_NoBlock(auxSocket) == false)
	{
		//套接字非阻塞设置失败，记录日志
		ret = "The " + to_string(port) + " port socket no block setting failed : >" + strerror(errno);
		Error_insert_File(LOG_URGENT, ret.c_str());

		return ret;
	}

	//设置要监听的地址与端口
	serv_addr.sin_port = htons(port);

	//绑定服务器地址结构
	if (bind(auxSocket, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
	{
		//服务器地址结构绑定失败，记录日志
		ret = "The tcp port " + to_string(port) + " bind failed:> " + strerror(errno);
		Error_insert_File(LOG_URGENT, ret.c_str());

		return ret;
	}

	//参数2表示服务器可以积压未处理完的连接请求个数
	if (listen(auxSocket, ListenBackLog) == -1)
	{
		//监听端口失败
		ret = "Listen to port " + to_string(port) + " failed:> " + strerror(errno);
		Error_insert_File(LOG_URGENT, ret.c_str());

		return ret;
	}

	//监听端口设置成功，记录日志
	Error_insert_File(LOG_NOTICE, "Listening to port %u Succeeded.",
		port);

	pConnection head;

	head = Get_connection(auxSocket);
	if (head == NULL)
	{
		//在Get_connection函数中已经记录了错误，此处不再记录
		exit(2);
	}

	head->port = port;
	head->admin = admin_id;

	//对监听端口的读事件建立处理方案
	head->rHandle = &MSocket::Event_accept;
	head->wHandle = NULL;

	if (Epoll_Add_event(head->fd, EPOLL_CTL_ADD, EPOLLIN | EPOLLRDHUP, 0,
		head) == -1)
	{
		exit(2);
	}

	head->opentime = GetTime();
	OpenMember* member = new OpenMember;
	member->adcptr = head;
	m_admin[port] = member;

	return ret;
}


int MSocket::epoll_process_events(int time)
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
			Error_insert_File(LOG_WARN, "epoll_wait():> interrupted system call Tcp_260.");
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
	pConnection cptr;
	uint32_t revents;

	for (int i = 0; i < event; i++)
	{
		cptr = (pConnection)(m_events[i].data.ptr);

		revents = m_events[i].events;

		//接收到用户的关闭连接请求等一些请求时
		if (revents & (EPOLLERR | EPOLLHUP))
		{
			revents |= EPOLLIN | EPOLLOUT;
			//做好关闭连接四次挥手的 读 写 准备
		}

		//接收到读事件
		if (revents & EPOLLIN)
		{
			//调用读事件处理函数Event_accept
			(this->*(cptr->rHandle))(cptr);
		}

		if (revents & EPOLLOUT)
		{
			if (revents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
			{
				//客户端关闭
				cptr->throwSendCount = 0;
			}
			else
			{
				(this->*(cptr->wHandle))(cptr);
			}
		}
	}//end for

	return 1;
}


int MSocket::Epoll_Add_event(int sock, uint32_t eventTp,
	uint32_t flag, int addAction, pConnection cptr)
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

	if (epoll_ctl(m_epollHandle, eventTp, sock, &ev) == -1)
	{
		Error_insert_File(LOG_FATAL, "Epoll event addition failed.");
		return -1;
	}

	return 1;
}


//设置接收方式非阻塞
bool MSocket::Set_NoBlock(int sock)
{
	int nb = 1;

	//FIONBIO:设置非阻塞标记，0清除，1设置(nb)
	if (ioctl(sock, FIONBIO, &nb) == -1)
	{
		return false;
	}

	return true;
}


//关闭监听端口
void MSocket::Connection_Exit(string admin_id)
{
	m_connectMutex.lock();
	if (m_workconn.find(admin_id) == m_workconn.end())
	{
		m_connectMutex.unlock();
		return;
	}
	m_connectMutex.unlock();

	pConnection cptr = m_workconn[admin_id];

	ActiveShutdown(cptr);
}


string MSocket::Close_Socket(uint16_t port, const string& adminid)
{
	string ret;
	OpenMember* member;
	pConnection cptr;

	if (m_admin.find(port) == m_admin.end())
	{
		ret = "this port is not openning";
		return ret;
	}
	member = m_admin[port];

	if (member->adcptr->admin != adminid)
	{
		ret = "this is not the port you openned";
		return ret;
	}

	//关闭主连接
	ActiveShutdown(member->adcptr);

	while (!member->opened.empty())
	{
		m_memberLock.lock();
		cptr = member->opened.front();
		member->opened.pop_front();
		m_memberLock.unlock();

		ActiveShutdown(cptr);
	}

	m_admin.erase(port);

	return ret;
}


string MSocket::Close_Sockets(const string& adminid)
{
	size_t i = 0;
	string ret;
	vector<uint16_t> delConn;

	if (m_admin.size() == 0)
	{
		ret = "no port is openning";
		return ret;
	}

	for (auto& tmp : m_admin)
	{
		if (tmp.second->adcptr->admin == adminid)
		{
			delConn.emplace_back(tmp.first);
		}

		if (++i == m_admin.size())
			break;
	}

	for (auto& port : delConn)
	{
		Close_Socket(port, adminid);
	}

	return ret;
}


string MSocket::Show_Ports(string admin_id)
{
	string ret;
	size_t i = 0;

	if (m_admin.empty())
		return ret;

	for (auto& tmp : m_admin)
	{
		if (tmp.second->adcptr->admin == admin_id)
		{
			ret += "tcp\t" + tmp.second->adcptr->opentime + '\t' + to_string(tmp.first) + '\n';
		}

		if (++i == m_admin.size())
			break;
	}

	return ret;
}


void MSocket::ClearSendQueue()
{
	list<char*>::iterator it;

	for (it = m_StSQueue.begin(); it != m_StSQueue.end(); it++)
	{
		delete[](*it);
	}

	m_StSQueue.clear();

	for (it = m_StUQueue.begin(); it != m_StUQueue.end(); it++)
	{
		delete[](*it);
	}

	m_StUQueue.clear();

	return;
}


void MSocket::WrokProcRelevent_Off()
{
	IsExit = true;

	vector<thread*>::iterator it;
	//等待所有线程终止
	for (it = m_auxthread.begin(); it != m_auxthread.end(); it++)
	{
		(*it)->join();
	}

	//释放线程内存
	for (it = m_auxthread.begin(); it != m_auxthread.end(); it++)
	{
		if ((*it) != NULL)
		{
			delete (*it);
			(*it) = NULL;
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
MSocket::~MSocket()
{
	return;
}