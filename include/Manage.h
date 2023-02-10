#ifndef Manage
#define Manage

#include "Globar.h"
#include "TcpSock.h"

extern MSocket msocket;

//导入配置文件参数
void Provar_init();

//显示所有配置文件参数
void Show_ProFile_var();

//更改environ环境变量位置
void MoveEnviron();

#endif
