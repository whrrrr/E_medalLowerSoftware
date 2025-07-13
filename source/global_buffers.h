#ifndef GLOBAL_BUFFERS_H
#define GLOBAL_BUFFERS_H

#include <stdint.h>

/**
 * @file global_buffers.h
 * @brief 全局缓冲区声明文件
 * 
 * 为了避免在函数中定义大型局部数组导致栈溢出，
 * 本文件定义了多个全局缓冲区供各模块复用。
 * 
 * 注意事项：
 * 1. 这些缓冲区不是线程安全的，使用时需要确保不会并发访问
 * 2. 使用完毕后建议清零缓冲区内容
 * 3. 优先使用较小的缓冲区，避免浪费内存
 */

// 全局缓冲区声明
extern uint8_t g_general_buffer_256[256];    // 通用256字节缓冲区
// extern uint8_t g_general_buffer_248[248];    // 通用248字节缓冲区  
// extern uint8_t g_general_buffer_200[200];    // 通用200字节缓冲区

/**
 * @brief 清零指定缓冲区
 * @param buffer 缓冲区指针
 * @param size 缓冲区大小
 */
void clear_buffer(uint8_t* buffer, uint16_t size);

#endif // GLOBAL_BUFFERS_H

