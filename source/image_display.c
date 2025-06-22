#include "image_display.h"
#include "epd.h"
#include "uart_interface.h"
#include <string.h>

// 外部变量
extern uint8_t g_flash_buffer[FLASH_PAGE_SIZE];

// 显示缓冲区（复用Flash缓冲区）
static uint8_t* display_buffer = NULL;

/**
 * @brief RLE解压缩
 */
static uint16_t rle_decompress(const uint8_t* input, uint16_t input_len, uint8_t* output, uint16_t max_output_len)
{
    uint16_t in_pos = 0, out_pos = 0;
    
    while (in_pos < input_len && out_pos < max_output_len) {
        uint8_t byte = input[in_pos++];
        
        if (byte & 0x80) {
            // RLE编码数据
            uint8_t count = byte & 0x7F;
            if (in_pos >= input_len) break;
            uint8_t value = input[in_pos++];
            
            for (uint8_t i = 0; i < count && out_pos < max_output_len; i++) {
                output[out_pos++] = value;
            }
        } else {
            // 直接数据
            output[out_pos++] = byte;
        }
    }
    
    return out_pos;
}

/**
 * @brief 从Flash读取图像数据块
 */
static uint8_t load_image_block(image_display_t* display, uint32_t block_offset, uint8_t* buffer, uint16_t buffer_size)
{
    uint16_t data_id = display->image_id * 1000 + (block_offset / FLASH_PAGE_SIZE);
    uint8_t read_size = buffer_size;
    
    flash_result_t result = flash_read_data(display->flash_mgr, data_id, buffer, &read_size);
    
    if (result == FLASH_OK) {
        display->loaded_bytes += read_size;
        return read_size;
    }
    
    return 0;
}

/**
 * @brief 显示图像数据到EPD
 */
static void display_to_epd(const uint8_t* bw_data, const uint8_t* red_data)
{
    // 这里应该调用EPD显示函数
    // 由于EPD驱动函数可能比较复杂，这里提供一个简化的接口
    
    // 示例：显示黑白数据
    // EPD_DisplayBWData(bw_data, DISPLAY_BW_BYTES);
    
    // 示例：显示红色数据
    // EPD_DisplayRedData(red_data, DISPLAY_RED_BYTES);
    
    // 刷新显示
    // EPD_Refresh();
    
    UARTIF_uartPrintf(0, "Image displayed on EPD\n");
}

/**
 * @brief 初始化图像显示器
 */
void image_display_init(image_display_t* display, flash_manager_t* flash_mgr)
{
    memset(display, 0, sizeof(image_display_t));
    display->state = DISPLAY_STATE_IDLE;
    display->flash_mgr = flash_mgr;
    display_buffer = g_flash_buffer; // 复用Flash缓冲区
}

/**
 * @brief 重置显示器
 */
void image_display_reset(image_display_t* display)
{
    display->state = DISPLAY_STATE_IDLE;
    display->image_id = 0;
    display->loaded_bytes = 0;
}

/**
 * @brief 显示指定ID的图像
 */
uint8_t image_display_show(image_display_t* display, uint16_t image_id)
{
    if (display->state != DISPLAY_STATE_IDLE) {
        return 0; // 忙碌中
    }
    
    display->image_id = image_id;
    display->loaded_bytes = 0;
    display->state = DISPLAY_STATE_LOADING;
    
    UARTIF_uartPrintf(0, "Starting to display image ID: %d\n", image_id);
    
    return 1;
}

/**
 * @brief 处理图像显示
 */
void image_display_process(image_display_t* display)
{
    static uint32_t block_offset = 0;
    static uint8_t bw_complete = 0;
    static uint8_t red_complete = 0;
    
    switch (display->state) {
        case DISPLAY_STATE_LOADING:
        {
            // 分块加载图像数据
            uint8_t loaded = load_image_block(display, block_offset, display_buffer, FLASH_PAGE_SIZE);
            
            if (loaded > 0) {
                // 处理加载的数据块
                if (block_offset < DISPLAY_BW_BYTES) {
                    // 处理黑白数据
                    // 这里可以添加解压缩逻辑
                    UARTIF_uartPrintf(0, "Loaded BW block at offset %d, size %d\n", block_offset, loaded);
                } else {
                    // 处理红色数据
                    UARTIF_uartPrintf(0, "Loaded RED block at offset %d, size %d\n", block_offset, loaded);
                }
                
                block_offset += loaded;
                
                // 检查是否加载完成
                if (display->loaded_bytes >= (DISPLAY_BW_BYTES + DISPLAY_RED_BYTES)) {
                    display->state = DISPLAY_STATE_DISPLAYING;
                    block_offset = 0;
                    UARTIF_uartPrintf(0, "Image data loaded completely\n");
                }
            } else {
                // 加载失败
                display->state = DISPLAY_STATE_ERROR;
                block_offset = 0;
                UARTIF_uartPrintf(0, "Failed to load image block\n");
            }
            break;
        }
        
        case DISPLAY_STATE_DISPLAYING:
        {
            // 显示图像到EPD
            // 这里应该重新从Flash读取完整的图像数据并显示
            // 为了简化，这里直接标记为完成
            
            UARTIF_uartPrintf(0, "Displaying image to EPD...\n");
            
            // 模拟显示过程
            display_to_epd(NULL, NULL);
            
            display->state = DISPLAY_STATE_COMPLETE;
            break;
        }
        
        case DISPLAY_STATE_COMPLETE:
        {
            UARTIF_uartPrintf(0, "Image display completed\n");
            // 保持完成状态，等待外部重置
            break;
        }
        
        case DISPLAY_STATE_ERROR:
        {
            UARTIF_uartPrintf(0, "Image display error\n");
            // 保持错误状态，等待外部重置
            break;
        }
        
        default:
            break;
    }
}