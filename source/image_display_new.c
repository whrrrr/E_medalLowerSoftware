#include "image_display_new.h"
#include "w25q32.h"
#include "epd.h"
#include "uart_interface.h"
#include "flash_manager.h"
#include "global_buffers.h"
#include "gpio.h"
#include "spi.h"
#include "crc_utils.h"
#include <string.h>

// 移除大型全局缓冲区，改为逐页处理以节省内存

// 外部变量
extern uint8_t g_flash_buffer[FLASH_PAGE_SIZE];

// 静态函数声明
static flash_result_t display_image_from_flash_streaming(image_display_new_t* display);
static flash_result_t verify_image_data_page(const uint8_t* page_data, uint8_t expected_magic, uint8_t expected_seq);
static flash_result_t send_bw_image_to_epd(image_display_new_t* display);
static flash_result_t send_red_image_to_epd(image_display_new_t* display);

/**
 * @brief 初始化图像显示器
 */
flash_result_t image_display_new_init(image_display_new_t* display, flash_manager_t* flash_mgr)
{
    if (!display || !flash_mgr) {
        return FLASH_ERROR_INVALID_PARAM;
    }
    
    memset(display, 0, sizeof(image_display_new_t));
    display->flash_mgr = flash_mgr;
    display->state = DISPLAY_STATE_IDLE;
    display->display_buffer = g_flash_buffer;  // 复用Flash缓冲区
    
    return FLASH_OK;
}

/**
 * @brief 重置图像显示器
 */
void image_display_new_reset(image_display_new_t* display)
{
    if (!display) return;
    
    display->state = DISPLAY_STATE_IDLE;
    display->current_page = 0;
    display->buffer_pos = 0;
    display->processed_bytes = 0;
    memset(display->bw_pages_addresses, 0, sizeof(display->bw_pages_addresses));
    memset(display->red_pages_addresses, 0, sizeof(display->red_pages_addresses));
}

/**
 * @brief 查找指定槽位的图像头页
 */
flash_result_t find_image_headers_by_slot(image_display_new_t* display, uint8_t slot, 
                                         uint16_t* bw_header_id, uint16_t* red_header_id)
{
    // 简化实现：假设头页ID按槽位分配
    // 实际实现中应该遍历Flash查找对应槽位的头页
    
    // 这里使用简单的ID分配策略
    // 槽位0: BW=0x1000, RED=0x1001
    // 槽位1: BW=0x1002, RED=0x1003
    // ...
    
    *bw_header_id = 0x1000 + (slot * 2);
    *red_header_id = 0x1000 + (slot * 2) + 1;
    
    UARTIF_uartPrintf(0, "Finding image headers for slot %d: BW=0x%04X, RED=0x%04X\n", 
                     slot, *bw_header_id, *red_header_id);
    
    return FLASH_OK;
}

/**
 * @brief 从Flash加载图像头页
 */
flash_result_t load_image_header(image_display_new_t* display, uint16_t header_id, uint8_t is_bw_header)
{
	    uint32_t calculated_crc;
    uint32_t* target_addresses;
    int i;
    

    uint16_t read_size;
    flash_result_t result;
        image_header_page_t* header;
    // 通过Flash管理器查找头页
    read_size = FLASH_PAGE_SIZE;
    result = flash_read_data(display->flash_mgr, header_id, g_flash_buffer, &read_size);
    
    if (result != FLASH_OK) {
        UARTIF_uartPrintf(0, "Failed to read image header: ID=0x%04X, error=%d\n", header_id, result);
        return result;
    }
    
    // 解析头页

    header = (image_header_page_t*)g_flash_buffer;
    
    // 验证魔法数
    if (header->magic != MAGIC_IMAGE_HEADER) {
        UARTIF_uartPrintf(0, "Image header magic number error: 0x%02X\n", header->magic);
        return FLASH_ERROR_INVALID_PARAM;
    }
    
    // 验证CRC
    calculated_crc = calculate_crc32_default((uint8_t*)&header->entries, 
                                             sizeof(header->entries) + sizeof(header->partner_header_id) + sizeof(header->reserved2));
    if (header->crc32 != calculated_crc) {
        UARTIF_uartPrintf(0, "Image header CRC error: expected=0x%08X, calculated=0x%08X\n", 
                         header->crc32, calculated_crc);
        return FLASH_ERROR_CRC_FAIL;
    }
    
    // 提取页地址
    target_addresses = is_bw_header ? display->bw_pages_addresses : display->red_pages_addresses;
    
    for (i = 0; i < IMAGE_HEADER_ENTRIES; i++) {
        target_addresses[i] = (header->entries[i].address[0] << 16) |
                             (header->entries[i].address[1] << 8) |
                             header->entries[i].address[2];
    }
    
    // 记录头页ID
    if (is_bw_header) {
        display->bw_header_id = header_id;
        display->red_header_id = header->partner_header_id;
    } else {
        display->red_header_id = header_id;
        display->bw_header_id = header->partner_header_id;
    }
    
    UARTIF_uartPrintf(0, "Image header loaded successfully: %s, ID=0x%04X\n", 
                     is_bw_header ? "BW" : "RED", header_id);
    
    return FLASH_OK;
}

/**
 * @brief 从Flash加载图像数据页
 */
static boolean_t load_image_data_page(uint16_t data_id, uint8_t* buffer, uint8_t expected_magic)
{
    extern flash_manager_t g_flash_manager;
    uint8_t size;
    flash_result_t result;
    
    size = IMAGE_DATA_PER_PAGE;
    
    // 使用flash_manager接口读取图像数据页
    result = flash_read_image_data_page(&g_flash_manager, data_id, expected_magic, buffer, &size);
    
    return (result == FLASH_OK);
}

/**
 * @brief 验证图像数据页
 */
static flash_result_t verify_image_data_page(const uint8_t* page_data, uint8_t expected_magic, uint8_t expected_seq)
{
    uint32_t stored_crc;
    uint32_t data_size;
    uint32_t calculated_crc;
    
    if (page_data[0] != expected_magic) {
        UARTIF_uartPrintf(0, "Data page magic number error: expected=0x%02X, actual=0x%02X\n", 
                         expected_magic, page_data[0]);
        return FLASH_ERROR_INVALID_PARAM;
    }
    
    if (page_data[1] != expected_seq) {
        UARTIF_uartPrintf(0, "Data page sequence error: expected=%d, actual=%d\n", 
                         expected_seq, page_data[1]);
        return FLASH_ERROR_INVALID_PARAM;
    }
    
    // 验证CRC32
    stored_crc = *(uint32_t*)&page_data[4];
    data_size = (expected_seq == IMAGE_PAGES_PER_COLOR) ? 
                        IMAGE_LAST_PAGE_DATA_SIZE : IMAGE_DATA_PER_PAGE;
    calculated_crc = calculate_crc32_default(&page_data[8], data_size);
    
    if (stored_crc != calculated_crc) {
        UARTIF_uartPrintf(0, "Data page CRC error: expected=0x%08X, calculated=0x%08X\n", 
                         stored_crc, calculated_crc);
        return FLASH_ERROR_CRC_FAIL;
    }
    
    return FLASH_OK;
}

/**
 * @brief 发送黑白图像数据到EPD
 */
static flash_result_t send_bw_image_to_epd(image_display_new_t* display)
{
    // 引用外部函数
//    extern void delay1ms(uint32_t ms);
    
    // 定义EPD控制宏（从epd.c复制）
//    #define DC_H    Gpio_SetIO(0, 1, 1) //DC输出高
//    #define DC_L    Gpio_SetIO(0, 1, 0) //DC输出低
    
    // 定义spiWriteCmd函数（从epd.c复制）
//    void spiWriteCmd(uint8_t cmd) {
//        DC_L;
//        Spi_SetCS(TRUE);
//        Spi_SetCS(FALSE);
//        Spi_SendData(cmd);
//        Spi_SetCS(TRUE);
//    }
    
    uint8_t* page_buffer;
    int page;
    uint32_t page_address;
    uint16_t data_id;
    boolean_t load_result;
    uint32_t data_size;
    uint32_t i;
    
    page_buffer = g_general_buffer_256; // 使用全局缓冲区
    
    UARTIF_uartPrintf(0, "Starting to send BW image data to EPD...\n");
    
    // 发送黑白图像写入指令
//    spiWriteCmd(0x24); // 写指令0x24，表示开始写黑白内存
    DC_H; // 拉高数据指令线
    Spi_SetCS(TRUE);
    Spi_SetCS(FALSE); // 拉低片选
    
    // 逐页读取Flash数据并发送到EPD
    for (page = 0; page < IMAGE_PAGES_PER_COLOR; page++) {
        page_address = display->bw_pages_addresses[page];
        if (page_address == 0) {
            UARTIF_uartPrintf(0, "BW image page %d address invalid\n", page + 1);
            Spi_SetCS(TRUE); // 拉高片选
            DC_L; // 拉低数据指令线
            return FLASH_ERROR_NOT_FOUND;
        }
        
        // 读取页数据（使用页序号作为data_id）
        data_id = page + 1;
        load_result = load_image_data_page(data_id, page_buffer, MAGIC_BW_IMAGE_DATA);
        if (!load_result) {
            Spi_SetCS(TRUE); // 拉高片选
            DC_L; // 拉低数据指令线
            return FLASH_ERROR_READ_FAIL;
        }
        
        // 发送页数据到EPD（跳过8字节头部，只发送图像数据）
        data_size = (page == IMAGE_PAGES_PER_COLOR - 1) ? 
                            IMAGE_LAST_PAGE_DATA_SIZE : IMAGE_DATA_PER_PAGE;
        
        for (i = 0; i < data_size; i++) {
            Spi_SendData(page_buffer[8 + i]); // 跳过8字节头部
        }
        
        // 进度显示
        if ((page + 1) % 10 == 0 || page == IMAGE_PAGES_PER_COLOR - 1) {
            UARTIF_uartPrintf(0, "BW image sending progress: %d/%d\n", page + 1, IMAGE_PAGES_PER_COLOR);
        }
    }
    
    Spi_SetCS(TRUE); // 拉高片选
    DC_L; // 拉低数据指令线
    delay1ms(2);
    
    UARTIF_uartPrintf(0, "BW image data sending completed\n");
    return FLASH_OK;
}

/**
 * @brief 发送红白图像数据到EPD
 */
static flash_result_t send_red_image_to_epd(image_display_new_t* display)
{
    // 引用外部函数
//    extern void delay1ms(uint32_t ms);
//    
//    // 定义EPD控制宏（从epd.c复制）
//    #define DC_H    Gpio_SetIO(0, 1, 1) //DC输出高
//    #define DC_L    Gpio_SetIO(0, 1, 0) //DC输出低
//    
//    // 定义spiWriteCmd函数（从epd.c复制）
//    void spiWriteCmd(uint8_t cmd) {
//        DC_L;
//        Spi_SetCS(TRUE);
//        Spi_SetCS(FALSE);
//        Spi_SendData(cmd);
//        Spi_SetCS(TRUE);
//    }
    
    uint8_t* page_buffer;
    uint32_t page;
    uint32_t page_address;
    uint16_t data_id;
    boolean_t load_result;
    uint32_t data_size;
    uint32_t i;
    
    page_buffer = g_general_buffer_256; // 使用全局缓冲区
    
    UARTIF_uartPrintf(0, "Starting to send RED image data to EPD...\n");
    
    // 发送红白图像写入指令
//    spiWriteCmd(0x26); // 写指令0x26，表示开始写红白内存
    DC_H; // 拉高数据指令线
    Spi_SetCS(TRUE);
    Spi_SetCS(FALSE); // 拉低片选
    
    // 逐页读取Flash数据并发送到EPD
    for (page = 0; page < IMAGE_PAGES_PER_COLOR; page++) {
        page_address = display->red_pages_addresses[page];
        if (page_address == 0) {
            UARTIF_uartPrintf(0, "RED image page %d address invalid\n", page + 1);
            Spi_SetCS(TRUE); // 拉高片选
            DC_L; // 拉低数据指令线
            return FLASH_ERROR_NOT_FOUND;
        }
        
        // 读取页数据（使用页序号作为data_id）
        data_id = page + 1;
        load_result = load_image_data_page(data_id, page_buffer, MAGIC_RED_IMAGE_DATA);
        if (!load_result) {
            Spi_SetCS(TRUE); // 拉高片选
            DC_L; // 拉低数据指令线
            return FLASH_ERROR_READ_FAIL;
        }
        
        // 发送页数据到EPD（跳过8字节头部，只发送图像数据）
        data_size = (page == IMAGE_PAGES_PER_COLOR - 1) ? 
                            IMAGE_LAST_PAGE_DATA_SIZE : IMAGE_DATA_PER_PAGE;
        
        for (i = 0; i < data_size; i++) {
            Spi_SendData(page_buffer[8 + i]); // 跳过8字节头部
        }
        
        // 进度显示
        if ((page + 1) % 10 == 0 || page == IMAGE_PAGES_PER_COLOR - 1) {
            UARTIF_uartPrintf(0, "RED image sending progress: %d/%d\n", page + 1, IMAGE_PAGES_PER_COLOR);
        }
    }
    
    Spi_SetCS(TRUE); // 拉高片选
    DC_L; // 拉低数据指令线
    delay1ms(2);
    
    UARTIF_uartPrintf(0, "RED image data sending completed\n");
    return FLASH_OK;
}

/**
 * @brief 从Flash流式显示图像到EPD
 */
static flash_result_t display_image_from_flash_streaming(image_display_new_t* display)
{
    // 引用EPD更新函数
    extern void EPD_UpdateGDEY042Z98ALL_fast(void);
    flash_result_t result;
    
    // 发送黑白图像数据
    result = send_bw_image_to_epd(display);
    if (result != FLASH_OK) {
        return result;
    }
    
    // 发送红白图像数据
    result = send_red_image_to_epd(display);
    if (result != FLASH_OK) {
        return result;
    }
    
    // 调用EPD更新显示
    EPD_UpdateGDEY042Z98ALL_fast();
    
    UARTIF_uartPrintf(0, "Image streaming display completed\n");
    return FLASH_OK;
}

/**
 * @brief 将图像数据显示到EPD（流式处理版本）
 */
flash_result_t display_image_to_epd_streaming(image_display_new_t* display)
{
    flash_result_t result;
    
    UARTIF_uartPrintf(0, "Starting streaming display image to EPD...\n");
    
    // 调用流式显示函数
    result = display_image_from_flash_streaming(display);
    
    if (result == FLASH_OK) {
        UARTIF_uartPrintf(0, "Image streaming display successful\n");
    } else {
        UARTIF_uartPrintf(0, "Image streaming display failed, error code: %d\n", result);
    }
    
    return result;
}

/**
 * @brief 将图像数据显示到EPD（兼容性函数，已弃用）
 */
flash_result_t display_image_to_epd(const uint8_t* bw_data, const uint8_t* red_data)
{
    UARTIF_uartPrintf(0, "Warning: display_image_to_epd function is deprecated, please use display_image_to_epd_streaming\n");
    
    // 这个函数保留用于兼容性，但不再使用大型缓冲区
    // 实际应用中应该使用display_image_to_epd_streaming函数
    
    UARTIF_uartPrintf(0, "Please use the new streaming display interface\n");
    return FLASH_ERROR_INVALID_PARAM;
}

/**
 * @brief 显示指定槽位的图像
 */
flash_result_t image_display_new_show(image_display_new_t* display, uint8_t slot)
{
    if (!display) {
        return FLASH_ERROR_INVALID_PARAM;
    }
    
    if (display->state != DISPLAY_STATE_IDLE) {
        UARTIF_uartPrintf(0, "Image display busy, current state=%d\n", display->state);
        return FLASH_ERROR_NO_SPACE;
    }
    
    display->current_slot = slot;
    display->state = DISPLAY_STATE_LOADING;
    display->current_page = 0;
    display->buffer_pos = 0;
    display->processed_bytes = 0;
    
    UARTIF_uartPrintf(0, "Starting to display image for slot %d\n", slot);
    
    return FLASH_OK;
}

/**
 * @brief 处理图像显示过程
 */
flash_result_t image_display_new_process(image_display_new_t* display)
{
    flash_result_t result;
    uint16_t bw_header_id, red_header_id;
    
    result = FLASH_OK;
    
    if (!display) {
        return FLASH_ERROR_INVALID_PARAM;
    }
    
    switch (display->state) {
        case DISPLAY_STATE_IDLE:
            // 空闲状态，无需处理
            break;
            
        case DISPLAY_STATE_LOADING:
        {
            // 查找图像头页
            result = find_image_headers_by_slot(display, display->current_slot, 
                                              &bw_header_id, &red_header_id);
            if (result != FLASH_OK) {
                display->state = DISPLAY_STATE_ERROR;
                break;
            }
            
            // 加载黑白图像头页
            result = load_image_header(display, bw_header_id, 1);
            if (result != FLASH_OK) {
                display->state = DISPLAY_STATE_ERROR;
                break;
            }
            
            // 加载红白图像头页
            result = load_image_header(display, red_header_id, 0);
            if (result != FLASH_OK) {
                display->state = DISPLAY_STATE_ERROR;
                break;
            }
            
            display->state = DISPLAY_STATE_DECOMPRESSING;
            break;
        }
        
        case DISPLAY_STATE_DECOMPRESSING:
        {
            // 跳过解压缩状态，直接进入显示状态
            // 新的流式处理方式不需要预先加载所有数据
            display->state = DISPLAY_STATE_DISPLAYING;
            break;
        }
        
        case DISPLAY_STATE_DISPLAYING:
        {
            // 使用流式显示图像到EPD
            result = display_image_to_epd_streaming(display);
            if (result != FLASH_OK) {
                display->state = DISPLAY_STATE_ERROR;
                break;
            }
            
            display->state = DISPLAY_STATE_COMPLETE;
            break;
        }
        
        case DISPLAY_STATE_COMPLETE:
            // 显示完成，保持状态直到被重置
            break;
            
        case DISPLAY_STATE_ERROR:
            // 错误状态，保持直到被重置
            break;
            
        default:
            display->state = DISPLAY_STATE_ERROR;
            break;
    }
    
    return result;
}
