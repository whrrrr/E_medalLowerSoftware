#ifndef CRC_UTILS_H
#define CRC_UTILS_H

#include <stdint.h>

// CRC32配置结构
typedef struct {
    uint32_t polynomial;    // CRC多项式
    uint32_t initial_value; // 初始值
    uint32_t final_xor;     // 最终异或值
    uint8_t reflect_input;  // 是否反转输入
    uint8_t reflect_output; // 是否反转输出
} crc32_config_t;

// 标准CRC32配置（IEEE 802.3）
extern const crc32_config_t CRC32_IEEE;

/**
 * @brief 计算CRC32校验值
 * @param data 数据指针
 * @param length 数据长度
 * @param config CRC配置指针，如果为NULL则使用默认配置
 * @return uint32_t CRC32值
 */
uint32_t calculate_crc32(const uint8_t* data, uint32_t length, const crc32_config_t* config);

/**
 * @brief 计算CRC32校验值（使用默认配置）
 * @param data 数据指针
 * @param length 数据长度
 * @return uint32_t CRC32值
 */
uint32_t calculate_crc32_default(const uint8_t* data, uint32_t length);

#endif // CRC_UTILS_H
