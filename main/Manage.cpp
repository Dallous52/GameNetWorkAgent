#include "Manage.h"
#include "Error.h"
#include "ProfileCtl.h"

//存储配置文件参数
vector<Profile_Var> GloVar;

//配置文件注释字符集
vector<char> note = { '#','[','\n' };

void Provar_init()
{
	ifstream file;

	file.open(File_Profile_Ctl, ios::in);

	if (!file.is_open())
	{
		Error_insert_File(LOG_FATAL, "open profile failed:> %s.", strerror(errno));
		exit(0);
	}

	//配置文件变量初始化
	char* test = new char[401];
	while (file.getline(test, 400))
	{
		vector<string> ansed = GetConfig_Name_Num(test, note);

		if (ansed.size())
		{
			Profile_Var aux;
			aux.name = ansed[0];
			aux.var = ansed[1];

			GloVar.push_back(aux);
		}
	}
}


void Show_ProFile_var()
{
	for (size_t i = 0; i < GloVar.size(); i++)
	{
		cout << GloVar[i].name << " = " << GloVar[i].var << endl;
	}
}


void MoveEnviron()
{
	size_t size = 0;

	//获取environ的元素个数
	int i;
	for (i = 0; environ[i]; i++);

	char** newenv = new char* [i + 1];
	newenv[i] = NULL;

	//拷贝
	for (int j = 0; environ[j]; j++)
	{
		size_t lenth = strlen(environ[j]);
		size += lenth + 1;//计算environ占用字节数
		newenv[j] = new char[lenth + 1];
		memcpy(newenv[j], environ[j], lenth);
		newenv[j][lenth] = '\0';
	}

	memset(environ[0], 0, size);//将旧内存全部设置为0

	environ = newenv;
	newenv = NULL;

	return;
}