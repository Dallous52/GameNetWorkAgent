#include "Process.h"
#include "Error.h"
#include "Manage.h"
#include "ProfileCtl.h"
#include "Crc32.h"

pid_t this_pid = getpid();
pid_t this_ppid = getppid();

MThreadPool mthreadpool;


ProcessCtl::ProcessCtl()
{
	;
}


void ProcessCtl::SetProcName(string name)
{
	size_t size = 0;
	for (int i = 0; m_argv[i]; i++)
	{
		size += strlen(m_argv[i]) + 1;
	}

	//保证更换完名字后之后所有的内存都为0
	if (name.size() >= size)
	{
		strcpy(m_argv[0], name.c_str());
	}
	else
	{
		strcpy(m_argv[0], name.c_str());
		char* set = m_argv[0] + name.size();

		memset(set, 0, size - name.size());
	}
}


void ProcessCtl::Major_Create_WorkProc()
{
	//创建信号集
	sigset_t sig;
	
	//置空信号集
	sigemptyset(&sig);

	//向信号集中添加信号
	sigaddset(&sig, SIGQUIT);//终端退出符
	sigaddset(&sig, SIGCHLD);//子进程状态改变
	sigaddset(&sig, SIGALRM);//定时器超时
	sigaddset(&sig, SIGIO);//异步io
	sigaddset(&sig, SIGINT);//终端中断符
	sigaddset(&sig, SIGHUP);//连接断开
	sigaddset(&sig, SIGUSR1);//用户自定义信号1
	sigaddset(&sig, SIGUSR2);//用户自定义信号2
	sigaddset(&sig, SIGTERM);//终止
	sigaddset(&sig, SIGWINCH);//终端窗口大小改变

	//下面这个函数将第二个参数的信号集配置到当进程的屏蔽信号集上
	//并将当前进程原本信号集的内容保存在第三个参数(这里不保存)
	if (sigprocmask(SIG_BLOCK, &sig, NULL) < 0)
	{
		Error_insert_File(LOG_ERR, "system shielding signal seting failed");
	}
	//就算创建失败也不中断程序程序

	Create_WorkProc_ForNums();//创建子进程

	SetProcName("mast process :");
	//设置主进程名

	sigemptyset(&sig);

	while (true)
	{
		//sleep(1);

		sigsuspend(&sig);
		//用于在信号被屏蔽时接收信号
		//调用时进程挂起，不占用cpu时间

		Error_insert_File(LOG_DEBUG, "this is mast process");
		
		//cout << "you are successed" << endl;
	}
}


void ProcessCtl::Create_WorkProc_ForNums()
{
	int num = stoi(findProVar("work_process_num"));
	//从配置文件中获取进数
	
	for (int i = 0; i < num; i++)
	{
		Create_WorkProc(i);
		//按进程数创建子进程
	}

	return;
}


int ProcessCtl::Create_WorkProc(int ordNum)
{
	pid_t pid;

	pid = fork();
	
	switch (pid)//创建子进程，子进程返回0创建成功，父进程返回子进程PID
	{	
	case -1:
		Error_insert_File(LOG_SERIOUS, "Ord %d: Child process %d creation failed", ordNum, getpid());
		//子进程创建失败记录日志
		return -1;
		
	case 0:
		//子进程创建完成,设置参数
		this_ppid = this_pid;
		this_pid = getpid();

		WorkProc_Cycle(ordNum);
		break;

	default:
		//父进程返回执行后续
		break;;
	}

	return pid;
}


void ProcessCtl::WorkProc_Cycle(int ordNum)
{
	WorkProcess_Init(ordNum);

	//子进程标志
	this_process = PROCESS_CHILD;

	//设置子进程名
	string childname = "work process (" + to_string(ordNum) + ')';
	SetProcName(childname);

	//创建辅助线程
	thread portCtl(&ProcessCtl::UserWork_Cycle, this);

	//记录启动日志
	Error_insert_File(LOG_NOTICE, "%s is start and running, pid is %d.", childname.c_str(), this_pid);

	while (true)
	{
		Process_Events_Timer();

		//cerr << "child process" << ordNum << endl;
		//Error_insert_File(LOG_DEBUG, "this is child process");
	}

	msocket.WrokProcRelevent_Off();
	mthreadpool.StopAll();
	
	portCtl.join();	

	Error_insert_File(LOG_NOTICE, "%s is shutdown, pid is %d.", childname.c_str(), this_pid);

	return;
}


void ProcessCtl::WorkProcess_Init(int ordNum)
{
	sigset_t sig;

	sigemptyset(&sig);

	if (sigprocmask(SIG_BLOCK, &sig, NULL) < 0)//放开接收所有信号
	{
		Error_insert_File(LOG_ERR, "work process %d: system shielding signal seting failed.", ordNum);
	}

	int threadNum = stoi(findProVar("thread_num"));
	
	//监听端口初始化
	if (msocket.Initialize() == false)
	{
		Error_insert_File(LOG_URGENT,
			"LogicSocket Listening port initialization failed.");
		exit(-2);
	}
	else
	{
		Error_insert_File(LOG_NOTICE,
			"LogicSocket Listening port initialization success.");
	}

	//创建线程池
	if (!mthreadpool.Create(threadNum))
	{
		exit(-2);
	}

	//工作进程相关量初始化
	if (!msocket.WrokProcRelevent_Init())
	{
		Error_insert_File(LOG_FATAL, 
			"Work process init :> work LogicSocket process relevent init failed.");
		exit(-2);
	}

	return;
}


//处理网络事件与定时器事件
void ProcessCtl::Process_Events_Timer()
{
	msocket.epoll_process_events(-1);

	//........
}


ProcessCtl::~ProcessCtl()
{
	;
}
