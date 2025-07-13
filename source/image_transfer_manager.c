#include "image_transfer_manager.h"
#include "uart_interface.h"
#include "w25q32.h"
#include "flash_manager.h"
#include <string.h>
#include "uart.h"
#include "lpuart.h"
#include "crc_utils.h"
#include "spi.h"

// 超时相关变量
static volatile uint32_t g_timeout_counter = 0;
static volatile bool g_timeout_flag = false;

#define TIMEOUT_THRESHOLD_MS 1000  // 1秒超时
#define TIMER_INTERVAL_MS 5        // 5ms定时器间隔

// 外部变量
extern uint8_t g_flash_buffer[FLASH_PAGE_SIZE];

// 静态函数声明
static flash_result_t handle_start_frame(image_transfer_manager_t* manager, const start_frame_t* frame);
static flash_result_t handle_data_frame(image_transfer_manager_t* manager, const data_frame_t* frame);
static flash_result_t handle_end_frame(image_transfer_manager_t* manager, const end_frame_t* frame);
static uint8_t parse_protocol_frame(image_transfer_manager_t* manager);
static bool write_image_data_page(uint16_t page_id, const uint8_t* data, uint8_t size, uint8_t magic);
static void send_start_reply(uint8_t command, uint8_t slot_color, uint8_t status);
static void send_data_reply(uint8_t command, uint8_t slot_color, uint8_t page_seq, uint8_t status);
static void send_end_reply(uint8_t command, uint8_t slot_color);



/**
 * @brief 发送首帧回复
 */
static void send_start_reply(uint8_t command, uint8_t slot_color, uint8_t status)
{
    start_reply_frame_t reply;
    reply.magic = PROTOCOL_MAGIC_MCU;
    reply.command = command;
    reply.slot_color = slot_color;
    reply.status = status;
    reply.reserved = 0;
    reply.end_magic = PROTOCOL_END_MCU;
    
    send_reply_frame((uint8_t*)&reply, sizeof(reply));
}

/**
 * @brief 发送数据帧回复
 */
static void send_data_reply(uint8_t command, uint8_t slot_color, uint8_t page_seq, uint8_t status)
{
    data_reply_frame_t reply;
    reply.magic = PROTOCOL_MAGIC_MCU;
    reply.command = command;
    reply.slot_color = slot_color;
    reply.page_seq = page_seq;
    reply.status = status;
    
    send_reply_frame((uint8_t*)&reply, sizeof(reply));
}

/**
 * @brief 发送尾帧回复
 */
static void send_end_reply(uint8_t command, uint8_t slot_color)
{
    end_reply_frame_t reply;
    reply.magic = PROTOCOL_MAGIC_MCU;
    reply.command = command;
    reply.slot_color = slot_color;
    reply.end_magic = PROTOCOL_END_MCU;
    
    send_reply_frame((uint8_t*)&reply, sizeof(reply));
}

/**
 * @brief 发送回复帧
 */
void send_reply_frame(const uint8_t* frame_data, uint16_t frame_size)
{
    uint16_t i;
    
    // 通过LPUART发送回复帧
    for (i = 0; i < frame_size; i++) {
        // 这里应该调用实际的LPUART发送函数
        UARTIF_SendBleData(frame_data[i]);
    }
}

/**
 * @brief 获取下一个可用的数据ID
 */
uint16_t get_next_data_id(image_transfer_manager_t* manager)
{
    static uint16_t next_id = 0x1000;
    return next_id++;
}

/**
 * @brief 分配Flash页地址
 */
uint32_t allocate_flash_page_address(image_transfer_manager_t* manager)
{
    uint32_t address;
    
    // 简化实现：基于当前写入地址分配
    address = manager->flash_mgr->next_write_address;
    manager->flash_mgr->next_write_address += FLASH_PAGE_SIZE;
    return address;
}

/**
 * @brief 写入图像数据页到Flash
 */
static bool write_image_data_page(uint16_t page_id, const uint8_t* data, uint8_t size, uint8_t magic)
{
    extern flash_manager_t g_flash_manager;
    flash_result_t result;
    
    // 使用flash_manager接口写入图像数据页
    result = flash_write_image_data_page(&g_flash_manager, magic, page_id, data, size);
    
    return (result == FLASH_OK);
}

/**
 * @brief 处理首帧
 */
static flash_result_t handle_start_frame(image_transfer_manager_t* manager, const start_frame_t* frame)
{
    image_transfer_context_t* ctx;
    
    ctx = &manager->context;
    
    // 检查魔法数字
    if (frame->magic != PROTOCOL_MAGIC_HOST || frame->end_magic != PROTOCOL_END_HOST) {
        send_start_reply(frame->command, frame->slot_color, MCU_STATUS_ERROR);
        return FLASH_ERROR_INVALID_PARAM;
    }
    
    // 检查是否忙碌
    if (ctx->state != IMG_TRANSFER_IDLE) {
        send_start_reply(frame->command, frame->slot_color, MCU_STATUS_BUSY);
        return FLASH_ERROR_NO_SPACE;
    }
    
    // 初始化传输上下文
    ctx->current_slot = frame->slot_color & 0x0F;
    ctx->current_color = (frame->slot_color & 0xF0);
    ctx->current_page = 1;
    ctx->current_frame = 1;
    ctx->buffer_pos = 0;
    ctx->timeout_counter = 0;
    ctx->retry_count = 0;
    
    // 分配头页ID
    if (ctx->current_color == COLOR_TYPE_BW) {
        ctx->state = IMG_TRANSFER_RECEIVING_BW;
        ctx->bw_header_id = get_next_data_id(manager);
    } else {
        ctx->state = IMG_TRANSFER_RECEIVING_RED;
        ctx->red_header_id = get_next_data_id(manager);
    }
    
    // 清空页缓冲区
    memset(ctx->page_buffer, 0, sizeof(ctx->page_buffer));
    
    send_start_reply(frame->command, frame->slot_color, MCU_STATUS_OK);
    
    UARTIF_uartPrintf(0, "Start frame received: slot=%d, color=%d\n", 
                     ctx->current_slot, ctx->current_color >> 4);
    
    return FLASH_OK;
}

/**
 * @brief 处理数据帧
 */
static flash_result_t handle_data_frame(image_transfer_manager_t* manager, const data_frame_t* frame)
{
    image_transfer_context_t* ctx;
	    uint16_t data_size;
    uint8_t magic;
    uint16_t page_id;
    bool result;
    uint32_t actual_data_size;

    
    ctx = &manager->context;
    
    // 检查魔法数字
    if (frame->magic != PROTOCOL_MAGIC_HOST || frame->end_magic != PROTOCOL_END_HOST) {
        send_data_reply(frame->command, frame->slot_color, frame->page_seq, DATA_STATUS_CRC_ERROR);
        return FLASH_ERROR_INVALID_PARAM;
    }
    
    // 检查状态
    if (ctx->state != IMG_TRANSFER_RECEIVING_BW && ctx->state != IMG_TRANSFER_RECEIVING_RED) {
        send_data_reply(frame->command, frame->slot_color, frame->page_seq, DATA_STATUS_CRC_ERROR);
        return FLASH_ERROR_INVALID_PARAM;
    }
    
    // 检查页序列
    if (frame->page_seq != ctx->current_page) {
        send_data_reply(frame->command, frame->slot_color, frame->page_seq, DATA_STATUS_CRC_ERROR);
        return FLASH_ERROR_INVALID_PARAM;
    }
    
    // 检查帧序列
    if (frame->frame_seq != ctx->current_frame) {
        send_data_reply(frame->command, frame->slot_color, frame->page_seq, 
                       DATA_STATUS_FRAME_MISSING | (frame->frame_seq & 0x0F));
        return FLASH_ERROR_INVALID_PARAM;
    }
    
    // 处理数据
        
        if (frame->frame_seq <= 4) {
        // 前4帧，每帧54字节
        data_size = FRAME_DATA_SIZE;
        memcpy(&ctx->page_buffer[ctx->buffer_pos], frame->data, data_size);
        ctx->buffer_pos += data_size;
    } else {
        // 第5帧，32字节数据 + 4字节CRC
        data_size = FRAME_LAST_DATA_SIZE;
        memcpy(&ctx->page_buffer[ctx->buffer_pos], frame->data, data_size);
        ctx->buffer_pos += data_size;
        
        // 提取CRC
        memcpy(&ctx->received_crc, &frame->data[data_size], 4);
        
        // 验证CRC
            actual_data_size = (ctx->current_page == IMAGE_PAGES_PER_COLOR) ? 
                                       IMAGE_LAST_PAGE_DATA_SIZE : IMAGE_DATA_PER_PAGE;
            ctx->calculated_crc = calculate_crc32_default(ctx->page_buffer, actual_data_size);
            
            if (ctx->received_crc != ctx->calculated_crc) {
                send_data_reply(frame->command, frame->slot_color, frame->page_seq, DATA_STATUS_CRC_ERROR);
                // 重置页缓冲区
                ctx->buffer_pos = 0;
                ctx->current_frame = 1;
                memset(ctx->page_buffer, 0, sizeof(ctx->page_buffer));
                return FLASH_ERROR_CRC_FAIL;
            }
            
            // 写入Flash
            magic = (ctx->current_color == COLOR_TYPE_BW) ? MAGIC_BW_IMAGE_DATA : MAGIC_RED_IMAGE_DATA;
            page_id = ctx->current_page;
            result = write_image_data_page(page_id, ctx->page_buffer, actual_data_size, magic);
        if (!result) {
            send_data_reply(frame->command, frame->slot_color, frame->page_seq, DATA_STATUS_CRC_ERROR);
            return FLASH_ERROR_WRITE_FAIL;
        }
        
        // 准备下一页
        ctx->current_page++;
        ctx->current_frame = 1;
        ctx->buffer_pos = 0;
        memset(ctx->page_buffer, 0, sizeof(ctx->page_buffer));
        
        UARTIF_uartPrintf(0, "Page %d written to Flash\n", frame->page_seq);
    }
    
    // 准备下一帧
    if (frame->frame_seq < FRAMES_PER_PAGE) {
        ctx->current_frame++;
    }
    
    send_data_reply(frame->command, frame->slot_color, frame->page_seq, DATA_STATUS_OK);
    
    return FLASH_OK;
}

/**
 * @brief 处理尾帧
 */
static flash_result_t handle_end_frame(image_transfer_manager_t* manager, const end_frame_t* frame)
{
    image_transfer_context_t* ctx;
    flash_result_t result;
    
    ctx = &manager->context;
    
    // 检查魔法数字
    if (frame->magic != PROTOCOL_MAGIC_HOST || frame->end_magic != PROTOCOL_END_HOST) {
        return FLASH_ERROR_INVALID_PARAM;
    }
    
    // 验证接收完整性
    if (!verify_received_pages(manager)) {
        // 请求重传缺失页
        return FLASH_ERROR_NOT_FOUND;
    }
    
    // 创建图像头页
    if (ctx->current_color == COLOR_TYPE_BW) {
        result = create_image_header_page(manager, 1);
        if (result == FLASH_OK) {
            // 切换到红白传输
            ctx->state = IMG_TRANSFER_RECEIVING_RED;
            ctx->current_color = COLOR_TYPE_RED;
            ctx->current_page = 1;
            ctx->current_frame = 1;
            ctx->buffer_pos = 0;
            ctx->red_header_id = get_next_data_id(manager);
            memset(ctx->page_buffer, 0, sizeof(ctx->page_buffer));
        }
    } else {
        result = create_image_header_page(manager, 0);
        if (result == FLASH_OK) {
            ctx->state = IMG_TRANSFER_COMPLETE;
        }
    }
    
    if (result == FLASH_OK) {
        send_end_reply(frame->command, frame->slot_color);
        UARTIF_uartPrintf(0, "End frame processed successfully\n");
    }
    
    return result;
}

/**
 * @brief 检查是否接收到完整帧
 */
static bool check_complete_frame(image_transfer_manager_t* manager)
{
	  uint16_t test_magic;
    uint32_t tail_magic;
	  uint16_t head_magic;

    int i;

    if (manager->protocol_pos < 8) {
        return false; // 数据不足，无法检查头魔法数
    }
    
    // 检查头魔法数 0xA5A5
    
    head_magic = *(uint16_t*)manager->protocol_buffer;
    
    if (head_magic != 0xA5A5) {
        // 头魔法数不匹配，查找下一个可能的头魔法数
        for (i = 1; i < manager->protocol_pos - 1; i++) {
            test_magic = *(uint16_t*)(manager->protocol_buffer + i);
            if (test_magic == 0xA5A5) {
                // 找到头魔法数，移动数据
                memmove(manager->protocol_buffer, manager->protocol_buffer + i, manager->protocol_pos - i);
                manager->protocol_pos -= i;
                break;
            }
        }
        if (manager->protocol_pos < 8) {
            return false;
        }
        head_magic = *(uint16_t*)manager->protocol_buffer;
        if (head_magic != 0xA5A5) {
            manager->protocol_pos = 0; // 重置缓冲区
            return false;
        }
    }
    
    // 检查是否有足够数据包含尾魔法数
    if (manager->protocol_pos < 12) {
        return false; // 数据不足，无法检查尾魔法数
    }
    
    // 查找尾魔法数 0xA5A5AFAF
    for (i = 8; i <= manager->protocol_pos - 4; i++) {
        tail_magic = *(uint32_t*)(manager->protocol_buffer + i);
        if (tail_magic == 0xAFAFA5A5) { // 注意字节序
            // 找到完整帧
            return true;
        }
    }
    
    return false; // 未找到完整的尾魔法数
}

/**
 * @brief 超时处理函数（由定时器中断调用）
 */
void image_transfer_timeout_handler(void)
{
    g_timeout_counter++;
    if (g_timeout_counter >= (TIMEOUT_THRESHOLD_MS / TIMER_INTERVAL_MS)) {
        g_timeout_flag = true;
        g_timeout_counter = 0;
    }
}

/**
 * @brief 重置超时计数器
 */
static void reset_timeout(void)
{
    g_timeout_counter = 0;
    g_timeout_flag = false;
}

/**
 * @brief 检查是否超时
 */
static bool is_timeout(void)
{
    return g_timeout_flag;
}

/**
 * @brief 解析协议帧
 */
static uint8_t parse_protocol_frame(image_transfer_manager_t* manager)
{
    uint16_t magic;
    uint8_t command;
    
    if (manager->protocol_pos < 8) {
        return 0; // 数据不足
    }
    
    magic = *(uint16_t*)manager->protocol_buffer;
    if (magic != PROTOCOL_MAGIC_HOST) {
        manager->protocol_pos = 0;
        return 0;
    }
    
    command = manager->protocol_buffer[2];
    
    switch (command) {
        case CMD_IMAGE_TRANSFER:
            if (manager->protocol_pos >= sizeof(start_frame_t)) {
                handle_start_frame(manager, (start_frame_t*)manager->protocol_buffer);
                manager->protocol_pos = 0;
                return 1;
            }
            break;
            
        case CMD_IMAGE_DATA:
            if (manager->protocol_pos >= sizeof(data_frame_t)) {
                handle_data_frame(manager, (data_frame_t*)manager->protocol_buffer);
                manager->protocol_pos = 0;
                return 1;
            }
            break;
            
        case CMD_TRANSFER_END:
            if (manager->protocol_pos >= sizeof(end_frame_t)) {
                handle_end_frame(manager, (end_frame_t*)manager->protocol_buffer);
                manager->protocol_pos = 0;
                return 1;
            }
            break;
            
        default:
            manager->protocol_pos = 0;
            break;
    }
    
    return 0;
}

/**
 * @brief 创建图像头页
 */
flash_result_t create_image_header_page(image_transfer_manager_t* manager, uint8_t is_bw_header)
{
    image_transfer_context_t* ctx;
    image_header_page_t* header;
    uint32_t* addresses;
    int i;
    extern flash_manager_t g_flash_manager;
    uint16_t header_id;
    flash_result_t result;
    
    ctx = &manager->context;
    header = (image_header_page_t*)g_flash_buffer;
    
    // 清空缓冲区
    memset(g_flash_buffer, 0, FLASH_PAGE_SIZE);
    
    // 填充头页
    header->magic = MAGIC_IMAGE_HEADER;
    header->data_id = is_bw_header ? ctx->bw_header_id : ctx->red_header_id;
    header->reserved1 = 0;
    
    // 填充地址条目
    addresses = is_bw_header ? ctx->bw_pages_addresses : ctx->red_pages_addresses;
    for (i = 0; i < IMAGE_HEADER_ENTRIES; i++) {
        header->entries[i].frame_seq_id = i + 1;
        header->entries[i].address[0] = (addresses[i] >> 16) & 0xFF;
        header->entries[i].address[1] = (addresses[i] >> 8) & 0xFF;
        header->entries[i].address[2] = addresses[i] & 0xFF;
    }
    
    // 设置伙伴头页ID
    header->partner_header_id = is_bw_header ? ctx->red_header_id : ctx->bw_header_id;
    header->reserved2 = 0;
    
    // 计算CRC
    header->crc32 = calculate_crc32_default((uint8_t*)&header->entries, 
                                   sizeof(header->entries) + sizeof(header->partner_header_id) + sizeof(header->reserved2));
    
    // 使用flash_manager接口写入图像头页
    header_id = is_bw_header ? ctx->bw_header_id : ctx->red_header_id;
    
    // 将头页数据写入Flash（跳过magic字段，因为flash_write_image_data_page会设置）
    result = flash_write_image_data_page(&g_flash_manager, MAGIC_IMAGE_HEADER, 
                                                       header_id, 
                                                       (uint8_t*)&header->data_id, 
                                                       sizeof(image_header_page_t) - 1);
    if (result != FLASH_OK) {
        return result;
    }
    
    UARTIF_uartPrintf(0, "Image header page created: ID=%d\n", header->data_id);
    
    return FLASH_OK;
}

/**
 * @brief 验证接收页完整性
 */
uint8_t verify_received_pages(image_transfer_manager_t* manager)
{
    image_transfer_context_t* ctx;
    
    ctx = &manager->context;
    
    // 检查是否接收了所有页
    if (ctx->current_page <= IMAGE_PAGES_PER_COLOR) {
        return 0; // 页数不足
    }
    
    // 这里可以添加更详细的验证逻辑
    return 1;
}

/**
 * @brief 初始化图像传输管理器
 */
flash_result_t image_transfer_init(image_transfer_manager_t* manager, 
                                  flash_manager_t* flash_mgr, 
                                  Queue* rx_queue)
{
    if (!manager || !flash_mgr || !rx_queue) {
        return FLASH_ERROR_INVALID_PARAM;
    }
    
    memset(manager, 0, sizeof(image_transfer_manager_t));
    manager->flash_mgr = flash_mgr;
    manager->rx_queue = rx_queue;
    manager->context.state = IMG_TRANSFER_IDLE;
    
    return FLASH_OK;
}

/**
 * @brief 重置传输状态
 */
void image_transfer_reset(image_transfer_manager_t* manager)
{
    if (!manager) return;
    
    manager->context.state = IMG_TRANSFER_IDLE;
    manager->protocol_pos = 0;
    memset(&manager->context, 0, sizeof(image_transfer_context_t));
}

/**
 * @brief 处理图像传输
 */
flash_result_t image_transfer_process(image_transfer_manager_t* manager)
{
    uint8_t data;
    extern Queue lpUartRecdata;  // 使用uart_interface.c中定义的lpUartRecdata
    
    if (!manager) {
        return FLASH_ERROR_INVALID_PARAM;
    }
    
    // 从lpUartRecdata队列读取数据
    while (Queue_Dequeue(&lpUartRecdata, &data)) {
        // 添加到协议缓冲区
        if (manager->protocol_pos < sizeof(manager->protocol_buffer)) {
            manager->protocol_buffer[manager->protocol_pos++] = data;
            
            // 检查是否接收到完整帧（头魔法数0xA5A5和尾魔法数0xA5A5AFAF）
            if (check_complete_frame(manager)) {
                // 尝试解析帧
                if (parse_protocol_frame(manager)) {
                    reset_timeout(); // 重置超时计数
                }
            }
        } else {
            // 缓冲区溢出，重置
            manager->protocol_pos = 0;
        }
    }
    
    // 检查超时
    if (is_timeout()) {
        // 超时处理：重置传输状态
        if (manager->context.state != IMG_TRANSFER_IDLE) {
            UARTIF_uartPrintf(0, "Image transfer timeout, resetting...\n");
            manager->context.state = IMG_TRANSFER_IDLE;
            manager->protocol_pos = 0;
            memset(manager->protocol_buffer, 0, sizeof(manager->protocol_buffer));
            reset_timeout();
        }
    }
    
    return FLASH_OK;
}
