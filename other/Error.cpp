#include "Error.h"
#include "ProfileCtl.h"

void Error_insert_File(int errLev, const char* error, ...)
{
	//获取可变参数
	va_list vapa;
	va_start(vapa, error);
	
	int fail_lev = stoi(findProVar("out_error_level"));
	//从配置文件获取输出错误等级
	
	string errfile = findProVar("log_addr");

	char* buf = nullptr;
	vasprintf(&buf, error, vapa);

	int log = open(errfile.c_str(), O_WRONLY | O_APPEND);

	if (log == -1)//日志打开失败处理
	{
		log = open(errfile.c_str(), O_WRONLY | O_CREAT, 0600);

		if (log == -1)
		{
			cerr << "log file open failed : " << strerror(errno) << endl;
			return;
		}
	}

	string currTime = GetTime();

	//错误信息输入日志文件
	if (errLev >= 0 && errLev < ERR_NUM)
	{
		if (errLev < fail_lev)//判断是否输出与屏幕
		{
			cerr << currTime << " [" << ErrorInfo[errLev] << "] " << buf << endl;
		}

		string err = currTime + " [" + ErrorInfo[errLev] + "] " + buf + "\n";
		//********记得加时间
		write(log, err.c_str(), err.size());
	}

	int er = close(log);
	if (er == -1)
	{
		cerr << "log file close failed : " << strerror(errno) << endl;
	}
}


string GetTime()
{
	string date;
	time_t times;
	struct tm* timed;
	char ansTime[50];

	time(&times); //获取从1900至今过了多少秒，存入time_t类型的timep
	timed = localtime(&times);//用localtime将秒数转化为struct tm结构体

	sprintf(ansTime, "%d-%02d-%02d %02d:%02d:%02d", 1900 + timed->tm_year, 1 + timed->tm_mon,
		timed->tm_mday, timed->tm_hour, timed->tm_min, timed->tm_sec);

	date = ansTime;

	return date;
}