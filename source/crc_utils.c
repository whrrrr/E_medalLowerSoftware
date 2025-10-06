#include "crc_utils.h"
#include "flash_config.h"

// 标准CRC32配置（IEEE 802.3）
const crc32_config_t CRC32_IEEE = {
    CRC32_POLYNOMIAL,     // 0xEDB88320
    0xFFFFFFFF,
    0xFFFFFFFF,
    0,
    0
};

/**
 * @brief 计算CRC32校验值
 * @param data 数据指针
 * @param length 数据长度
 * @param config CRC配置指针，如果为NULL则使用默认配置
 * @return uint32_t CRC32值
 */
uint32_t calculate_crc32(const uint8_t* data, uint32_t length, const crc32_config_t* config)
// uint32_t calculate_crc32(uint8_t* data, uint32_t length, const crc32_config_t* config)
{
    uint32_t crc;
    uint32_t i, j;
    uint32_t polynomial;
    uint32_t initial_value;
    uint32_t final_xor;
    
    // 如果没有提供配置，使用默认配置
    if (config == NULL) {
        config = &CRC32_IEEE;
    }
    
    polynomial = config->polynomial;
    initial_value = config->initial_value;
    final_xor = config->final_xor;
    
    crc = initial_value;
    
    for (i = 0; i < length; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ polynomial;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc ^ final_xor;
}

/**
 * @brief 计算CRC32校验值（使用默认配置）
 * @param data 数据指针
 * @param length 数据长度
 * @return uint32_t CRC32值
 */
// uint32_t calculate_crc32_default(uint8_t* data, uint32_t length)
uint32_t calculate_crc32_default(const uint8_t* data, uint32_t length)
{
    return calculate_crc32(data, length, NULL);
}
