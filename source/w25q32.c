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
 ** @file w25q32.c
 **
 ** @brief Source file for w25q32 driver
 **
 ** @author MADS Team 
 **
 ******************************************************************************/

/******************************************************************************
 * Include files
 ******************************************************************************/
#include "gpio.h"
#include "spi.h"
#include "ddl.h"
#include "w25q32.h"
#include "uart_interface.h"

/******************************************************************************
 * Local pre-processor symbols/macros ('#define')                            
 ******************************************************************************/
#define FLASH_TOTAL_SIZE        0x400000    // 4MB总容量

/******************************************************************************
 * Global variable definitions (declared in header file with 'extern')
 ******************************************************************************/

/******************************************************************************
 * Local type definitions ('typedef')                                         
 ******************************************************************************/

/******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/

/******************************************************************************
 * Local variable definitions ('static')                                      *
 ******************************************************************************/

/******************************************************************************
 * Local pre-processor symbols/macros ('#define')                             
 ******************************************************************************/

/*****************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/* 片选控制函数 */
void W25Q32_CS(uint8_t state) 
{
    Gpio_SetIO(1, 4, state); //DC输出高
}

/* 初始化SPI接口 */
void W25Q32_Init(void) 
{
    Gpio_InitIO(1, 4, GpioDirOut);
    Gpio_SetIO(1, 4, 1);               //RST输出高
}

/* 读取状态寄存器1 (BUSY位在bit0) */
uint8_t W25Q32_ReadStatusReg(void)
{
    uint8_t status;
    
    W25Q32_CS(0);
    Spi_SendData(W25Q32_CMD_READ_STATUS_REG1);
    status = Spi_ReceiveData();
    W25Q32_CS(1);
    
    return status;
}

/* 读取状态寄存器2 */
uint8_t W25Q32_ReadStatusReg2(void)
{
    uint8_t status;
    
    W25Q32_CS(0);
    Spi_SendData(W25Q32_CMD_READ_STATUS_REG2);
    status = Spi_ReceiveData();
    W25Q32_CS(1);
    
    return status;
}

/* 读取状态寄存器3 */
uint8_t W25Q32_ReadStatusReg3(void)
{
    uint8_t status;
    
    W25Q32_CS(0);
    Spi_SendData(W25Q32_CMD_READ_STATUS_REG3);
    status = Spi_ReceiveData();
    W25Q32_CS(1);
    
    return status;
}

/* 等待Flash就绪 (检查BUSY位) */
void W25Q32_WaitForReady(void) 
{
    while (W25Q32_ReadStatusReg() & 0x01) {  // BIT0=1表示忙
        delay100us(1);
    }
}

/* 写使能命令 (必须在前置擦除/编程操作前调用) */
void W25Q32_WriteEnable(void) 
{
    
    W25Q32_CS(0);
    Spi_SendData(W25Q32_CMD_WRITE_ENABLE);
    W25Q32_CS(1);
}

void W25Q32_WriteDisable(void) 
{
    W25Q32_CS(0);
    Spi_SendData(W25Q32_CMD_WRITE_DISABLE);
    W25Q32_CS(1);
}

/* 读取JEDEC ID (制造商+设备ID) */
uint32_t W25Q32_ReadID(void) 
{
    uint8_t idBuf[3];
    
    W25Q32_CS(0);
    Spi_SendData(W25Q32_CMD_JEDEC_ID);

    idBuf[2] = Spi_ReceiveData();
    idBuf[1] = Spi_ReceiveData();
    idBuf[0] = Spi_ReceiveData();

    W25Q32_CS(1);
    
    return (idBuf[0] << 16) | (idBuf[1] << 8) | idBuf[2];
}

/* 扇区擦除 (4KB) */
void W25Q32_EraseSector(uint32_t sectorAddr) 
{    
       uint8_t sts = 0;

    W25Q32_WriteEnable();          // 使能写操作
    W25Q32_CS(0);

    Spi_SendData(W25Q32_CMD_SECTOR_ERASE);
    Spi_SendData((uint8_t)((sectorAddr >> 16) & 0xFF));
    Spi_SendData((uint8_t)((sectorAddr >> 8) & 0xFF));
    Spi_SendData((uint8_t)(sectorAddr & 0xFF));

    UARTIF_uartPrintf(0, "Waiting for erase sector .");

    W25Q32_CS(1);
    do 
    {
        sts = W25Q32_ReadStatusReg();
        // UARTIF_uartPrintf(0, "Status Reg 1 is 0x%x ! \n", sts);
        UARTIF_uartPrintf(0, ".");

        delay1ms(50);
    }
    while(sts & 0x01);
    UARTIF_uartPrintf(0, "\n");
    // W25Q32_WaitForReady();         // 等待擦除完成
}

void W25Q32_Erase32k(uint32_t addr) 
{    
       uint8_t sts = 0;

    W25Q32_WriteEnable();          // 使能写操作
    W25Q32_CS(0);

    Spi_SendData(W25Q32_CMD_32K_BLOCK_ERASE);
    Spi_SendData((uint8_t)((addr >> 16) & 0xFF));
    Spi_SendData((uint8_t)((addr >> 8) & 0xFF));
    Spi_SendData((uint8_t)(addr & 0xFF));
    UARTIF_uartPrintf(0, "Waiting for erase 32k .");

    W25Q32_CS(1);
    do 
    {
        sts = W25Q32_ReadStatusReg();
        // UARTIF_uartPrintf(0, "Status Reg 1 is 0x%x ! \n", sts);
        UARTIF_uartPrintf(0, ".");

        delay1ms(100);
    }
    while(sts & 0x01);
    UARTIF_uartPrintf(0, "\n");

    // W25Q32_WaitForReady();         // 等待擦除完成
}

void W25Q32_Erase64k(uint32_t addr) 
{    
       uint8_t sts = 0;

    W25Q32_WriteEnable();          // 使能写操作
    W25Q32_CS(0);

    Spi_SendData(W25Q32_CMD_64K_BLOCK_ERASE);
    Spi_SendData((uint8_t)((addr >> 16) & 0xFF));
    Spi_SendData((uint8_t)((addr >> 8) & 0xFF));
    Spi_SendData((uint8_t)(addr & 0xFF));
    UARTIF_uartPrintf(0, "Waiting for erase 64k .");

    W25Q32_CS(1);
    do 
    {
        sts = W25Q32_ReadStatusReg();
        // UARTIF_uartPrintf(0, "Status Reg 1 is 0x%x ! \n", sts);
        UARTIF_uartPrintf(0, ".");
        delay1ms(200);
    }
    while(sts & 0x01);
    UARTIF_uartPrintf(0, "\n");

    // W25Q32_WaitForReady();         // 等待擦除完成
}

/* 整片擦除 */
void W25Q32_EraseChip(void) 
{
    uint8_t sts = 0;
    W25Q32_WriteEnable();
    W25Q32_CS(0);
    Spi_SendData(W25Q32_CMD_CHIP_ERASE);
    UARTIF_uartPrintf(0, "Waiting for erase chip .");

    W25Q32_CS(1);
    do 
    {
        sts = W25Q32_ReadStatusReg();
        // UARTIF_uartPrintf(0, "Status Reg 1 is 0x%x ! \n", sts);
        UARTIF_uartPrintf(0, ".");
        delay1ms(200);
    }
    while(sts & 0x01);
    UARTIF_uartPrintf(0, "\n");

    // W25Q32_WaitForReady();         // 等待时间较长（秒级）
}

/* 读取数据 (支持跨页连续读) */
uint8_t W25Q32_ReadData(uint32_t addr, uint8_t *buf, uint32_t len) 
{
    uint32_t i = 0;    
    if (buf == NULL || len == 0 || addr >= FLASH_TOTAL_SIZE)
    {
        return W25Q32_ERROR;
    }

    W25Q32_CS(0);

    Spi_SendData(W25Q32_CMD_READ_DATA);
    Spi_SendData((uint8_t)((addr >> 16) & 0xFF));
    Spi_SendData((uint8_t)((addr >> 8) & 0xFF));
    Spi_SendData((uint8_t)(addr & 0xFF));


    for (;i < len;i++)
    {
        *(buf + i) = Spi_ReceiveData();
    }
    W25Q32_CS(1);

    return W25Q32_OK;
}

/* 写入数据 (页编程，单次最大256字节) */
uint8_t W25Q32_WritePage(uint32_t addr, uint8_t *buf, uint16_t len) 
{
    uint32_t i = 0;

    if (buf == NULL || len == 0 || addr >= FLASH_TOTAL_SIZE)
    {
        return W25Q32_ERROR;
    }
        // 长度不能超过页边界
    if (len > W25Q32_PAGE_SIZE)
    {
     len = W25Q32_PAGE_SIZE;
    }

    W25Q32_WriteEnable();          // 必须使能写操作
    W25Q32_CS(0);
    Spi_SendData(W25Q32_CMD_PAGE_PROGRAM);
    Spi_SendData((uint8_t)((addr >> 16) & 0xFF));
    Spi_SendData((uint8_t)((addr >> 8) & 0xFF));
    Spi_SendData((uint8_t)(addr & 0xFF));
    for (;i < len;i++)
    {
        Spi_SendData(*(buf + i));
    }
    W25Q32_CS(1);
    W25Q32_WaitForReady();         // 等待写入完成

    W25Q32_WriteDisable();
    return W25Q32_OK;
}

uint8_t W25Q32_memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *)s;
    if (s == NULL || n == 0) {
        return W25Q32_ERROR;
    }
    while (n--) {
        *p++ = (uint8_t)c;
    }
    return W25Q32_OK;
}
/******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/


