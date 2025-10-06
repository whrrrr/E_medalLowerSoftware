#ifndef TESTCASE_H
#define TESTCASE_H

#include "base_types.h"
#include "flash_config.h"
#include "flash_manager.h"

void TEST_FlashManagerReadAndWrite(void);
void TEST_FlashManagerReadAndWrite2(void);
void TEST_FlashManagerRead(void);
void TEST_FlashManagerRead2(void);
void TEST_FlashManagerGarbageCollection(void);
void TEST_ReadRawData(void);
void TEST_ReadRawDataByAddress(uint32_t address);
void TEST_WriteImage(void);

#endif // TESTCASE_H
