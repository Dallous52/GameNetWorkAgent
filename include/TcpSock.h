#ifndef TCPSOCK
#define TCPSOCK

#include "Globar.h"

//服务器可以积压未处理完的连接请求个数
#define ListenBackLog 99						  

//事件最大处理数
#define MAX_EVENTS	  512	

#define SYS_CTRL	"system_control"

//*****************通讯状态相关定义*******************

#define _PKG_MAX_LENGTH 30000						  //每个包最大长度

#define MAX_ID_NUM		4294967000					  //id最大数
#pragma pack(1)//设置系统默认对齐字节序为 1

//包头结构定义
typedef struct PKG_Head
{
	unsigned short pkgLen;			 		//报文总长
	uint32_t mesgCode;						//用于区分报文内容

	uint32_t crc32;							//crc32错误校验【循环冗余校验】

}PKG_Head;

#pragma pack()//还原系统默认对其字符序

//****************************************************

typedef struct Connection  vConnection, *pConnection;	//指向连接信息结构指针

typedef class MSocket MSocket;

//事件处理函数指针
typedef void (MSocket::* Event_handle)(pConnection head);


//连接池内连接结构
struct Connection
{
	//******初始化与释放******
	Connection();
	~Connection();
	void GetOne();					//当获取或创建一个连接时执行此函数初始化成员
	void FreeOne();					//当要释放一个函数时调用此函数
	
	//************************


	//******连接基本信息******
	int fd;							//套接字句柄
	in_port_t port;					//端口号

	string	admin;					//管理员连接id
	string  connectId;				//本链接id
	uint32_t events;				//本连接的事件处理类型
	struct sockaddr	s_addr;			//用于保存对方地址信息
	
	Event_handle rHandle;			//读事件处理相关函数
	Event_handle wHandle;			//写事件处理相关函数
	//*************************


	//********收包有关*********
	char*	bufptr;					//初始指向包头缓冲区首元素，用于辅助接收
	ssize_t recvlen;				//收到包的长度

	char* MsgInfo;					//指向完整的信息包
	//*************************


	//*********发包相关*********
	char* sendptr;					//发送数据头指针
	char* delptr;					//用于发送数据后释放内存的指针
	size_t sendLen;					//要发送数据的长度

	atomic<int> requestSent;		//客户端请求发送的信息数
	atomic<int> throwSendCount;		//是否通过系统发送，> 0 表示通过系统通知发送
	
	pConnection forwardConn;		//转发专业连接
	//**************************


	//*********安全相关*********
	uint64_t floodCheckLast;		//最后一次泛洪检测时间
	size_t floodCount;				//泛洪嫌疑包数量
	//**************************


	//**********其他***********
	string opentime;							//连接创建时间
	bool isexit;								//判断用户是否退出
	mutex logicMutex;							//业务逻辑处理函数互斥锁
};


typedef struct
{
	pConnection adcptr;			//存放开放端口主连接
	list<pConnection> opened;	//存放由指定端口接入的连接
}OpenMember;


//网络信息处理类
class MSocket
{
public:

	MSocket();
	
	~MSocket();

public:
	
	//初始化函数
	bool Initialize();

	//工作进程相关量初始化
	bool WrokProcRelevent_Init();

	//epoll事件处理函数
	int epoll_process_events(int time);

	//用于线程接收信号处理
	void ThreadReceive_Proc(char* msgInfo);

	//工作进程相关关闭回收函数
	void  WrokProcRelevent_Off();

	//增加epoll事件
	int Epoll_Add_event(int sock, uint32_t enventTp,
		uint32_t otherflag, int addAction, pConnection cptr);

	//将一个待发送的数据放入消息队列等待发送（系统到系统）
	void MsgSend_StS(char* sendBuf);

	//将一个待发送的数据放入消息队列等待发送(系统到客户)
	void MsgSend_StU(char* sendBuf);

	//主动关闭一个连接
	void ActiveShutdown(pConnection cptr);

	//退出关闭主连接
	void Connection_Exit(string admin_id);

	//用于管理员连接后的处理
	void AdminRequest_Handle(pConnection cptr);

	//用于接收到代理客户端信息后的处理
	void MessageForward_Handle(pConnection cptr);

	//用于接收到用户包后的处理
	void UserMessage_Handle(pConnection cptr);

	//分发管理员id
	void Id_Distribute(pConnection cptr);

private:
	//建立监听端口
	string Establish_listenSocket(in_port_t port, string admin_id);
	
	//设置监听非阻塞
	bool Set_NoBlock(int sock);
	
	//初始化连接池
	void InitConnections();
	
	//从连接池中获取一个空闲连接对象
	pConnection Get_connection(int sock);
	
	//用户连接事件处理函数
	void Event_accept(pConnection old);
	
	//将从连接池内申请的连接释放
	void Free_connection(pConnection cptr);
	
	//数据发送线程
	static void MessageStS_Thread(void* threadData);

	//数据转发线程
	static void MessageStU_Thread(void* threadData);

	//转发链接请求线程
	static void ConnectRequest_Thread(void* threadData);

	//关闭某个监听端口以及接入连接
	string Close_Socket(uint16_t port, const string& admin_id);

	//关闭某个用户开放的所有监听端口以及接入连接
	string Close_Sockets(const string& admin_id);

	string Show_Ports(string admin_id);

	//清空回收链接池
	void ClearConnections();

	//对数据的读处理函数
	bool Read_requestHandle(pConnection cptr);

	//对数据的发送处理函数
	void Write_requestHandle(pConnection cptr);

	//接收数据专用函数，返回接收到的数据大小
	ssize_t ReceiveProc(pConnection cptr, char* buffer, ssize_t buflen);

	//发送数据专用函数，返回接收到的数据大小
	bool SendProc(pConnection cptr, char* buffer, ssize_t buflen);

	//清空发送队列
	void ClearSendQueue();

	//检测连接是否产生了泛洪攻击
	bool TestFlood(pConnection cptr);

private:

	//初始化指令函数
	void Command_Init();

	//指令处理函数
	bool Handle_Command(pConnection pconn, char* pkgBodyInfo, unsigned short bodyLen);

	//*********************各种指令处理函数**********************

	//open: 打开端口
	string Comm_Open(string& active, string admin_id);

	//close: 关闭端口
	string Comm_Close(string& active, string admin_id);

	//show: 显示打开的端口
	string Comm_Show(string& active, string admin_id);

	//exit: 退出程序
	string Comm_Exit(string& active, string admin_id);

	//***********************************************************

	//命令响应提示信息发送
	void PromptMsg_Send(string strerr, pConnection pMsghdr, uint32_t msgCode);

private:

	in_port_t m_logicPort;						//任务处理逻辑监听端口
	in_port_t m_forwardPort;					//消息转发端口

	int m_work_connections;						//epoll最大连接数量
	int m_epollHandle;							//epoll_create返回句柄	
	struct epoll_event m_events[MAX_EVENTS];	//用于储存epoll行为

	list<pConnection>	m_connection;			//连接池链表
	list<pConnection> m_fconnection;			//连接池空闲链表
	mutex m_connectMutex;						//连接相关互斥量
	atomic<int> m_connection_all;				//所有连接数
	atomic<int> m_connection_free;				//可用空闲连接数
	atomic<int> m_connection_work;				//正在工作连接数

	list<char*>	m_StSQueue;					//发送消息队列
	condition_variable m_StSCond;				//发送队列条件变量
	mutex m_StSMutex;						//发包相关互斥量

	list<char*>	m_StUQueue;					//发送消息队列
	condition_variable m_StUCond;			//发送队列条件变量
	mutex m_StUMutex;						//发包相关互斥量

	int    m_MAX_Send;							//发送消息滞留的最大数量
	int	   m_MAX_rqSend;						//单个连接发送消息滞留的最大数量

	bool m_floodCheckStart;						//是否开启泛洪检查
	uint32_t m_floodInterval;					//泛洪检测允许间隔时间
	size_t m_floodPackages;						//泛洪检测所规定时间段内允许通过包个数

	vector<thread*> m_auxthread;				//用于储存辅助线程

public:

	unordered_map<string, pConnection> m_workconn;			//工作连接表 <ip, connect>
	
	unordered_map<uint16_t, OpenMember*> m_admin;			//管理员端口表 <msgid, socket>
	mutex m_memberLock;										//操控OpenMember时使用互斥锁

	unordered_map<string, pConnection> m_unfullyConnect;		//未完成转发连接表
	bool m_fullyGet;										//同步线程用

	pair<int, int> m_rangeOf;					//允许开放端口范围
};

#endif // !TCPSOCK

