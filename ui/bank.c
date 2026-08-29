#include <string.h>

#include "app/bank.h"
#include "bitmaps.h"
#include "driver/eeprom.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "settings.h"
#include "ui/helper.h"

static void UI_BANK_PrintRow(const char *text, uint8_t line, bool selected)
{
    UI_PrintStringSmallNormal(text, 0, 0, line);
    if (selected)
        for (uint8_t x = 0; x < LCD_WIDTH; x++)
            gFrameBuffer[line][x] ^= 0xFF;
}

static void UI_BANK_Status(const char *text)
{
    memset(gStatusLine, 0, sizeof(gStatusLine));
    UI_PrintStringSmallBufferBold(text, gStatusLine);
    ST7565_BlitStatusLine();
    gUpdateStatus = false;
}

static void UI_BANK_FetchName(uint8_t bank, char name[11])
{
    int8_t last = -1;

    EEPROM_ReadBuffer(EEPROM_K5RX_BANK_TABLE_BASE +
        ((bank - EEPROM_K5RX_BANK_MIN) * EEPROM_K5RX_BANK_RECORD_SIZE),
        name, EEPROM_K5RX_CHANNEL_NAME_LENGTH);
    for (uint8_t i = 0; i < EEPROM_K5RX_CHANNEL_NAME_LENGTH; i++) {
        if ((uint8_t)(name[i] - 32) > 94u)
            goto fallback;
        if (name[i] != ' ')
            last = i;
    }
    if (last >= 0) {
        name[last + 1] = 0;
        return;
    }

fallback:
    memcpy(name, "BANK0", 6);
    name[4] += bank;
}

void UI_DisplayBank(void)
{
    char text[36];

    UI_DisplayClear();
    if (gBankUiState == BANK_UI_BULK_SELECT) {
        sprintf(text, "B%u BULK M:RUN", gBankUiSelection + EEPROM_K5RX_BANK_MIN);
        UI_BANK_Status(text);
        for (uint8_t row = 0; row < 6; row++) {
            sprintf(text, "%s L%u", row < 3 ? "LOAD" : "ADD", (row % 3) + 1);
            UI_BANK_PrintRow(text, row, row == gBankUiAction);
        }
        ST7565_BlitFullScreen();
        return;
    }

    if (gBankUiState == BANK_UI_CHANNEL_LIST) {
        uint16_t selected = 0;
        uint16_t shown = 0;
        char name[11];

        for (channel_t channel = 0; channel < gBankUiChannel; channel++)
            if (BANK_IsMember(channel))
                selected++;

        const uint16_t first = (selected / BANK_CHANNELS_PER_PAGE) * BANK_CHANNELS_PER_PAGE;
        uint16_t ordinal = 0;
        for (channel_t channel = 0; channel < EEPROM_K5RX_CHANNEL_COUNT && shown < BANK_CHANNELS_PER_PAGE; channel++) {
            if (!BANK_IsMember(channel))
                continue;
            if (ordinal++ < first)
                continue;

            SETTINGS_FetchChannelName(name, channel);
            const SETTINGS_K5RX_ChannelRecord_t *record = SETTINGS_GetK5RXChannelRecord(channel);
            const uint8_t scanListMask = record == NULL ? 0 : record->scanListMask;
            sprintf(text, "%03u %-10.10s %c%c%c", channel + 1u, name,
                scanListMask & 1u ? '1' : '-', scanListMask & 2u ? '2' : '-',
                scanListMask & 4u ? '3' : '-');
            UI_BANK_PrintRow(text, shown++, channel == gBankUiChannel);
        }

        ST7565_BlitFullScreen();
        return;
    }

    const uint8_t selectedBank = gBankUiSelection + EEPROM_K5RX_BANK_MIN;
    uint16_t count = 0;
    char name[11];

    for (channel_t channel = 0; channel < EEPROM_K5RX_CHANNEL_COUNT; channel++)
        if (BANK_IsMember(channel))
            count++;
    UI_BANK_FetchName(selectedBank, name);
    sprintf(text, "B%u %-10.10s %u", selectedBank, name, count);
    UI_PrintStringSmallNormal(text, 0, 0, 0);

    for (uint8_t row = 0; row < 4; row++) {
        for (uint8_t col = 0; col < 2; col++) {
            const uint8_t bank = row + (col * 4u) + EEPROM_K5RX_BANK_MIN;
            const uint8_t x = col * 64u;
            UI_BANK_FetchName(bank, name);
            if (bank == selectedBank)
                memcpy(&gFrameBuffer[row + 1u][x], BITMAP_VFO_Default, sizeof(BITMAP_VFO_Default));
            sprintf(text, "B%u %.6s", bank, name);
            UI_PrintStringSmallNormal(text, x + 8u, 0, row + 1u);
        }
    }

    UI_PrintStringSmallNormal("F:BULK", 0, 0, 6);
    ST7565_BlitFullScreen();
}
