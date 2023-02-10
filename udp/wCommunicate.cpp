#include "UdpSock.h"

string WorkSock::Show_Ports(string admin_id)
{
	string ret;
	size_t i = 0;

	if (m_workconn.empty())
		return ret;

	for (auto& tmp : m_workconn)
	{
		if (tmp.second->admin_id == admin_id)
		{
			ret += "udp\t" + tmp.second->opentime + '\t' + to_string(tmp.first) + '\n';
		}

		if (++i == m_workconn.size())
			break;
	}

	return ret;
}