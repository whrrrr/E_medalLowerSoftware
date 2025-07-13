#include "global_buffers.h"
#include <string.h>

// 全局缓冲区定义 - 用于替换大型局部数组，避免栈溢出
uint8_t g_general_buffer_256[256];    // 通用256字节缓冲区
// uint8_t g_general_buffer_248[248];    // 通用248字节缓冲区  
uint8_t g_general_buffer_200[200];    // 通用200字节缓冲区

/**
 * @brief 清零指定缓冲区
 * @param buffer 缓冲区指针
 * @param size 缓冲区大小
 */
void clear_buffer(uint8_t* buffer, uint16_t size)
{
    if (buffer != NULL && size > 0) {
        memset(buffer, 0, size);
    }
}
