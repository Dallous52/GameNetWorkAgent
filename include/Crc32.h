#ifndef Crc32
#define Crc32

#include <stdint.h>
#include "UdpSock.h"

extern WorkSock udpsock;

extern "C" 
{
	//Crc32校验值计算处理函数
	uint32_t crc32(const unsigned char* s, uint16_t len);

	//Crc32多项式校验函数
	uint32_t crc32_poly(uint32_t poly, uint16_t value);
}

#endif // !Crc32

