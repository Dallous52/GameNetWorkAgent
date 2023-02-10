#ifndef UDPSOCK
#define UDPSOCK

#include "Globar.h"

//服务器可以积压未处理完的连接请求个数
#define  BackLogConn  512							  

//事件最大处理数
#define MAX_EVENTS	  512

#define _PKG_MAX_LENGTH 30000						  //每个包最大长度

typedef struct uConnection  uConn, *pConn_u;	//指向连接信息结构指针

typedef class WorkSock WorkSock;

typedef void (WorkSock::* UEvent_handle)(pConn_u head);


typedef struct RegistInfo 
{
	sockaddr_in addr;		//客户端信息
	uint32_t msgid;			//对应的消息id
	int64_t timed;			//最后一次发包时间
	in_port_t port;			//注册消息端口号

}RegistInfo;


//连接池内连接结构
struct uConnection
{
	//******初始化与释放******
	void GetOne();					//当获取或创建一个连接时执行此函数初始化成员
	void FreeOne();					//当要释放一个函数时调用此函数
	//************************


	//******连接基本信息******
	int fd;							//套接字句柄
	uint16_t port;					//由本链接开放的端口

	uint32_t events;				//本连接的事件处理类型
	UEvent_handle rHandle;			//读事件处理相关函数

	string opentime;				//本连接开放时间

	unordered_map<string, RegistInfo*> csinfo;		//用户连接表 <ip, regiest>
	unordered_map<uint32_t, string> auxinfo;		//辅助连接表 <msgid, ip>
	uint32_t msg_id;								//用户消息id，用于查找返回链接

	string admin_id;				//请求打开本链接的管理员id
	//*************************


	//********收包有关*********
	char* bufptr;					//接收缓冲区
	ssize_t recvLen;				//接收到数据的长度
	//*************************


	//**********其他***********
	time_t recyTime;				//连接开始回收时间
};


//网络UDP信息处理类
class WorkSock
{
public:

	WorkSock();

	//加virtual可在其子类中实现对目标函数的覆盖
	virtual ~WorkSock();

public:

	//工作进程相关量初始化
	virtual bool WrokProcRelevent_Init();

	//epoll事件处理函数
	int epoll_process_events(int time);

	//工作进程相关关闭回收函数
	void  WrokProcRelevent_Off();

	//将一个待发送的数据放入消息队列等待发送
	void MsgSend(char* sendBuf);
	
	//将一个待转发的数据放入转发队列等待发送
	void MsgForward(char* sendBuf);

	//主动关闭一个连接
	void ActiveShutdown(pConn_u cptr);

	//用于用户包接收完与处理
	void UserRequest_Handle(pConn_u cptr);

	//用于转发包接收与处理
	void ForwardRequest_Handle(pConn_u cptr);

	//初始化连接池
	void InitConnections();

	//删除注册信息
	void RegiestInfo_Delete(string& adminId);

public:
	//外接控制指令

	//关闭所有正在工作端口
	string Close_Sockets(string admin_id);

	//根据用户指令关闭端口
	string Close_Socket(uint16_t port, string admin_id);

	//根据用户指令建立端口
	string Establish_Socket(uint16_t port, string admin_id);

	//显示所有开放的端口
	string Show_Ports(string admin_id);

private:

	//增加epoll事件
	int Epoll_Add_event(int sock, uint32_t enventTp,
		uint32_t otherflag, int addAction, pConn_u cptr);

	//从连接池中获取一个空闲连接对象
	pConn_u Get_connection(int sock);

	//将从连接池内申请的连接释放
	void Free_connection(pConn_u cptr);

	//数据发送线程
	static void* MessageSend_Thread(void* threadData);

	//无用连接删除线程
	static void UselessDelete_Thread(void* threadData);

	//维护连接心跳包
	static void HeartPackage_Check_Send(void* threadData);

	//数据转发线程
	static void MessageForward_thread(void* threadData);

	//将转发消息封装放入消息队列
	void Packaging_Send(char* buffer, size_t recvLen, string admin_id, uint32_t msgid, uint32_t port);

	//清空回收链接池
	void ClearConnections();

	//对数据的读处理函数
	RegistInfo* Read_requestHandle(pConn_u cptr);

	//清空发送队列
	void ClearSendQueue();

private:
	
	list<pConn_u>	m_connection;			//连接池链表
	list<pConn_u>	m_fconnection;			//连接池空闲链表
	mutex m_connectMutex;					//连接互斥量

	atomic<int> m_connection_all;			//总连接数
	atomic<int> m_connection_free;			//可用空闲连接数

	vector<RegistInfo*>	m_timeCheckQueue;	//用户连接检测队列
	mutex m_idCheckLock;					//用户连接检测互斥锁

	chrono::milliseconds m_ms;				//存储时间结构（ms）
	chrono::system_clock::time_point m_now;	//获取时间结构

	vector<RegistInfo*>	m_heartQueue;		//心跳包检测队列
	mutex m_heartLock;						//心跳包检测互斥锁

	uint32_t m_ForwardPort;				//消息转发端口

	unordered_map<uint32_t, pConn_u>	m_workconn;		//已使用连接队列 <端口号, 连接>
	mutex m_closeLock;									//关闭连接时使用互斥锁

	int m_work_connections;						//epoll最大连接数量
	int m_epollHandle;							//epoll_create返回句柄	
	struct epoll_event m_events[MAX_EVENTS];	//用于储存epoll行为

	vector<thread*> m_auxthread;			//用于储存辅助线程

	size_t m_MAX_Send;						//发送消息滞留的最大数量

	list<char*>	m_sendQueue;				//发送消息队列
	mutex m_sendMsgMutex;					//发包相关互斥量
	condition_variable m_SendCond;			//发送队列条件变量

	list<char*>	m_forwardQueue;				//转发消息队列
	mutex m_forwardMsgMutex;				//转发相关互斥量
	condition_variable m_forwardCond;		//转发队列条件变量
};


#endif // !UDPSOCK

