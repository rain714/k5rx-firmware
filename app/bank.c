#include "app/bank.h"

#include <stddef.h>

#include "app/main.h"
#include "audio.h"
#include "misc.h"
#include "settings.h"
#include "ui/ui.h"

BANK_Ui_t gBankUi;
bool gBankMainPreview;
static uint8_t gBankPreviewDualWatch;
static uint8_t gBankPreviewCrossBand;
static uint8_t gBankPreviewVfo;
static channel_t gBankPreviewMrChannel;
static channel_t gBankPreviewScreenChannel;

bool BANK_IsMember(channel_t channel)
{
    const SETTINGS_K5RX_ChannelRecord_t *record = SETTINGS_GetK5RXChannelRecord(channel);
    return record != NULL && record->bankCode == (gBankUiSelection + EEPROM_K5RX_BANK_MIN);
}

static channel_t BANK_FindChannel(int32_t start, int8_t direction)
{
    for (int32_t channel = start; channel >= 0 && channel < (int32_t)EEPROM_K5RX_CHANNEL_COUNT; channel += direction)
        if (BANK_IsMember((channel_t)channel))
            return (channel_t)channel;
    return CHANNEL_NONE;
}

static uint16_t BANK_GetOrdinal(channel_t selected, uint16_t *total)
{
    uint16_t ordinal = 0;
    uint16_t count = 0;

    for (channel_t channel = 0; channel < EEPROM_K5RX_CHANNEL_COUNT; channel++) {
        if (!BANK_IsMember(channel))
            continue;
        if (channel == selected)
            ordinal = count;
        count++;
    }
    if (total != NULL)
        *total = count;
    return ordinal;
}

static channel_t BANK_FindOrdinal(uint16_t target)
{
    uint16_t ordinal = 0;

    for (channel_t channel = 0; channel < EEPROM_K5RX_CHANNEL_COUNT; channel++) {
        if (!BANK_IsMember(channel))
            continue;
        if (ordinal++ == target)
            return channel;
    }
    return CHANNEL_NONE;
}

static void BANK_PageMove(int8_t direction)
{
    uint16_t total;
    const uint16_t ordinal = BANK_GetOrdinal(gBankUiChannel, &total);
    if (total == 0)
        return;

    int32_t target = (int32_t)ordinal + ((int32_t)direction * BANK_CHANNELS_PER_PAGE);
    if (target < 0)
        target = 0;
    else if (target >= total)
        target = total - 1;

    const channel_t channel = BANK_FindOrdinal((uint16_t)target);
    if (channel != CHANNEL_NONE)
        gBankUiChannel = channel;
}

void BANK_Open(void)
{
    gBankUiSelection = 0;
    gBankUiState = BANK_UI_BANK_LIST;
}

static void BANK_Apply(void)
{
    const uint8_t listBit = 1u << (gBankUiAction % 3u);
    const bool load = gBankUiAction < 3;

    for (channel_t channel = 0; channel < EEPROM_K5RX_CHANNEL_COUNT; channel++) {
        if (gMR_ChannelAttributes[channel].band > BAND7_470MHz)
            continue;
        const bool inBank = BANK_IsMember(channel);
        if (!load && !inBank)
            continue;
        SETTINGS_K5RXSetChannelScanList(channel, listBit, inBank);
    }
    gBankUiState = BANK_UI_BANK_LIST;
}

static void BANK_StartMainPreview(void)
{
    gBankPreviewDualWatch = gEeprom.DUAL_WATCH;
    gBankPreviewCrossBand = gEeprom.CROSS_BAND_RX_TX;
    gBankPreviewVfo = gEeprom.TX_VFO;
    gBankPreviewMrChannel = gEeprom.MrChannel[gBankPreviewVfo];
    gBankPreviewScreenChannel = gEeprom.ScreenChannel[gBankPreviewVfo];
    gEeprom.DUAL_WATCH = DUAL_WATCH_OFF;
    gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_OFF;
    channelMove(gBankUiChannel);
    gFlagReconfigureVfos = true;
    gUpdateStatus = true;
    gBankMainPreview = true;
    gRequestDisplayScreen = DISPLAY_MAIN;
}

bool BANK_MainPreviewProcessKey(KEY_Code_t key, bool pressed, bool held)
{
    if (!gBankMainPreview)
        return false;

    if (key == KEY_MENU) {
        if (pressed || held)
            return true;
    } else if (key != KEY_EXIT || !pressed || held) {
        return false;
    }

    gEeprom.DUAL_WATCH = gBankPreviewDualWatch;
    gEeprom.CROSS_BAND_RX_TX = gBankPreviewCrossBand;
    gBankMainPreview = false;
    gFlagReconfigureVfos = true;
    gUpdateStatus = true;

    if (key == KEY_MENU)
        SETTINGS_SaveVfoIndices();
    else {
        gEeprom.MrChannel[gBankPreviewVfo] = gBankPreviewMrChannel;
        gEeprom.ScreenChannel[gBankPreviewVfo] = gBankPreviewScreenChannel;
        gRequestDisplayScreen = DISPLAY_BANK;
        gUpdateDisplay = true;
    }

    gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
    return true;
}

void BANK_ProcessKeys(KEY_Code_t key, bool pressed, bool held)
{
    if (key == KEY_MENU && !pressed && !held) {
        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;

        if (gBankUiState == BANK_UI_CHANNEL_LIST) {
            BANK_StartMainPreview();
        } else if (gBankUiState == BANK_UI_BULK_SELECT) {
            BANK_Apply();
        } else {
            gBankUiChannel = BANK_FindChannel(0, 1);
            if (gBankUiChannel != CHANNEL_NONE)
                gBankUiState = BANK_UI_CHANNEL_LIST;
            else
                gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        }

        gUpdateDisplay = true;
        return;
    }

    if (!pressed || (held && key != KEY_UP && key != KEY_DOWN))
        return;

    if (!held)
        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;

    if (gBankUiState == BANK_UI_BULK_SELECT) {
        switch (key) {
        case KEY_UP:
            gBankUiAction = NUMBER_AddWithWraparound(gBankUiAction, -1, 0, 5);
            break;
        case KEY_DOWN:
            gBankUiAction = NUMBER_AddWithWraparound(gBankUiAction, 1, 0, 5);
            break;
        case KEY_MENU:
            break;
        case KEY_EXIT:
            gBankUiState = BANK_UI_BANK_LIST;
            break;
        default:
            gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
            break;
        }
        gUpdateDisplay = true;
        return;
    }

    if (gBankUiState == BANK_UI_CHANNEL_LIST) {
        switch (key) {
        case KEY_UP: {
            const channel_t channel = BANK_FindChannel((int32_t)gBankUiChannel - 1, -1);
            if (channel != CHANNEL_NONE)
                gBankUiChannel = channel;
            break;
        }
        case KEY_DOWN: {
            const channel_t channel = BANK_FindChannel((int32_t)gBankUiChannel + 1, 1);
            if (channel != CHANNEL_NONE)
                gBankUiChannel = channel;
            break;
        }
        case KEY_STAR:
            BANK_PageMove(-1);
            break;
        case KEY_F:
            BANK_PageMove(1);
            break;
        case KEY_1:
        case KEY_2:
        case KEY_3: {
            const uint8_t listBit = 1u << (key - KEY_1);
            const SETTINGS_K5RX_ChannelRecord_t *record = SETTINGS_GetK5RXChannelRecord(gBankUiChannel);
            SETTINGS_K5RXSetChannelScanList(gBankUiChannel, listBit,
                record == NULL || !(record->scanListMask & listBit));
            break;
        }
        case KEY_MENU:
            break;
        case KEY_EXIT:
            gBankUiState = BANK_UI_BANK_LIST;
            break;
        default:
            gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
            break;
        }
        gUpdateDisplay = true;
        return;
    }

    switch (key) {
    case KEY_UP:
        gBankUiSelection = NUMBER_AddWithWraparound(gBankUiSelection, -1, 0, EEPROM_K5RX_BANK_COUNT - 1);
        break;
    case KEY_DOWN:
        gBankUiSelection = NUMBER_AddWithWraparound(gBankUiSelection, 1, 0, EEPROM_K5RX_BANK_COUNT - 1);
        break;
    case KEY_1 ... KEY_8:
        gBankUiSelection = key - KEY_1;
        break;
    case KEY_MENU:
        break;
    case KEY_F:
        gBankUiAction = 0;
        gBankUiState = BANK_UI_BULK_SELECT;
        break;
    case KEY_EXIT:
        gRequestDisplayScreen = DISPLAY_MAIN;
        break;
    default:
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        break;
    }
    gUpdateDisplay = true;
}
