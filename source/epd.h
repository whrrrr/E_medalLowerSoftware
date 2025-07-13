#ifndef EPD_H
#define EPD_H
#include "base_types.h"


#define WIDTH_420  160
// #define WIDTH_VISIBLE = WIDTH;
#define HEIGHT_420  150
#define EPD_ARRAY   WIDTH_420*HEIGHT_420/8 
#define WIDTH WIDTH_420
#define HEIGHT HEIGHT_420
#define BYTES_PER_ROW (WIDTH / 8)
#define DC_H    Gpio_SetIO(0, 1, 1) //DC输出高
#define DC_L    Gpio_SetIO(0, 1, 0) //DC输出低
#define RST_H   Gpio_SetIO(0, 3, 1) //RST输出高
#define RST_L   Gpio_SetIO(0, 3, 0) //RST输出低


void EPD_initWft0154cz17(boolean_t isfull);
void EPD_poweroff(void);
void EPD_initGDEY042Z98(void);
void EPD_WhiteScreenGDEY042Z98ALL(unsigned char data[][BYTES_PER_ROW]);
void EPD_WhiteScreenGDEY042Z98ALLBlack(void);
void EPD_WhiteScreenGDEY042Z98ALLWrite(void);
void EPD_WhiteScreenGDEY042Z98ALLRed(void);

#endif // EPD_H
