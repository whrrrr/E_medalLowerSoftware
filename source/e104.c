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
 ** @file e104.c
 **
 ** @brief Source file for e104 functions
 **
 ** @author MADS Team 
 **
 ******************************************************************************/

/******************************************************************************
 * Include files
 ******************************************************************************/
#include "gpio.h"
#include "uart_interface.h"
#include "lpuart.h"

/******************************************************************************
 * Local pre-processor symbols/macros ('#define')                            
 ******************************************************************************/

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

void E104_ioInit(void)
{
    Gpio_InitIO(3, 1, GpioDirIn);  // Link
    Gpio_InitIOExt(3,2,GpioDirOut,TRUE,FALSE,FALSE,FALSE);   // Wake up
    Gpio_InitIOExt(2,7,GpioDirOut,TRUE,FALSE,FALSE,FALSE);   // MOD (配置/透传模式)

    Gpio_SetIO(3,2, FALSE);  // 唤醒
    Gpio_SetIO(2,7, TRUE);   // 拉高MOD（透传模式）
    delay1ms(50);
    Gpio_SetIO(2,7, FALSE);
    delay1ms(50);
    Gpio_SetIO(2,7, TRUE);
    
    UARTIF_uartPrintf(0, "E104 IO Init Done\n");
}

boolean_t E104_getLinkState(void) // 返回 TRUE 链接成功； FALSE 链接断开
{
    static boolean_t preState = TRUE;
    static boolean_t currentState = TRUE;
    currentState = Gpio_GetIO(3,1);  
    if ((currentState == FALSE) && (preState == TRUE))
    {
        UARTIF_uartPrintf(0, "Link start! \n");
        // Gpio_SetIO(3,2, TRUE);  // 
    }
    else if ((currentState == TRUE) && (preState == FALSE))
    {
        UARTIF_uartPrintf(0, "Link end! \n");
    }
    else
    {
        // do nothing
    }
    preState = currentState;
    return !currentState;
}

// boolean_t E104_getDataState(void) // 返回 TRUE 数据传输中； FALSE 无数据
// {
//     static boolean_t preState = TRUE;
//     static boolean_t currentState = TRUE;
//     currentState = Gpio_GetIO(0,2);  
//     if ((currentState == FALSE) && (preState == TRUE))
//     {
//         UARTIF_uartPrintf(0, "Data on comming! \n");
//     }
//     else if ((currentState == TRUE) && (preState == FALSE))
//     {
//         UARTIF_uartPrintf(0, "Data end! \n");
//     }
//     else
//     {
//         // do nothing
//     }
//     preState = currentState;
//     return !currentState;
// }

void E104_setSleepMode(void)
{
    Gpio_SetIO(3,2, TRUE);
}

void E104_setWakeUpMode(void)
{
    Gpio_SetIO(3,2, FALSE);
}

// 配置模式 - 设置 GPIO 2,7 为低电平进入配置模式
void E104_setConfigMode(void)
{
    Gpio_SetIO(2,7, FALSE);
}

// 正常工作模式 - 设置 GPIO 2,7 为高电平
void E104_setTransmitMode(void)
{
    Gpio_SetIO(2,7, TRUE);
}

// void E104_setDisconnect(void)
// {
//     Gpio_SetIO(3,2, FALSE);
//     UARTIF_uartPrintf(0, "Set disconnect! \n");
// }

void E104_executeCommand(void)
{
    switch (UARTIF_passThroughCmd())
    {
        case '0':
        E104_setSleepMode();
        break;
        case '1':
        E104_setWakeUpMode();
        break;
        case '2':
//        E104_setTransmitMode();
//        break;
//        case '3':
//        E104_setConfigMode();
//        break;
//        case '4':
//        E104_setDisconnect();
//        break;
        default:

        break;
    }
}

/******************************************************************************
 * ===== AT 指令配置函数 =====
 ******************************************************************************/

/**
 * @brief 发送 AT 指令给 E104（通过 LPUART）
 * @param cmd AT 指令字符串，必须以 \r\n 结尾
 *
 * 示例：
 *   E104_sendATCommand("AT\r\n");              // 查询设备
 *   E104_sendATCommand("AT+CONMIN?\r\n");      // 查询连接间隙
 *   E104_sendATCommand("AT+CONMIN=8\r\n");     // 设置连接间隙为 10ms
 */
void E104_sendATCommand(const char *cmd)
{
    const char *p = NULL;
    
    if (cmd == NULL) {
        return;
    }
    
    // 打印发送的指令
    UARTIF_uartPrintf(0, "TX: %s\n", cmd);
    
    // 通过 LPUART 逐字节发送
    p = cmd;
    while (*p != '\0') {
        LPUart_SendData((uint8_t)(*p));
        p++;
    }
}

/**
 * @brief 接收 E104 的 AT 响应（使用中断队列）
 * @param timeout_ms 超时时间（毫秒）
 * @return 接收到的字节数
 */
uint16_t E104_receiveResponse(uint32_t timeout_ms)
{
    uint8_t rxData;
    uint16_t rxCount = 0;
    uint32_t timeoutCounter = timeout_ms;
    
    while (timeoutCounter-- > 0) {
        if (!UARTIF_isLpUartQueueEmpty()) {
            if (UARTIF_dequeueFromLpUart(&rxData)) {
                UARTIF_uartPrintf(0, "%c", rxData);  // 实时打印接收字节
                rxCount++;
                timeoutCounter = timeout_ms;
            }
        } else {
            delay1ms(1);
        }
    }
    
    if (rxCount > 0) {
        UARTIF_uartPrintf(0, "\n");
    }
    return rxCount;
}

/**
 * @brief 设置 E104 的连接间隙
 * @param interval 连接间隙参数 (6~3200)，实际间隙 = interval * 1.25ms
 *
 * 参数说明：
 *   - 6    → 7.5ms   （最小）
 *   - 8    → 10ms    （推荐）
 *   - 12   → 15ms
 *   - 160  → 200ms   （默认）
 *   - 3200 → 4000ms  （最大）
 */
void E104_setConnectionInterval(uint16_t interval)
{
    char cmd[32];
    uint16_t tens, ones;
    
    if (interval < 6 || interval > 3200) {
        UARTIF_uartPrintf(0, "E104: Invalid interval %d\n", interval);
        return;
    }
    
    UARTIF_uartPrintf(0, "E104: Set interval %d\n", interval);
    
    // 唤醒
    E104_setWakeUpMode();
    delay1ms(200);
    
    // 配置模式
    E104_setConfigMode();
    delay1ms(300);
    
    // 查询当前设置
    E104_sendATCommand("AT+CONMIN?");
    delay1ms(200);
    
    // 设置新值
    if (interval < 100) {
        tens = interval / 10;
        ones = interval % 10;
        
        cmd[0] = 'A';
        cmd[1] = 'T';
        cmd[2] = '+';
        cmd[3] = 'C';
        cmd[4] = 'O';
        cmd[5] = 'N';
        cmd[6] = 'M';
        cmd[7] = 'I';
        cmd[8] = 'N';
        cmd[9] = '=';
        cmd[10] = '0' + tens;
        cmd[11] = '0' + ones;
        cmd[12] = '\0';
    }
    else {
        UARTIF_uartPrintf(0, "E104: 2-digit only\n");
        return;
    }
    
    E104_sendATCommand(cmd);
    delay1ms(200);
    
    // 验证
    E104_sendATCommand("AT+CONMIN?");
    delay1ms(200);
    
    // 正常模式
    E104_setTransmitMode();
    delay1ms(200);
    
    // 睡眠
    E104_setSleepMode();
    delay1ms(200);
}

/**
 * @brief 诊断GPIO和LPUART状态
 */
/**
 * @brief 诊断GPIO和LPUART状态
 */
void E104_diagnosisMode(void)
{
    uint16_t waitCount = 0;
    uint8_t rxData;
    boolean_t linkState;
    uint16_t rxTotal = 0;  // 移到函数开头
    
    UARTIF_uartPrintf(0, "\n[DIAG] START\n");
    
    // 检查初始Link状态
    linkState = E104_getLinkState();
    UARTIF_uartPrintf(0, "[DIAG] Link=%d\n", linkState);
    
    // 唤醒
    E104_setWakeUpMode();
    delay1ms(300);
    UARTIF_uartPrintf(0, "[DIAG] Wake\n");
    
    // 配置模式
    E104_setConfigMode();
    delay1ms(300);
    UARTIF_uartPrintf(0, "[DIAG] Config mode\n");
    
    // 等待5秒接收数据
    waitCount = 5000;
    while (waitCount-- > 0) {
        if (!UARTIF_isLpUartQueueEmpty()) {
            if (UARTIF_dequeueFromLpUart(&rxData)) {
                if (rxTotal < 20) {  // 最多显示20字节
                    UARTIF_uartPrintf(0, "%02X ", rxData);
                }
                rxTotal++;
            }
        }
        delay1ms(1);
    }
    
    UARTIF_uartPrintf(0, "\n[DIAG] RX total: %d bytes\n", rxTotal);
    UARTIF_uartPrintf(0, "[DIAG] END\n");
}
void E104_testBasicAT(void)
{
    UARTIF_uartPrintf(0, "\n[TEST] E104 AT Test\n");
    
    // 唤醒
    E104_setWakeUpMode();
    delay1ms(300);
    
    // 进入配置模式
    E104_setConfigMode();
    delay1ms(300);
    
    // 测试 AT 指令
    UARTIF_uartPrintf(0, "Test 1: AT\n");
    E104_sendATCommand("AT");
    E104_receiveResponse(500);
    
    delay1ms(200);
    
    // 查询连接间隔
    UARTIF_uartPrintf(0, "Test 2: AT+CONMIN?\n");
    E104_sendATCommand("AT+CONMIN?");
    E104_receiveResponse(500);
    
    delay1ms(200);
    
    // 查询连接间隔MAX
    UARTIF_uartPrintf(0, "Test 3: AT+CONMAX?\n");
    E104_sendATCommand("AT+CONMAX?");
    E104_receiveResponse(500);
    
    delay1ms(200);
    
    // 查询设备角色
    UARTIF_uartPrintf(0, "Test 4: AT+ROLE?\n");
    E104_sendATCommand("AT+ROLE?");
    E104_receiveResponse(500);
    
    delay1ms(200);
    
    // 退出配置模式
    E104_setTransmitMode();
    delay1ms(300);
    UARTIF_uartPrintf(0, "[TEST] Done\n");
}

/**
 * @brief 发送测试数据 - 验证 BLE 特征是否可读
 * 这个函数用于验证 App 能否收到设备发送的数据
 */
void E104_sendTestData(void)
{
    const char *testMsg = "TEST_DATA_OK";
    UARTIF_uartPrintf(0, "[E104_TEST] Sending test data to BLE characteristic\n");
    // 注意：这需要通过 BLE 特征发送，但在 MCU 端 E104 是 BLE 主机
    // 实际测试需要 E104 将数据放入特征，App 才能读到
    // 这里仅作示意
}

