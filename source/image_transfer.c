/******************************************************************************
 * Copyright (C) 2021, 
 *
 *  
 *  
 *  
 *  
 *
 ******************************************************************************/
 
/******************************************************************************
 ** @file image_transfer.c
 **
 ** @brief Source file for image transfer functions
 **
 ** @author MADS Team 
 **
 ******************************************************************************/


/******************************************************************************
 * Include files
 ******************************************************************************/
#include "flash_manager.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "base_types.h"
#include "image_transfer.h"
#include "uart_interface.h"
#include "crc_utils.h"
#include "uart.h"


/******************************************************************************
 * Local pre-processor symbols/macros ('#define')                            
 ******************************************************************************/
 /* command */
/* 带 frameIsFull 的命令格式（14字节）：*/
/* byte 0: header magic number 0xCA */
/* byte 1: command ID */
/* byte 2: image magic BW_IMAGE_DATA 0xA3 RED_IMAGE_DATA 0xA4 */
/* byte 3: slot ID 0x00~0x07 */
/* byte 4-11: frameIsFull (64 bits, little-endian) */
/* byte 12: tail magic number 0xAC*/
/* byte 13: tail new line 0x0A*/

/* 不带 frameIsFull 的命令格式（8字节）：*/
/* byte 0: header magic number 0xCA */
/* byte 1: command ID */
/* byte 2: sub ID or parameter */
/* byte 3: parameter */
/* byte 4: parameter */
/* byte 5: parameter */
/* byte 6: tail magic number 0xAC*/
/* byte 7: tail new line 0x0A*/

/* Command ID 0xB0 0x01 0xff 0xff 0xff 超时重传（8字节）*/
/* Command ID 0xB0 0x02 xx 0xff 0xff busy，byte 3 发送数值*100ms，后上位机可以尝试重发（8字节）*/
/* Command ID 0xB0 0x03 xx 0xff 0xFF 内部错误，停止传输，后面跟一个字节错误码（8字节）*/
/* Command ID 0xB1 上一个page发送成功，可继续发送下一个page，后续跟上一个page发的magic，slot，和当前frameIsFull（14字节）*/
/* Command ID 0xB3 上一个page发送失败，后续发上一个page发的magic，slot，和当前frameIsFull，请求立即重传（14字节）*/
/* Command ID 0xB5 当前帧数据全部接收并存储完成，后续发magic，slot，和当前frameIsFull，可开始下帧传输（14字节）*/
/* Command ID 0xB7 当前接收序列错误，后续发当前正在传输的magic，slot，和当前frameIsFull，请求立即重传（14字节）*/
/* Command ID 0xB7 0xFF 0xFF 0xFF 0xFF 传输错误，在IDLE状态收到非帧头数据，或接收数据magic，slot，frameNum不匹配（8字节）*/

#define IMAGE_PAGE_SIZE PAYLOAD_SIZE
#define IMAGE_TOTAL_SIZE 15000
#define IMAGE_PAGE_COUNT 61

#define IMAGE_FRAME_HEAD_MAGIC  0xA5A5A5A5
#define IMAGE_FRAME_TAIL_MAGIC  0x5A5A5A5A

#define IMAGE_FRAME_HEAD_SIZE   4
#define IMAGE_FRAME_TAIL_SIZE   4
#define IMAGE_META_SIZE         8  // slot_id(1) + type(1) + page_idx(1) + reserved(1) + crc32(2,建议4)
#define IMAGE_FRAME_SIZE        (IMAGE_FRAME_HEAD_SIZE + IMAGE_META_SIZE + IMAGE_PAGE_SIZE + IMAGE_FRAME_TAIL_SIZE)

#define IMAGE_TRANSFER_TIMEOUT_CYCLE  1000  // 可根据实际调整
#define IMAGE_HEADER_MAGIC          0xCA
#define IMAGE_TAIL_MAGIC            0xAC
#define IMAGE_TAIL_NEWLINE          0x0A
#define IMAGE_COMMAND_ID_GENERAL    0xB0
#define IMAGE_COMMAND_ID_ACK        0xB1
#define IMAGE_COMMAND_ID_NACK       0xB3
#define IMAGE_COMMAND_ID_COMPLETE   0xB5
#define IMAGE_COMMAND_ID_SEQ_ERR    0xB7
#define IMAGE_COMMAND_SUBID_TIMEOUT 0x01
#define IMAGE_COMMAND_SUBID_BUSY    0x02
#define IMAGE_COMMAND_SUBID_INTERNAL 0x03

// #define IMAGE_COMMAND_ID_INTERNAL_ERR 0xBE
/******************************************************************************
 * Global variable definitions (declared in header file with 'extern')
 ******************************************************************************/

/******************************************************************************
 * Local type definitions ('typedef')                                         
 ******************************************************************************/
typedef enum 
{
    IMGTRSF_IDLE = 0,
    IMGTRSF_RECEIVING_DATA,
    IMGTRSF_SAVING_DATA,
    IMGTRSF_WAITING_NEXT_PAGE
} imageTransferStates;

/******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/

/******************************************************************************
 * Local variable definitions ('static')                                      *
 ******************************************************************************/
// static ImageTransferStatus g_transferStatus;

static uint8_t rx_buf[IMAGE_FRAME_SIZE];
static uint16_t rx_idx = 0;
static uint16_t timeout_cycle = 0;
static uint64_t frameIsFull = 0x00;
static imageTransferStates state = IMGTRSF_IDLE;
static uint8_t lastImageMagic = 0xff;
static uint8_t lastSlotId = 0xff;
static uint8_t lastFrameNum = 0xff;


/*****************************************************************************
 * Function implementation - local ('static')
 ******************************************************************************/
static void sendCmd(uint8_t *cmd, uint8_t length)
{
    uint8_t i = 0;
    for (i = 0; i < length; i++)
    {
        Uart_SendData(UARTCH1, cmd[i]);
    }
}

/**
 * @brief 发送8字节通用命令（不带frameIsFull）
 * @param cmdId 命令ID
 * @param param1 参数1
 * @param param2 参数2
 * @param param3 参数3
 * @param param4 参数4
 */
static void sendCmd8(uint8_t cmdId, uint8_t param1, uint8_t param2, uint8_t param3, uint8_t param4)
{
    uint8_t cmd[8];
    cmd[0] = IMAGE_HEADER_MAGIC;
    cmd[1] = cmdId;
    cmd[2] = param1;
    cmd[3] = param2;
    cmd[4] = param3;
    cmd[5] = param4;
    cmd[6] = IMAGE_TAIL_MAGIC;
    cmd[7] = IMAGE_TAIL_NEWLINE;
    sendCmd(cmd, 8);
}

/**
 * @brief 发送14字节命令（带完整64位frameIsFull）
 * @param cmdId 命令ID
 * @param magic 图像magic
 * @param slotId 槽位ID
 */
static void sendCmd14(uint8_t cmdId, uint8_t magic, uint8_t slotId)
{
    uint8_t cmd[14];
    cmd[0] = IMAGE_HEADER_MAGIC;
    cmd[1] = cmdId;
    cmd[2] = magic;
    cmd[3] = slotId;
    // 发送完整的 64 位 frameIsFull (little-endian)
    cmd[4] = (uint8_t)(frameIsFull & 0xFF);
    cmd[5] = (uint8_t)((frameIsFull >> 8) & 0xFF);
    cmd[6] = (uint8_t)((frameIsFull >> 16) & 0xFF);
    cmd[7] = (uint8_t)((frameIsFull >> 24) & 0xFF);
    cmd[8] = (uint8_t)((frameIsFull >> 32) & 0xFF);
    cmd[9] = (uint8_t)((frameIsFull >> 40) & 0xFF);
    cmd[10] = (uint8_t)((frameIsFull >> 48) & 0xFF);
    cmd[11] = (uint8_t)((frameIsFull >> 56) & 0xFF);
    cmd[12] = IMAGE_TAIL_MAGIC;
    cmd[13] = IMAGE_TAIL_NEWLINE;
    sendCmd(cmd, 14);
}

static void checkTimeout(void)
{
    if (timeout_cycle > IMAGE_TRANSFER_TIMEOUT_CYCLE)
    {
        sendCmd8(IMAGE_COMMAND_ID_GENERAL, IMAGE_COMMAND_SUBID_TIMEOUT, 0xff, 0xff, 0xff);
        ImageTransfer_Init();
    }
    else
    {
        timeout_cycle++;
    }
}

static void sendCmdAck(uint8_t ack)
{
    sendCmd14(ack, lastImageMagic, lastSlotId);
}

static void sendCmdNack(void)
{
    sendCmd14(IMAGE_COMMAND_ID_NACK, lastImageMagic, lastSlotId);
}

/*****************************************************************************
 * Function implementation - global
 ******************************************************************************/

void ImageTransfer_Init(void)
{
    frameIsFull = 0x00;
    rx_idx = 0;
    timeout_cycle = 0;
    state = IMGTRSF_IDLE;
    lastImageMagic = 0xff;
    lastSlotId = 0xff;
    lastFrameNum = 0xff;
    memset(rx_buf, 0, IMAGE_FRAME_SIZE);
}

void ImageTransfer_Process(void)
{
    uint32_t magic = 0;
    uint32_t crcReceived = 0;
    uint32_t crcCalc = 0;
    const uint8_t *data = NULL;
    flash_result_t re = FLASH_OK;

    switch (state)
    {
        case IMGTRSF_IDLE:
            // Handle idle state
            (void)UARTIF_fetchDataFromUart(rx_buf, &rx_idx);
            if (rx_idx >= IMAGE_FRAME_HEAD_SIZE)
            {
                magic = (rx_buf[0]<<24)|(rx_buf[1]<<16)|(rx_buf[2]<<8)|rx_buf[3];
                if (magic == IMAGE_FRAME_HEAD_MAGIC)
                {
                    state = IMGTRSF_RECEIVING_DATA;
                    timeout_cycle = 0;
                }
                else
                {
                    sendCmd8(IMAGE_COMMAND_ID_SEQ_ERR, 0xff, 0xff, 0xff, 0xff);
                    ImageTransfer_Init();
                }
            }
            else if(rx_idx > 0)
            {
                checkTimeout();
            }
            else
            {
                // nothing to do
            }
            break;
        case IMGTRSF_RECEIVING_DATA:
            // Handle receiving data state
            if (UARTIF_fetchDataFromUart(rx_buf, &rx_idx) > 0u)
            {
                timeout_cycle = 0; // 有数据则重置超时
            }
            else
            {
                checkTimeout();
            }
            if (rx_idx >= IMAGE_FRAME_SIZE)
            {
                // 检查帧尾
                magic = (rx_buf[IMAGE_FRAME_SIZE-4]<<24)|
                        (rx_buf[IMAGE_FRAME_SIZE-3]<<16)|
                        (rx_buf[IMAGE_FRAME_SIZE-2]<<8)|
                        rx_buf[IMAGE_FRAME_SIZE-1];
                if (magic != IMAGE_FRAME_TAIL_MAGIC)
                {
                    // 帧尾错误，发送NACK
                    sendCmdNack();
                    rx_idx = 0;
                    timeout_cycle = 0;
                    state = IMGTRSF_IDLE;
                }
                else
                {
                    // 解析接收到的帧数据，直接存入 last* 变量
                    lastImageMagic = rx_buf[4];
                    lastSlotId = rx_buf[5];
                    lastFrameNum = rx_buf[6];
                    // rx_buf[7]; // reserved
                    crcReceived = (rx_buf[8]<<24)|(rx_buf[9]<<16)|(rx_buf[10]<<8)|rx_buf[11];
                    data = &rx_buf[12];

                    // CRC校验和参数有效性检查
                    crcCalc = calculate_crc32_default(data, IMAGE_PAGE_SIZE);
                    if (crcCalc != crcReceived ||
                        (lastImageMagic != MAGIC_BW_IMAGE_DATA && lastImageMagic != MAGIC_RED_IMAGE_DATA) ||
                        lastSlotId >= MAX_IMAGE_ENTRIES ||
                        lastFrameNum >= IMAGE_PAGE_COUNT)
                    {
                        sendCmdNack();
                        rx_idx = 0;
                        timeout_cycle = 0;
                        state = IMGTRSF_IDLE;
                        memset(rx_buf, 0, IMAGE_FRAME_SIZE);
                    }
                    else
                    {
                        state = IMGTRSF_SAVING_DATA;
                    }
                }
            }
            break;
        case IMGTRSF_SAVING_DATA:
            // Handle saving data state
            data = &rx_buf[12];
            re = FM_writeData(lastImageMagic, (uint16_t)(lastSlotId << 8 | lastFrameNum), data, IMAGE_PAGE_SIZE);
            if (re != FLASH_OK)
            {
                sendCmd8(IMAGE_COMMAND_ID_GENERAL, IMAGE_COMMAND_SUBID_INTERNAL, re, 0xff, 0xff);
                ImageTransfer_Init();
            }
            else
            {
                // 更新接收状态
                frameIsFull |= ((uint64_t)1 << lastFrameNum);

                // 清理缓冲区，准备接收下一帧
                rx_idx = 0;
                timeout_cycle = 0;
                memset(rx_buf, 0, IMAGE_FRAME_SIZE);
                state = IMGTRSF_IDLE;

                if (frameIsFull == 0x1FFFFFFFFFFFFF) // 61页全满 (bits 0-60)
                {
                    re = FM_writeImageHeader(lastImageMagic - 2u, lastSlotId);
                    if (re != FLASH_OK)
                    {
                        sendCmd8(IMAGE_COMMAND_ID_GENERAL, IMAGE_COMMAND_SUBID_INTERNAL, re, 0xff, 0xff);
                    }
                    else
                    {
                        // 全部接收完成，发送 COMPLETE
                        sendCmdAck(IMAGE_COMMAND_ID_COMPLETE);
                    }
                    ImageTransfer_Init();
                }
                else
                {
                    sendCmdAck(IMAGE_COMMAND_ID_ACK);
                }
            }
            break;

        default:
            break;
    }
}

/******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
