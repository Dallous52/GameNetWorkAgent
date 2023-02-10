#ifndef Process
#define Process

#include "Globar.h"
#include "ThreadPool.h"

extern MThreadPool mthreadpool;

//创建守护进程
int Create_daemon();

class ProcessCtl
{
public:

	ProcessCtl();

	~ProcessCtl();

	//创建子进程主体函数
	void Major_Create_WorkProc();
	
	//更改进程名
	void SetProcName(string name);

private:
	//子进程相关函数

	//创建子进程
	int Create_WorkProc(int ordNum);

	//按配置文件参数创建子进程
	void Create_WorkProc_ForNums();

	//子进程工作环境
	void WorkProc_Cycle(int ordNum);

	//子进程初始化
	void WorkProcess_Init(int ordNum);

	//处理子进程网络事件与定时器事件
	void Process_Events_Timer();

private:
	//用户次级工作循环函数

	//次级工作循环线程主体
	static void UserWork_Cycle(void* threadData);

	//次级工作循环初始化
	bool UserThread_Init();
};

#endif // !Process

