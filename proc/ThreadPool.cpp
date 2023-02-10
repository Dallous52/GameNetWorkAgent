#include "ThreadPool.h"
#include "Error.h"
#include "Manage.h"

//初始化全局变量
bool MThreadPool::m_isExit = false;


MThreadPool::MThreadPool()
{
	m_threadRunning = 0;
	
	m_oldTime = 0;
}


//线程创建
bool MThreadPool::Create(int num)
{
	ThreadItem* newthread;

	m_CreateNum = num;

	for (int i = 0; i < m_CreateNum; i++)
	{
		newthread = new	ThreadItem(this);
		m_threadpool.push_back(newthread);
		
		//调用系统函数创建线程		ThreadFunc为线程入口函数
		newthread->Handle = new thread(&MThreadPool::ThreadFunc, newthread);
	}//end for

	return true;
}


void MThreadPool::GetMassage_And_Proc(char* buffer)
{
	//启用互斥锁
	m_pMutex.lock();

	m_MsgQueue.push_back(buffer);
	
	++m_MsgQueueCount;

	//取消互斥锁
	m_pMutex.unlock();

	Call();

	return;
}


void* MThreadPool::ThreadFunc(void* threadData)
{
	ThreadItem* auxThread = static_cast<ThreadItem*>(threadData);
	MThreadPool* cpthreadpool = auxThread->pThis;
	//用于取得指向本线程池类的指针(因为是静态成员函数无法使用 this指针)

	while (true)
	{
		unique_lock<mutex>	ulock(cpthreadpool->m_pMutex);
		//用于控制线程在接收消息时启动，处理完成后根据消息队列是否为空自动判断是否执行
		while (cpthreadpool->m_MsgQueue.size() == 0 && m_isExit == false)
		{
			cpthreadpool->m_pCond.wait(ulock);
			//若不满足条件，线程将卡在此函数中等待
		}

		//如果进程正在运行时需要退出
		//m_isExit条件会为true
		if (m_isExit)
		{
			ulock.unlock();
			break;
		}

		//线程满足运行条件准备运行
		char* workbuf = cpthreadpool->m_MsgQueue.front();
		cpthreadpool->m_MsgQueue.pop_front();
		--cpthreadpool->m_MsgQueueCount;

		ulock.unlock();
		
		//运行到此说明线程可以进行数据处理
		//线程运行数原子量+1
		++cpthreadpool->m_threadRunning;

		//对消息进行处理
		msocket.ThreadReceive_Proc(workbuf);

		delete[]workbuf;
		workbuf = NULL;

		--cpthreadpool->m_threadRunning;

	}//end while
	
	return (void*)0;
}


void MThreadPool::StopAll()
{
	//若程序已结束直接退出
	if (m_isExit)
	{
		return;
	}

	m_isExit = true;

			
	//唤醒由cpthreadpool->m_pCond.wait(ulock);卡着的所有线程
	m_pCond.notify_all();

	vector<ThreadItem*>::iterator it;
	for (it = m_threadpool.begin(); it != m_threadpool.end(); it++)
	{
		(*it)->Handle->join();
		//等待一个线程终止
	}
	
	//释放线程所占空间
	for (it = m_threadpool.begin(); it != m_threadpool.end(); it++)
	{
		if ((*it) != NULL)
		{
			if ((*it)->Handle != NULL)
			{
				delete (*it)->Handle;
				(*it)->Handle = NULL;
			}
			delete (*it);
			(*it) = NULL;
		}
	}

	m_threadpool.clear();

	Error_insert_File(LOG_NOTICE, "All threads exit normally.");

	return;
}


void MThreadPool::Call()
{
	m_pCond.notify_one();
	//上述函数用于唤醒一个等待m_pCond的进程

	if (m_CreateNum == m_threadRunning)
	{
		//正在运行的线程数 = 线程总数
		//线程数量不足
		time_t auxtime = time(NULL);
		if (auxtime - m_oldTime > 10)
		{
			m_oldTime = auxtime;

			Error_insert_File(LOG_CRIT, 
				"thread call:> Insufficient threads, please expand the thread pool.");
		}
	}

	return;
}


void MThreadPool::ClearMsgQueue()
{
	char* auxPoint;

	while (!m_MsgQueue.empty())
	{
		auxPoint = m_MsgQueue.front();
		m_MsgQueue.pop_front();

		delete[]auxPoint;
	}
}


MThreadPool::~MThreadPool()
{
	//清理消息队列遗留
	ClearMsgQueue();
}