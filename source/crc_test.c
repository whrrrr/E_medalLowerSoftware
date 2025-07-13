#include "crc_utils.h"
#include "uart_interface.h"
#include <string.h>

/**
 * @brief 测试CRC工具模块
 */
void test_crc_utils(void)
{
    uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint32_t crc1, crc2;
    
    // 测试默认配置
    crc1 = calculate_crc32_default(test_data, sizeof(test_data));
    
    // 测试使用显式配置
    crc2 = calculate_crc32(test_data, sizeof(test_data), &CRC32_IEEE);
    
    // 两个结果应该相同
    if (crc1 == crc2) {
        UARTIF_uartPrintf(0, "CRC test PASSED: 0x%08X\n", crc1);
    } else {
        UARTIF_uartPrintf(0, "CRC test FAILED: 0x%08X != 0x%08X\n", crc1, crc2);
    }
    
    // 测试空数据
    crc1 = calculate_crc32_default(NULL, 0);
    UARTIF_uartPrintf(0, "Empty data CRC: 0x%08X\n", crc1);
    
    // 测试单字节数据
    uint8_t single_byte = 0xAA;
    crc1 = calculate_crc32_default(&single_byte, 1);
    UARTIF_uartPrintf(0, "Single byte (0xAA) CRC: 0x%08X\n", crc1);
}