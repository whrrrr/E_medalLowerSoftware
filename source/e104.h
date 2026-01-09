
 void E104_ioInit(void);

boolean_t E104_getLinkState(void);
void E104_setSleepMode(void);
void E104_setWakeUpMode(void);
void E104_setConfigMode(void);
void E104_setTransmitMode(void);

// ===== AT 指令配置 =====
void E104_sendATCommand(const char *cmd);
uint16_t E104_receiveResponse(uint32_t timeout_ms);
void E104_setConnectionInterval(uint16_t interval);

// ===== 测试函数 =====
void E104_testBasicAT(void);
void E104_diagnosisMode(void);
void E104_testMTU(void);  // MTU 测试

void E104_sendTestData(void);
