#ifndef APP_BANK_H
#define APP_BANK_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/keyboard.h"
#include "eeprom-layout.h"
#include "misc.h"

#define BANK_CHANNELS_PER_PAGE 7u

typedef enum {
    BANK_UI_BANK_LIST,
    BANK_UI_CHANNEL_LIST,
    BANK_UI_BULK_SELECT
} BANK_UiState_t;

typedef struct {
    channel_t channel;
    BANK_UiState_t state;
    uint8_t selection;
    uint8_t action;
} BANK_Ui_t;

extern BANK_Ui_t gBankUi;
extern bool gBankMainPreview;
#define gBankUiChannel   gBankUi.channel
#define gBankUiState     gBankUi.state
#define gBankUiSelection gBankUi.selection
#define gBankUiAction    gBankUi.action

bool BANK_IsMember(channel_t channel);
void BANK_Open(void);
void BANK_ProcessKeys(KEY_Code_t key, bool pressed, bool held);
bool BANK_MainPreviewProcessKey(KEY_Code_t key, bool pressed, bool held);

#endif
