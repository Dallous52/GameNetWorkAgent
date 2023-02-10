#include "TcpSock.h"
#include "Crc32.h"
#include "Error.h"
#include "ProfileCtl.h"


typedef string(MSocket::* CommandProc)(string& active, string admin_id);

static unordered_map<string, CommandProc> ProcFunc_Comm;


void MSocket::Command_Init()
{
	ProcFunc_Comm["open"] = &MSocket::Comm_Open;
	ProcFunc_Comm["close"] = &MSocket::Comm_Close;
	ProcFunc_Comm["show"] = &MSocket::Comm_Show;
	ProcFunc_Comm["exit"] = &MSocket::Comm_Exit;
}


bool MSocket::Handle_Command(pConnection pConn,
	char* pkgBodyInfo, unsigned short bodyLen)
{
	if (pkgBodyInfo == NULL)
	{
		return false;
	}

	lock_guard<mutex> lock(pConn->logicMutex);

	string tmpComm = pkgBodyInfo;
	string strerr;

	size_t finded = tmpComm.find(' ');
	if (finded == string::npos)
	{
		//指令错误，发送错误
		strerr = "Command format ERROR:> Please use : <command> <active>.";
		PromptMsg_Send(strerr, pConn, 0);
		return false;
	}

	string command, active;
	command.append(tmpComm, 0, finded);
	active.append(tmpComm, finded + 1, tmpComm.size() - finded - 1);

	if (command.empty() || active.empty())
	{
		strerr = "Command format ERROR:> Please use : <command> <active>.";
		PromptMsg_Send(strerr, pConn, 0);
		return false;
	}

	//调用指令处理函数
	if (ProcFunc_Comm.find(command) != ProcFunc_Comm.end())
		strerr = (this->*ProcFunc_Comm[command])(active, pConn->admin);
	else
		strerr = "Command format ERROR:> Command not found.";


	PromptMsg_Send(strerr, pConn, 0);

	return true;
}



string MSocket::Comm_Open(string& active, string admin_id)
{
	string strerr;

	size_t finded = active.find('/');
	if (finded == string::npos)
	{
		//指令错误，返回错误
		strerr = "Open error:> Please use <- open [tcp_or_udp]/[port] ->.";
		return strerr;
	}

	string type, port;
	type.append(active, 0, finded);
	port.append(active, finded + 1, active.size() - finded - (size_t)1);

	for (size_t i = 0; i < port.size(); i++)
	{
		if (port[i] > '9' || port[i] < '0')
		{
			strerr = "Open error:> Please enter the port with type [unsigned int].";
			return strerr;
		}
	}

	int tmpPort = stoi(port);

	if (tmpPort < m_rangeOf.first || tmpPort > m_rangeOf.second)
	{
		strerr = "Open error:> Please enter the port ranging from " +
			to_string(m_rangeOf.first) + " to " + to_string(m_rangeOf.second) + ".";
		return strerr;
	}

	string temperr;

	if (type == "tcp")
	{
		//开放并监听tcp连接代理端口
		temperr = Establish_listenSocket(tmpPort, admin_id);
		if (!temperr.empty())
		{
			strerr = "Open errror:> " + temperr;
			return strerr;
		}
	}
	else if (type == "udp")
	{
		//开放udp代理端口
		temperr = udpsock.Establish_Socket(tmpPort, admin_id);
		if (!temperr.empty())
		{
			strerr = "Open errror:> " + temperr;
			return strerr;
		}
	}
	else
	{
		strerr = "Open errror:> Please enter the socket type with 'tcp' or 'udp'.";
		return strerr;
	}

	strerr = "Open " + type + " port:> " + port + " success.";

	return strerr;
}


string MSocket::Comm_Close(string& active, string admin_id)
{
	string strerr;

	size_t finded = active.find('/');
	if (finded == string::npos)
	{
		//指令错误，返回错误
		strerr = "Close error:> Please use <- close [tcp_or_udp]/[port] ->.";
		return strerr;
	}

	string type, port;
	int tmpPort;
	type.append(active, 0, finded);
	port.append(active, finded + 1, active.size() - finded - (size_t)1);

	bool is_all = (port == "all");

	if (!is_all)
	{
		for (size_t i = 0; i < port.size(); i++)
		{
			if (port[i] > '9' || port[i] < '0')
			{
				strerr = "Close error:> Please enter the port with type [unsigned int] or 'all'.";
				return strerr;
			}
		}

		tmpPort = stoi(port);

		if (tmpPort < m_rangeOf.first || tmpPort > m_rangeOf.second)
		{
			strerr = "Close error:> Please enter the port ranging from " +
				to_string(m_rangeOf.first) + " to " + to_string(m_rangeOf.second) + ".";
			return strerr;
		}
	}

	string temperr;

	if (type == "tcp")
	{
		//关闭tcp连接代理端口
		if (is_all)
		{
			temperr = Close_Sockets(admin_id);
			if (!temperr.empty())
			{
				strerr = "Close error:> Close tcp port [" + port + "] FAILED, " + temperr;
				return strerr;
			}
		}
		else
		{
			temperr = Close_Socket(tmpPort, admin_id);
			if (!temperr.empty())
			{
				strerr = "Close error:> Close tcp port [" + port + "] FAILED, " + temperr;
				return strerr;
			}
		}
	}
	else if (type == "udp")
	{
		//关闭udp代理端口
		if (is_all)
		{
			temperr = udpsock.Close_Sockets(admin_id);
			if (!temperr.empty())
			{
				strerr = "Close error:> Close udp port [" + port + "] FAILED, " + temperr;
				return strerr;
			}
		}
		else
		{
			temperr = udpsock.Close_Socket(tmpPort, admin_id);
			if (!temperr.empty())
			{
				strerr = "Close error:> Close udp port [" + port + "] FAILED, " + temperr;
				return strerr;
			}
		}
	}
	else
	{
		strerr = "Close errror:> Please enter the socket type with 'tcp' or 'udp'.";
		return strerr;
	}

	strerr = "Close " + type + " port:> " + port + " success.";

	return strerr;
}


string MSocket::Comm_Show(string& active, string admin_id)
{
	string strerr;

	if (active == "tcp")
	{
		//显示tcp连接代理端口
		strerr = Show_Ports(admin_id);
		if (strerr.empty())                                                                                             
		{
			strerr = "Show errror:> Display all open tcp ports FAILED, no tcp port openned.";
			return strerr;
		}
	}
	else if (active == "udp")
	{
		//显示udp代理端口
		strerr = udpsock.Show_Ports(admin_id);
		if (strerr.empty())
		{
			strerr = "Show errror:> Display all open udp ports FAILED, no udp port openned.";
			return strerr;
		}
	}
	else if (active == "all")
	{
		//显示所有开放端口
		strerr = udpsock.Show_Ports(admin_id) + Show_Ports(admin_id);
		if (strerr.empty())
		{
			strerr = "Show errror:> Display all open udp ports FAILED, no port openned.";
			return strerr;
		}
	}
	else
	{
		strerr = "Show errror:> Please enter the socket type with 'tcp' or 'udp' or 'all'.";
		return strerr;
	}

	return strerr;
}


string MSocket::Comm_Exit(string& active, string admin_id)
{
	string strerr;

	unsigned int tmpTime;

	bool is_now = (active == "now");

	if (!is_now)
	{
		for (size_t i = 0; i < active.size(); i++)
		{
			if (active[i] > '9' || active[i] < '0')
			{
				strerr = "Exit error:> Please enter the port with type [unsigned int] or 'now'.";
				return strerr;
			}
		}

		tmpTime = stoi(active);

		if (tmpTime < 0 || tmpTime > 1800)
		{
			strerr = "Exit error:> Please enter the port ranging from 0 to 1800.";
			return strerr;
		}

		sleep(tmpTime);
	}

	Connection_Exit(admin_id);

	strerr = "Exited success.";

	return strerr;
}



void MSocket::PromptMsg_Send(string strerr, pConnection pConn, uint32_t msgCode)
{
	size_t sendsize = strerr.size() + 1;
	size_t connsize = pConn->connectId.size() + 1;
	char* sendBuf = new char[m_PKG_Hlen + connsize + sendsize];

	//拷贝消息头
	strcpy(sendBuf, pConn->connectId.c_str());

	PKG_Head* pkghdr = (PKG_Head*)(sendBuf + connsize);

	//填充包头
	pkghdr->mesgCode = htonl(msgCode);
	pkghdr->pkgLen = htons(m_PKG_Hlen + sendsize);

	//填充包体
	char* sendInfo = sendBuf + connsize + m_PKG_Hlen;

	strcpy(sendInfo, strerr.c_str());

	//计算crc值
	uint32_t crc = crc32((unsigned char*)sendInfo, (uint16_t)sendsize);
	pkghdr->crc32 = htonl(crc);

	//发送数据
	MsgSend_StS(sendBuf);
}