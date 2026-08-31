/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include <string.h>

#include "app/dtmf.h"
#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
#endif
#include "driver/bk1080.h"
#include "driver/bk4819.h"
#include "driver/eeprom.h"
#include "eeprom-layout.h"
#include "misc.h"
#include "settings.h"
#include "ui/menu.h"

#ifdef ENABLE_FEAT_F4HWN_RESET_CHANNEL
static const uint32_t gDefaultFrequencyTable[] =
{
    14500000,    //
    14550000,    //
    43300000,    //
    43320000,    //
    43350000     //
};
#endif

EEPROM_Config_t gEeprom = { 0 };

#ifdef ENABLE_K5RX_CUSTOM_EEPROM
static SETTINGS_K5RX_ChannelRecord_t gK5RXChannelCache[EEPROM_K5RX_CHANNEL_COUNT];
static bool gK5RXHeaderValid;

static uint16_t SETTINGS_K5RXChannelRecordOffset(const channel_t channel)
{
    return EEPROM_K5RX_CHANNEL_RECORD_BASE + (channel * EEPROM_K5RX_CHANNEL_RECORD_SIZE);
}

static uint16_t SETTINGS_K5RXChannelNameOffset(const channel_t channel)
{
    return EEPROM_K5RX_CHANNEL_NAME_BASE + (channel * EEPROM_K5RX_CHANNEL_NAME_STRIDE);
}

static uint16_t SETTINGS_K5RXReadU16(const uint8_t *data, const unsigned int offset)
{
    return (uint16_t)data[offset] | ((uint16_t)data[offset + 1u] << 8);
}

static uint32_t SETTINGS_K5RXReadU32(const uint8_t *data, const unsigned int offset)
{
    return (uint32_t)data[offset] |
        ((uint32_t)data[offset + 1u] << 8) |
        ((uint32_t)data[offset + 2u] << 16) |
        ((uint32_t)data[offset + 3u] << 24);
}

static channel_t SETTINGS_K5RXReadChannel(const uint8_t *data, const unsigned int offset)
{
    return (channel_t)SETTINGS_K5RXReadU16(data, offset);
}

static void SETTINGS_K5RXWriteU16(uint8_t *data, const unsigned int offset, const uint16_t value)
{
    data[offset] = (uint8_t)value;
    data[offset + 1u] = (uint8_t)(value >> 8);
}

static void SETTINGS_K5RXWriteU32(uint8_t *data, const unsigned int offset, const uint32_t value)
{
    data[offset] = (uint8_t)value;
    data[offset + 1u] = (uint8_t)(value >> 8);
    data[offset + 2u] = (uint8_t)(value >> 16);
    data[offset + 3u] = (uint8_t)(value >> 24);
}

static void SETTINGS_K5RXWriteChannel(uint8_t *data, const unsigned int offset, const channel_t channel)
{
    SETTINGS_K5RXWriteU16(data, offset, (uint16_t)channel);
}

static void SETTINGS_WriteK5RXBytes(uint16_t offset, const uint8_t *data, uint8_t size)
{
    uint8_t written = 0;

    if (!gK5RXHeaderValid)
        return;

    while (written < size) {
        uint8_t block[8];
        const uint16_t blockOffset = (offset + written) & ~7u;
        const uint8_t blockStart = (offset + written) - blockOffset;
        const uint8_t chunk = MIN((uint8_t)(sizeof(block) - blockStart), (uint8_t)(size - written));

        EEPROM_ReadBuffer(blockOffset, block, sizeof(block));
        memcpy(&block[blockStart], &data[written], chunk);
        EEPROM_WriteBuffer(blockOffset, block);
        written += chunk;
    }
}

static bool SETTINGS_HasValidK5RXHeader(void)
{
    uint8_t data[EEPROM_K5RX_HEADER_SIZE];

    EEPROM_ReadBuffer(EEPROM_K5RX_HEADER_BASE, data, sizeof(data));
    return SETTINGS_K5RXReadU32(data, EEPROM_K5RX_HEADER_MAGIC_OFFSET) == EEPROM_K5RX_MAGIC &&
        data[EEPROM_K5RX_HEADER_VERSION_OFFSET] == EEPROM_K5RX_SCHEMA_VERSION &&
        data[EEPROM_K5RX_HEADER_SIZE_OFFSET] == EEPROM_K5RX_HEADER_SIZE &&
        SETTINGS_K5RXReadU16(data, EEPROM_K5RX_HEADER_MUTABLE_END_OFFSET) == EEPROM_K5RX_MUTABLE_END &&
        SETTINGS_K5RXReadU16(data, EEPROM_K5RX_HEADER_CHANNEL_COUNT_OFFSET) == EEPROM_K5RX_CHANNEL_COUNT &&
        data[EEPROM_K5RX_HEADER_RECORD_SIZE_OFFSET] == EEPROM_K5RX_CHANNEL_RECORD_SIZE &&
        data[EEPROM_K5RX_HEADER_NAME_LENGTH_OFFSET] == EEPROM_K5RX_CHANNEL_NAME_LENGTH &&
        data[EEPROM_K5RX_HEADER_BANK_COUNT_OFFSET] == EEPROM_K5RX_BANK_COUNT;
}

bool SETTINGS_IsK5RXEEPROMReady(void)
{
    return gK5RXHeaderValid;
}

static void SETTINGS_SaveK5RXHeader(void)
{
    uint8_t data[EEPROM_K5RX_HEADER_SIZE];

    memset(data, 0xFF, sizeof(data));
    SETTINGS_K5RXWriteU32(data, EEPROM_K5RX_HEADER_MAGIC_OFFSET, EEPROM_K5RX_MAGIC);
    data[EEPROM_K5RX_HEADER_VERSION_OFFSET] = EEPROM_K5RX_SCHEMA_VERSION;
    data[EEPROM_K5RX_HEADER_SIZE_OFFSET] = EEPROM_K5RX_HEADER_SIZE;
    SETTINGS_K5RXWriteU16(data, EEPROM_K5RX_HEADER_CAPS_OFFSET,
        EEPROM_K5RX_CAP_400_CHANNELS | EEPROM_K5RX_CAP_CHANNEL_BANK_CODE);
    SETTINGS_K5RXWriteU16(data, EEPROM_K5RX_HEADER_MUTABLE_END_OFFSET, EEPROM_K5RX_MUTABLE_END);
    SETTINGS_K5RXWriteU16(data, EEPROM_K5RX_HEADER_CHANNEL_COUNT_OFFSET, EEPROM_K5RX_CHANNEL_COUNT);
    data[EEPROM_K5RX_HEADER_RECORD_SIZE_OFFSET] = EEPROM_K5RX_CHANNEL_RECORD_SIZE;
    data[EEPROM_K5RX_HEADER_NAME_LENGTH_OFFSET] = EEPROM_K5RX_CHANNEL_NAME_LENGTH;
    data[EEPROM_K5RX_HEADER_BANK_COUNT_OFFSET] = EEPROM_K5RX_BANK_COUNT;
    EEPROM_WriteBuffer(EEPROM_K5RX_HEADER_BASE, data);
    EEPROM_WriteBuffer(EEPROM_K5RX_HEADER_BASE + 8u, data + 8u);
    gK5RXHeaderValid = true;
}

static uint16_t SETTINGS_K5RXVfoRuntimeOffset(const uint8_t vfo, const uint8_t band)
{
    return EEPROM_K5RX_VFO_RUNTIME_BASE +
        (((band * 2u) + vfo) * EEPROM_K5RX_VFO_RUNTIME_SIZE);
}

bool SETTINGS_LoadK5RXVfoRuntime(const uint8_t vfo, const uint8_t band, VFO_Info_t *pVFO)
{
    uint8_t data[EEPROM_K5RX_VFO_RUNTIME_SIZE];
    uint32_t frequency;
    uint8_t codeType;
    uint8_t modulation;
    uint8_t step;

    if (!gK5RXHeaderValid || pVFO == NULL || vfo >= 2u || band >= EEPROM_K5RX_VFO_BAND_COUNT)
        return false;

    EEPROM_ReadBuffer(SETTINGS_K5RXVfoRuntimeOffset(vfo, band), data, sizeof(data));
    frequency = SETTINGS_K5RXReadU32(data, 0u);
    if (frequency == 0xFFFFFFFFu || RX_freq_check(frequency) < 0 || FREQUENCY_GetBand(frequency) != band)
        return false;

    codeType = data[5] & 0x03u;
    modulation = (data[5] >> 2) & 0x07u;
    step = data[6] & 0x1Fu;
    if (modulation >= MODULATION_UKNOWN)
        modulation = MODULATION_FM;
    if (step >= STEP_N_ELEM)
        step = STEP_12_5kHz;

    if (codeType == CODE_TYPE_CONTINUOUS_TONE) {
        if (data[4] >= ARRAY_SIZE(CTCSS_Options))
            codeType = CODE_TYPE_OFF;
    } else if (codeType == CODE_TYPE_DIGITAL || codeType == CODE_TYPE_REVERSE_DIGITAL) {
        if (data[4] >= ARRAY_SIZE(DCS_Options))
            codeType = CODE_TYPE_OFF;
    } else {
        codeType = CODE_TYPE_OFF;
    }

    RADIO_InitInfo(pVFO, FREQ_CHANNEL_FIRST + band, frequency);
    pVFO->freq_config_RX.CodeType = codeType;
    pVFO->freq_config_RX.Code = (codeType == CODE_TYPE_OFF) ? 0u : data[4];
    pVFO->Modulation = modulation;
    pVFO->CHANNEL_BANDWIDTH = (data[5] >> 5) & 0x01u;
    pVFO->Compander = (data[5] >> 6) & 0x03u;
    pVFO->STEP_SETTING = step;
    pVFO->StepFrequency = gStepFrequencyTable[step];
    return true;
}

static void SETTINGS_SaveK5RXVfoRuntime(const uint8_t vfo, const uint8_t band, const VFO_Info_t *pVFO)
{
    uint8_t data[EEPROM_K5RX_VFO_RUNTIME_SIZE];

    if (!gK5RXHeaderValid || pVFO == NULL || vfo >= 2u || band >= EEPROM_K5RX_VFO_BAND_COUNT ||
        RX_freq_check(pVFO->freq_config_RX.Frequency) < 0 || FREQUENCY_GetBand(pVFO->freq_config_RX.Frequency) != band)
        return;

    memset(data, 0xFF, sizeof(data));
    SETTINGS_K5RXWriteU32(data, 0u, pVFO->freq_config_RX.Frequency);
    data[4] = pVFO->freq_config_RX.Code;
    data[5] = ((uint8_t)pVFO->freq_config_RX.CodeType & 0x03u) |
        (((uint8_t)pVFO->Modulation & 0x07u) << 2) |
        ((pVFO->CHANNEL_BANDWIDTH & 0x01u) << 5) |
        ((pVFO->Compander & 0x03u) << 6);
    data[6] = (uint8_t)pVFO->STEP_SETTING & 0x1Fu;
    EEPROM_WriteBuffer(SETTINGS_K5RXVfoRuntimeOffset(vfo, band), data);
}

static channel_t SETTINGS_ValidateK5RXChannel(const channel_t channel, const channel_t defaultChannel)
{
    return IS_VALID_CHANNEL(channel) ? channel : defaultChannel;
}

static channel_t SETTINGS_ValidateK5RXMrChannel(const channel_t channel)
{
    return IS_MR_CHANNEL(channel) ? channel : MR_CHANNEL_FIRST;
}

static channel_t SETTINGS_ValidateK5RXFreqChannel(const channel_t channel)
{
    const channel_t defaultChannel = FREQ_CHANNEL_FIRST + BAND6_400MHz;
    return IS_FREQ_CHANNEL(channel) ? channel : defaultChannel;
}

static channel_t SETTINGS_ValidateK5RXPriorityChannel(const channel_t channel)
{
    return IS_MR_CHANNEL(channel) ? channel : CHANNEL_NONE;
}

static void SETTINGS_SetK5RXDefaults(void)
{
    const channel_t defaultChannel = FREQ_CHANNEL_FIRST + BAND6_400MHz;

    memset(&gEeprom, 0, sizeof(gEeprom));
    for (unsigned int vfo = 0; vfo < 2; vfo++) {
        gEeprom.ScreenChannel[vfo] = defaultChannel;
        gEeprom.MrChannel[vfo] = MR_CHANNEL_FIRST;
        gEeprom.FreqChannel[vfo] = defaultChannel;
    }
    gEeprom.CHAN_1_CALL = MR_CHANNEL_FIRST;
    gEeprom.SQUELCH_LEVEL = 1;
    gEeprom.KEY_LOCK = false;
    gEeprom.MIC_SENSITIVITY = 4;
    gEeprom.BACKLIGHT_MAX = 10;
    gEeprom.BACKLIGHT_MIN = 0;
    gEeprom.CHANNEL_DISPLAY_MODE = MDF_FREQUENCY;
    gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_OFF;
    gEeprom.BATTERY_SAVE = 4;
    gEeprom.DUAL_WATCH = DUAL_WATCH_CHAN_A;
#ifdef ENABLE_FEAT_F4HWN
    gCB = gEeprom.CROSS_BAND_RX_TX;
    gDW = gEeprom.DUAL_WATCH;
#endif
    gEeprom.BACKLIGHT_TIME = 12;
    gEeprom.TAIL_TONE_ELIMINATION = false;
    gEeprom.VFO_OPEN = true;
    gEeprom.BEEP_CONTROL = true;
    gEeprom.KEY_M_LONG_PRESS_ACTION = ACTION_OPT_NONE;
    gEeprom.KEY_1_SHORT_PRESS_ACTION = ACTION_OPT_MONITOR;
    gEeprom.KEY_1_LONG_PRESS_ACTION = ACTION_OPT_NONE;
    gEeprom.KEY_2_SHORT_PRESS_ACTION = ACTION_OPT_SCAN;
    gEeprom.KEY_2_LONG_PRESS_ACTION = ACTION_OPT_NONE;
    gEeprom.SCAN_RESUME_MODE = 14;
    gEeprom.AUTO_KEYPAD_LOCK = false;
    gEeprom.POWER_ON_DISPLAY_MODE = POWER_ON_DISPLAY_MODE_VOLTAGE;
    gEeprom.TX_VFO = 0;
    gEeprom.RX_VFO = 0;
    gEeprom.BATTERY_TYPE = BATTERY_TYPE_1600_MAH;
    gEeprom.CURRENT_STATE = 0;
    gEeprom.CURRENT_LIST = 0;
    gEeprom.SCAN_LIST_DEFAULT = 0;
    for (unsigned int i = 0; i < 3; i++) {
        gEeprom.SCAN_LIST_ENABLED[i] = true;
        gEeprom.SCANLIST_PRIORITY_CH1[i] = CHANNEL_NONE;
        gEeprom.SCANLIST_PRIORITY_CH2[i] = CHANNEL_NONE;
    }
    gSetting_350EN = true;
    gSetting_live_DTMF_decoder = false;
    gSetting_battery_text = 2;
#ifdef ENABLE_K5RX_FAST_SCAN
    gSetting_fast_scan_mode = FAST_SCAN_MODE_FAST;
#endif
    gSetting_backlight_on_tx_rx = BACKLIGHT_ON_TR_OFF;
#ifdef ENABLE_AM_FIX
    gSetting_AM_fix = true;
#endif
#ifdef ENABLE_FEAT_F4HWN_NARROWER
    gSetting_set_nfm = false;
#endif
#ifdef ENABLE_FEAT_F4HWN_INV
    gSetting_set_inv = false;
#endif
#ifdef ENABLE_FEAT_F4HWN_CTR
    gSetting_set_ctr = 10;
#endif
    gSetting_set_lck = false;
    gSetting_set_met = false;
    gSetting_set_gui = false;
    gSetting_set_tmr = false;
#ifdef ENABLE_FEAT_F4HWN_SLEEP
    gSetting_set_off = 0;
#endif
    gSetting_set_ptt_session = false;
    gEeprom.KEY_LOCK_PTT = false;
    memset(gCustomAesKey, 0xFF, sizeof(gCustomAesKey));
    bHasCustomAesKey = false;
}

static void SETTINGS_LoadK5RXVfoIndices(void)
{
    const channel_t defaultChannel = FREQ_CHANNEL_FIRST + BAND6_400MHz;

    for (unsigned int vfo = 0; vfo < 2; vfo++) {
        uint8_t data[EEPROM_K5RX_VFO_INDEX_RECORD_SIZE];
        const uint16_t offset = EEPROM_K5RX_VFO_INDEX_BASE + (vfo * EEPROM_K5RX_VFO_INDEX_RECORD_SIZE);

        EEPROM_ReadBuffer(offset, data, sizeof(data));
        gEeprom.ScreenChannel[vfo] = SETTINGS_ValidateK5RXChannel(SETTINGS_K5RXReadChannel(data, 0u), defaultChannel);
        gEeprom.MrChannel[vfo] = SETTINGS_ValidateK5RXMrChannel(SETTINGS_K5RXReadChannel(data, 2u));
        gEeprom.FreqChannel[vfo] = SETTINGS_ValidateK5RXFreqChannel(SETTINGS_K5RXReadChannel(data, 4u));
    }
}

static void SETTINGS_SaveK5RXVfoIndices(void)
{
    if (!gK5RXHeaderValid)
        return;

    for (unsigned int vfo = 0; vfo < 2; vfo++) {
        uint8_t data[EEPROM_K5RX_VFO_INDEX_RECORD_SIZE];
        const uint16_t offset = EEPROM_K5RX_VFO_INDEX_BASE + (vfo * EEPROM_K5RX_VFO_INDEX_RECORD_SIZE);

        memset(data, 0xFF, sizeof(data));
        SETTINGS_K5RXWriteChannel(data, 0u, gEeprom.ScreenChannel[vfo]);
        SETTINGS_K5RXWriteChannel(data, 2u, gEeprom.MrChannel[vfo]);
        SETTINGS_K5RXWriteChannel(data, 4u, gEeprom.FreqChannel[vfo]);
        SETTINGS_K5RXWriteChannel(data, 6u, CHANNEL_NONE);
        EEPROM_WriteBuffer(offset, data);
    }
}

static void SETTINGS_LoadK5RXScanSettings(void)
{
    uint8_t data[8];

    EEPROM_ReadBuffer(EEPROM_K5RX_SETTINGS_SCAN_BASE, data, sizeof(data));
    gEeprom.SCAN_LIST_DEFAULT = (data[0] < 6) ? data[0] : 0;
    for (unsigned int i = 0; i < 3; i++)
        gEeprom.SCAN_LIST_ENABLED[i] = (data[1] >> i) & 1u;

    EEPROM_ReadBuffer(EEPROM_K5RX_SETTINGS_SCAN_BASE + 8u, data, sizeof(data));
    gEeprom.SCANLIST_PRIORITY_CH1[0] = SETTINGS_ValidateK5RXPriorityChannel(SETTINGS_K5RXReadChannel(data, 0u));
    gEeprom.SCANLIST_PRIORITY_CH2[0] = SETTINGS_ValidateK5RXPriorityChannel(SETTINGS_K5RXReadChannel(data, 2u));
    gEeprom.SCANLIST_PRIORITY_CH1[1] = SETTINGS_ValidateK5RXPriorityChannel(SETTINGS_K5RXReadChannel(data, 4u));
    gEeprom.SCANLIST_PRIORITY_CH2[1] = SETTINGS_ValidateK5RXPriorityChannel(SETTINGS_K5RXReadChannel(data, 6u));

    EEPROM_ReadBuffer(EEPROM_K5RX_SETTINGS_SCAN_BASE + 16u, data, sizeof(data));
    gEeprom.SCANLIST_PRIORITY_CH1[2] = SETTINGS_ValidateK5RXPriorityChannel(SETTINGS_K5RXReadChannel(data, 0u));
    gEeprom.SCANLIST_PRIORITY_CH2[2] = SETTINGS_ValidateK5RXPriorityChannel(SETTINGS_K5RXReadChannel(data, 2u));
}

static void SETTINGS_SaveK5RXScanSettings(void)
{
    uint8_t data[8];

    if (!gK5RXHeaderValid)
        return;

    memset(data, 0xFF, sizeof(data));
    data[0] = gEeprom.SCAN_LIST_DEFAULT;
    data[1] = 0;
    for (unsigned int i = 0; i < 3; i++)
        if (gEeprom.SCAN_LIST_ENABLED[i])
            data[1] |= 1u << i;
    EEPROM_WriteBuffer(EEPROM_K5RX_SETTINGS_SCAN_BASE, data);

    memset(data, 0xFF, sizeof(data));
    SETTINGS_K5RXWriteChannel(data, 0u, gEeprom.SCANLIST_PRIORITY_CH1[0]);
    SETTINGS_K5RXWriteChannel(data, 2u, gEeprom.SCANLIST_PRIORITY_CH2[0]);
    SETTINGS_K5RXWriteChannel(data, 4u, gEeprom.SCANLIST_PRIORITY_CH1[1]);
    SETTINGS_K5RXWriteChannel(data, 6u, gEeprom.SCANLIST_PRIORITY_CH2[1]);
    EEPROM_WriteBuffer(EEPROM_K5RX_SETTINGS_SCAN_BASE + 8u, data);

    memset(data, 0xFF, sizeof(data));
    SETTINGS_K5RXWriteChannel(data, 0u, gEeprom.SCANLIST_PRIORITY_CH1[2]);
    SETTINGS_K5RXWriteChannel(data, 2u, gEeprom.SCANLIST_PRIORITY_CH2[2]);
    EEPROM_WriteBuffer(EEPROM_K5RX_SETTINGS_SCAN_BASE + 16u, data);
}

static void SETTINGS_LoadK5RXSettings(void)
{
    uint8_t data[8];

    SETTINGS_SetK5RXDefaults();
    EEPROM_ReadBuffer(EEPROM_K5RX_SETTINGS_GENERAL_BASE, data, sizeof(data));
    gEeprom.CHAN_1_CALL = SETTINGS_ValidateK5RXMrChannel(SETTINGS_K5RXReadChannel(data, 0u));
    gEeprom.SQUELCH_LEVEL = (data[2] < 10) ? data[2] : 1;
    gEeprom.KEY_LOCK = (data[3] < 2) ? data[3] : false;
    gEeprom.MIC_SENSITIVITY = (data[4] < 5) ? data[4] : 4;
    gEeprom.BACKLIGHT_MAX = (data[5] & 0x0Fu) <= 10 ? (data[5] & 0x0Fu) : 10;
    gEeprom.BACKLIGHT_MIN = (data[5] >> 4) < gEeprom.BACKLIGHT_MAX ? (data[5] >> 4) : 0;
    gEeprom.CHANNEL_DISPLAY_MODE = (data[6] < 4) ? data[6] : MDF_FREQUENCY;
    gEeprom.CROSS_BAND_RX_TX = (data[7] < 3) ? data[7] : CROSS_BAND_OFF;

    EEPROM_ReadBuffer(EEPROM_K5RX_SETTINGS_GENERAL_BASE + 8u, data, sizeof(data));
    gEeprom.BATTERY_SAVE = (data[0] < 5) ? data[0] : 4;
    gEeprom.DUAL_WATCH = (data[1] < 3) ? data[1] : DUAL_WATCH_CHAN_A;
#ifdef ENABLE_FEAT_F4HWN
    gCB = gEeprom.CROSS_BAND_RX_TX;
    gDW = gEeprom.DUAL_WATCH;
#endif
    gEeprom.BACKLIGHT_TIME = (data[2] < 62) ? data[2] : 12;
    gEeprom.TAIL_TONE_ELIMINATION = data[3] & 0x01u;
#ifdef ENABLE_FEAT_F4HWN_NARROWER
    gSetting_set_nfm = (data[3] >> 1) & 0x01u;
#endif
    gEeprom.VFO_OPEN = data[4] < 2 ? data[4] : true;
    gEeprom.BEEP_CONTROL = data[5] & 0x01u;
    gEeprom.KEY_M_LONG_PRESS_ACTION = ((data[5] >> 1) < ACTION_OPT_LEN) ? (data[5] >> 1) : ACTION_OPT_NONE;
    gEeprom.SCAN_RESUME_MODE = (data[6] < 105) ? data[6] : 14;
    gEeprom.AUTO_KEYPAD_LOCK = (data[7] < 2) ? data[7] : false;

    EEPROM_ReadBuffer(EEPROM_K5RX_SETTINGS_GENERAL_BASE + 16u, data, sizeof(data));
    gEeprom.POWER_ON_DISPLAY_MODE = (data[0] < 4) ? data[0] : POWER_ON_DISPLAY_MODE_VOLTAGE;
    gEeprom.TX_VFO = (data[1] < 2) ? data[1] : 0;
    gEeprom.RX_VFO = gEeprom.TX_VFO;
    gEeprom.BATTERY_TYPE = (data[2] < BATTERY_TYPE_UNKNOWN) ? data[2] : BATTERY_TYPE_1600_MAH;
    gSetting_350EN = data[3] < 2 ? data[3] : true;
    gSetting_battery_text = (data[4] <= 2) ? data[4] : 2;
    gEeprom.CURRENT_STATE = (data[5] <= 5) ? data[5] : 0;
    gEeprom.CURRENT_LIST = (data[6] < 6) ? data[6] : 0;

    EEPROM_ReadBuffer(EEPROM_K5RX_SETTINGS_GENERAL_BASE + 24u, data, sizeof(data));
    gEeprom.KEY_1_SHORT_PRESS_ACTION = (data[0] < ACTION_OPT_LEN) ? data[0] : ACTION_OPT_MONITOR;
    gEeprom.KEY_1_LONG_PRESS_ACTION = (data[1] < ACTION_OPT_LEN) ? data[1] : ACTION_OPT_NONE;
    gEeprom.KEY_2_SHORT_PRESS_ACTION = (data[2] < ACTION_OPT_LEN) ? data[2] : ACTION_OPT_SCAN;
    gEeprom.KEY_2_LONG_PRESS_ACTION = (data[3] < ACTION_OPT_LEN) ? data[3] : ACTION_OPT_NONE;
#ifdef ENABLE_AM_FIX
    gSetting_AM_fix = data[4] < 2 ? data[4] : true;
#endif
    gSetting_live_DTMF_decoder = data[5] != 0xFFu && (data[5] & EEPROM_K5RX_SETTINGS_FLAG_LIVE_DTMF) != 0;
    gSetting_backlight_on_tx_rx = data[6] <= 3 ? data[6] : BACKLIGHT_ON_TR_OFF;
#ifdef ENABLE_K5RX_FAST_SCAN
    const uint8_t fastScanMode = data[7] & 0x03u;
    gSetting_fast_scan_mode = fastScanMode < FAST_SCAN_MODE_COUNT ?
        (FastScanMode_t)fastScanMode : FAST_SCAN_MODE_FAST;
#endif

    EEPROM_ReadBuffer(EEPROM_K5RX_SETTINGS_F4HWN_BASE, data, sizeof(data));
#ifdef ENABLE_FEAT_F4HWN_INV
    gSetting_set_inv = data[0] & 0x01u;
#endif
    gSetting_set_lck = (data[0] >> 1) & 0x01u;
    gSetting_set_met = (data[0] >> 2) & 0x01u;
    gSetting_set_gui = (data[0] >> 3) & 0x01u;
#ifdef ENABLE_FEAT_F4HWN_CTR
    gSetting_set_ctr = (data[1] > 0 && data[1] < 16) ? data[1] : 10;
#endif
#ifdef ENABLE_FEAT_F4HWN_SLEEP
    gSetting_set_off = data[2] <= 60 ? data[2] : 0;
#endif
    gSetting_set_tmr = data[3] & 0x01u;
    gSetting_set_ptt_session = false;
    gEeprom.KEY_LOCK_PTT = gSetting_set_lck;
    SETTINGS_LoadK5RXScanSettings();
}

static void SETTINGS_SaveK5RXSettings(void)
{
    uint8_t data[8];

    if (!gK5RXHeaderValid)
        return;

    memset(data, 0xFF, sizeof(data));
    SETTINGS_K5RXWriteChannel(data, 0u, gEeprom.CHAN_1_CALL);
    data[2] = gEeprom.SQUELCH_LEVEL;
    data[3] = gEeprom.KEY_LOCK;
    data[4] = gEeprom.MIC_SENSITIVITY;
    data[5] = (gEeprom.BACKLIGHT_MIN << 4) | (gEeprom.BACKLIGHT_MAX & 0x0Fu);
    data[6] = gEeprom.CHANNEL_DISPLAY_MODE;
#ifdef ENABLE_FEAT_F4HWN
    data[7] = gSaveRxMode ? gEeprom.CROSS_BAND_RX_TX : gCB;
#else
    data[7] = gEeprom.CROSS_BAND_RX_TX;
#endif
    EEPROM_WriteBuffer(EEPROM_K5RX_SETTINGS_GENERAL_BASE, data);

    memset(data, 0xFF, sizeof(data));
    data[0] = gEeprom.BATTERY_SAVE;
#ifdef ENABLE_FEAT_F4HWN
    data[1] = gSaveRxMode ? gEeprom.DUAL_WATCH : gDW;
#else
    data[1] = gEeprom.DUAL_WATCH;
#endif
    data[2] = gEeprom.BACKLIGHT_TIME;
    data[3] = gEeprom.TAIL_TONE_ELIMINATION & 0x01u;
#ifdef ENABLE_FEAT_F4HWN_NARROWER
    data[3] |= (gSetting_set_nfm & 0x01u) << 1;
#endif
    data[4] = gEeprom.VFO_OPEN;
    data[5] = (gEeprom.BEEP_CONTROL & 0x01u) | (gEeprom.KEY_M_LONG_PRESS_ACTION << 1);
    data[6] = gEeprom.SCAN_RESUME_MODE;
    data[7] = gEeprom.AUTO_KEYPAD_LOCK;
    EEPROM_WriteBuffer(EEPROM_K5RX_SETTINGS_GENERAL_BASE + 8u, data);

    memset(data, 0xFF, sizeof(data));
    data[0] = gEeprom.POWER_ON_DISPLAY_MODE;
    data[1] = gEeprom.TX_VFO;
    data[2] = gEeprom.BATTERY_TYPE;
    data[3] = gSetting_350EN;
    data[4] = gSetting_battery_text;
    data[5] = gEeprom.CURRENT_STATE;
    data[6] = gEeprom.CURRENT_LIST;
    EEPROM_WriteBuffer(EEPROM_K5RX_SETTINGS_GENERAL_BASE + 16u, data);

    memset(data, 0xFF, sizeof(data));
    data[0] = gEeprom.KEY_1_SHORT_PRESS_ACTION;
    data[1] = gEeprom.KEY_1_LONG_PRESS_ACTION;
    data[2] = gEeprom.KEY_2_SHORT_PRESS_ACTION;
    data[3] = gEeprom.KEY_2_LONG_PRESS_ACTION;
#ifdef ENABLE_AM_FIX
    data[4] = gSetting_AM_fix;
#endif
    data[5] = gSetting_live_DTMF_decoder ? EEPROM_K5RX_SETTINGS_FLAG_LIVE_DTMF : 0u;
    data[6] = gSetting_backlight_on_tx_rx;
#ifdef ENABLE_K5RX_FAST_SCAN
    data[7] = (uint8_t)(0xFCu | ((uint8_t)gSetting_fast_scan_mode & 0x03u));
#endif
    EEPROM_WriteBuffer(EEPROM_K5RX_SETTINGS_GENERAL_BASE + 24u, data);

    memset(data, 0xFF, sizeof(data));
    data[0] = (gSetting_set_inv << 0) | (gSetting_set_lck << 1) |
        (gSetting_set_met << 2) | (gSetting_set_gui << 3);
    data[1] = gSetting_set_ctr;
#ifdef ENABLE_FEAT_F4HWN_SLEEP
    data[2] = gSetting_set_off;
#endif
    data[3] = gSetting_set_tmr;
    EEPROM_WriteBuffer(EEPROM_K5RX_SETTINGS_F4HWN_BASE, data);

    SETTINGS_SaveK5RXScanSettings();
}

static bool SETTINGS_DecodeK5RXChannelRecord(
    SETTINGS_K5RX_ChannelRecord_t *out,
    const uint8_t in[EEPROM_K5RX_CHANNEL_RECORD_SIZE])
{
    uint32_t frequency;
    uint8_t code;
    uint8_t codeType;
    uint8_t modulation;
    uint8_t step;
    uint8_t bankCode;

    if (out == NULL)
        return false;

    frequency = (uint32_t)in[0] |
        ((uint32_t)in[1] << 8) |
        ((uint32_t)in[2] << 16) |
        ((uint32_t)(in[3] & 0x07u) << 24);
    if (frequency == 0 || frequency == EEPROM_K5RX_CHANNEL_FREQ_MASK || RX_freq_check(frequency) < 0)
        return false;

    codeType = (in[3] >> 3) & 0x03u;
    code = in[4] & 0x7Fu;
    modulation = in[5] & 0x07u;
    step = (in[5] >> 3) & 0x1Fu;
    bankCode = (in[6] & EEPROM_K5RX_CHANNEL_BANK_MASK) >> EEPROM_K5RX_CHANNEL_BANK_SHIFT;

    switch (codeType) {
    default:
    case CODE_TYPE_OFF:
        codeType = CODE_TYPE_OFF;
        code = 0;
        break;
    case CODE_TYPE_CONTINUOUS_TONE:
        if (code >= ARRAY_SIZE(CTCSS_Options)) {
            codeType = CODE_TYPE_OFF;
            code = 0;
        }
        break;
    case CODE_TYPE_DIGITAL:
    case CODE_TYPE_REVERSE_DIGITAL:
        if (code >= ARRAY_SIZE(DCS_Options)) {
            codeType = CODE_TYPE_OFF;
            code = 0;
        }
        break;
    }
    if (modulation >= MODULATION_UKNOWN)
        modulation = MODULATION_FM;
    if (step >= STEP_N_ELEM)
        step = STEP_12_5kHz;
    if (bankCode > EEPROM_K5RX_BANK_MAX)
        bankCode = EEPROM_K5RX_BANK_UNBANKED;

    out->frequency = frequency;
    out->code = code;
    out->codeType = codeType;
    out->modulation = modulation;
    out->scanListMask = in[6] & EEPROM_K5RX_CHANNEL_SCAN_MASK;
    out->bankCode = bankCode;
    out->bandwidth = (in[3] >> 5) & 0x01u;
    out->compander = ((in[3] >> 6) & 0x01u) ? 2u : 0u;
    out->step = step;
    return true;
}

static void SETTINGS_EncodeK5RXChannelRecord(
    uint8_t out[EEPROM_K5RX_CHANNEL_RECORD_SIZE],
    const SETTINGS_K5RX_ChannelRecord_t *in)
{
    memset(out, 0, EEPROM_K5RX_CHANNEL_RECORD_SIZE);
    out[0] = (uint8_t)(in->frequency >> 0);
    out[1] = (uint8_t)(in->frequency >> 8);
    out[2] = (uint8_t)(in->frequency >> 16);
    out[3] = (uint8_t)((in->frequency >> 24) & 0x07u);
    out[3] |= (uint8_t)(((uint8_t)in->codeType & 0x03u) << 3);
    out[3] |= (uint8_t)((in->bandwidth & 0x01u) << 5);
    out[3] |= (uint8_t)((in->compander ? 1u : 0u) << 6);
    out[4] = in->code & 0x7Fu;
    out[5] = (uint8_t)(((uint8_t)in->modulation & 0x07u) | (((uint8_t)in->step & 0x1Fu) << 3));
    out[6] = (uint8_t)((in->scanListMask & EEPROM_K5RX_CHANNEL_SCAN_MASK) |
        ((in->bankCode << EEPROM_K5RX_CHANNEL_BANK_SHIFT) & EEPROM_K5RX_CHANNEL_BANK_MASK));
    out[7] = 0;
}

static uint8_t SETTINGS_BuildK5RXScanListMask(const VFO_Info_t *pVFO)
{
    uint8_t mask = 0;

    if (pVFO == NULL)
        return 0;
    if (pVFO->SCANLIST1_PARTICIPATION)
        mask |= 1u << 0;
    if (pVFO->SCANLIST2_PARTICIPATION)
        mask |= 1u << 1;
    if (pVFO->SCANLIST3_PARTICIPATION)
        mask |= 1u << 2;
    return mask;
}

static ChannelAttributes_t SETTINGS_K5RXChannelAttributes(const SETTINGS_K5RX_ChannelRecord_t *record)
{
    ChannelAttributes_t att = {0};

    if (record == NULL) {
        att.__val = 0xFFu;
        return att;
    }
    att.band = FREQUENCY_GetBand(record->frequency);
    att.compander = record->compander;
    att.scanlist1 = !!(record->scanListMask & (1u << 0));
    att.scanlist2 = !!(record->scanListMask & (1u << 1));
    att.scanlist3 = !!(record->scanListMask & (1u << 2));
    return att;
}

static void SETTINGS_SaveK5RXChannelRecord(channel_t channel, const VFO_Info_t *pVFO)
{
    SETTINGS_K5RX_ChannelRecord_t record;
    uint8_t data[EEPROM_K5RX_CHANNEL_RECORD_SIZE];

    if (!gK5RXHeaderValid || !IS_MR_CHANNEL(channel) || pVFO == NULL ||
        RX_freq_check(pVFO->freq_config_RX.Frequency) < 0)
        return;

    memset(&record, 0, sizeof(record));
    record.frequency = pVFO->freq_config_RX.Frequency;
    record.code = pVFO->freq_config_RX.Code;
    record.codeType = pVFO->freq_config_RX.CodeType;
    record.modulation = (pVFO->Modulation < MODULATION_UKNOWN) ? pVFO->Modulation : MODULATION_FM;
    record.scanListMask = SETTINGS_BuildK5RXScanListMask(pVFO);
    record.bankCode = gK5RXChannelCache[channel].frequency != 0 ?
        gK5RXChannelCache[channel].bankCode : EEPROM_K5RX_BANK_UNBANKED;
    record.bandwidth = pVFO->CHANNEL_BANDWIDTH;
    record.compander = pVFO->Compander;
    record.step = (pVFO->STEP_SETTING < STEP_N_ELEM) ? pVFO->STEP_SETTING : STEP_12_5kHz;

    SETTINGS_EncodeK5RXChannelRecord(data, &record);
    EEPROM_WriteBuffer(SETTINGS_K5RXChannelRecordOffset(channel), data);
    gK5RXChannelCache[channel] = record;
    gMR_ChannelAttributes[channel] = SETTINGS_K5RXChannelAttributes(&record);
}

static void SETTINGS_LoadK5RXChannelAttributes(void)
{
    uint8_t data[EEPROM_K5RX_CHANNEL_RECORD_SIZE];

    memset(gK5RXChannelCache, 0, sizeof(gK5RXChannelCache));
    memset(gMR_ChannelAttributes, 0xFF, sizeof(gMR_ChannelAttributes));
    memset(gMR_ChannelExclude, 0, sizeof(gMR_ChannelExclude));
    for (channel_t channel = MR_CHANNEL_FIRST; channel <= MR_CHANNEL_LAST; channel++) {
        EEPROM_ReadBuffer(SETTINGS_K5RXChannelRecordOffset(channel), data, sizeof(data));
        if (SETTINGS_DecodeK5RXChannelRecord(&gK5RXChannelCache[channel], data))
            gMR_ChannelAttributes[channel] = SETTINGS_K5RXChannelAttributes(&gK5RXChannelCache[channel]);
    }
}

const SETTINGS_K5RX_ChannelRecord_t *SETTINGS_GetK5RXChannelRecord(channel_t channel)
{
    if (!IS_MR_CHANNEL(channel) || gK5RXChannelCache[channel].frequency == 0)
        return NULL;
    return &gK5RXChannelCache[channel];
}

#ifdef ENABLE_K5RX_BANK_UI
void SETTINGS_K5RXSetChannelScanList(channel_t channel, uint8_t listBit, bool enabled)
{
    if (!gK5RXHeaderValid || !IS_MR_CHANNEL(channel) ||
        gK5RXChannelCache[channel].frequency == 0 || (listBit & ~EEPROM_K5RX_CHANNEL_SCAN_MASK) != 0)
        return;

    SETTINGS_K5RX_ChannelRecord_t *record = &gK5RXChannelCache[channel];
    if (!!(record->scanListMask & listBit) != enabled) {
        uint8_t data[EEPROM_K5RX_CHANNEL_RECORD_SIZE];
        record->scanListMask ^= listBit;
        SETTINGS_EncodeK5RXChannelRecord(data, record);
        EEPROM_WriteBuffer(SETTINGS_K5RXChannelRecordOffset(channel), data);
    }
    gMR_ChannelAttributes[channel] = SETTINGS_K5RXChannelAttributes(record);
}
#endif

bool SETTINGS_LoadK5RXChannel(channel_t channel, VFO_Info_t *pVFO)
{
    const SETTINGS_K5RX_ChannelRecord_t *record = SETTINGS_GetK5RXChannelRecord(channel);

    if (pVFO == NULL || record == NULL)
        return false;

    const ChannelAttributes_t att = gMR_ChannelAttributes[channel];
    memset(pVFO, 0, sizeof(*pVFO));
    pVFO->freq_config_RX.Frequency = record->frequency;
    pVFO->freq_config_TX.Frequency = record->frequency;
    pVFO->freq_config_RX.CodeType = record->codeType;
    pVFO->freq_config_TX.CodeType = CODE_TYPE_OFF;
    pVFO->freq_config_RX.Code = record->code;
    pVFO->freq_config_TX.Code = 0;
    pVFO->pRX = &pVFO->freq_config_RX;
    pVFO->pTX = &pVFO->freq_config_TX;
    pVFO->TX_OFFSET_FREQUENCY = 0;
    pVFO->TX_OFFSET_FREQUENCY_DIRECTION = TX_OFFSET_FREQUENCY_DIRECTION_OFF;
    pVFO->StepFrequency = gStepFrequencyTable[record->step];
    pVFO->CHANNEL_SAVE = channel;
    pVFO->STEP_SETTING = record->step;
    pVFO->FrequencyReverse = false;
    pVFO->CHANNEL_BANDWIDTH = record->bandwidth;
    pVFO->SCANLIST1_PARTICIPATION = att.scanlist1;
    pVFO->SCANLIST2_PARTICIPATION = att.scanlist2;
    pVFO->SCANLIST3_PARTICIPATION = att.scanlist3;
    pVFO->Band = att.band;
    pVFO->DTMF_PTT_ID_TX_MODE = PTT_ID_OFF;
    pVFO->BUSY_CHANNEL_LOCK = false;
    pVFO->Modulation = record->modulation;
    pVFO->Compander = record->compander;
    return true;
}

static void SETTINGS_ResetK5RXMutable(bool bIsAll)
{
    uint8_t blank[8];
    const bool resetAll = bIsAll || !gK5RXHeaderValid;
    const uint16_t start = resetAll ? EEPROM_K5RX_CHANNEL_RECORD_BASE : EEPROM_K5RX_VFO_STATE_BASE;
    const uint16_t end = resetAll ? EEPROM_K5RX_MUTABLE_END : EEPROM_K5RX_FM_END;

    memset(blank, 0xFF, sizeof(blank));
    for (uint16_t offset = start; offset < end; offset += sizeof(blank)) {
        blank[0] = (offset == EEPROM_K5RX_WELCOME_LINE0_BASE || offset == EEPROM_K5RX_WELCOME_LINE1_BASE) ? 0 : 0xFFu;
        EEPROM_WriteBuffer(offset, blank);
    }

    SETTINGS_SetK5RXDefaults();
    SETTINGS_SaveK5RXHeader();
    SETTINGS_SaveK5RXSettings();
    SETTINGS_SaveK5RXVfoIndices();
    SETTINGS_LoadK5RXChannelAttributes();
}

#ifdef ENABLE_FMRADIO
static void SETTINGS_LoadK5RXFM(void)
{
    EEPROM_ReadBuffer(EEPROM_K5RX_FM_CONFIG_BASE, gEeprom.FM_ConfigRaw, sizeof(gEeprom.FM_ConfigRaw));
    if (gEeprom.FM_SelectedFrequency == 0xFFFFu)
        memset(gEeprom.FM_ConfigRaw, 0, sizeof(gEeprom.FM_ConfigRaw));

    const uint16_t lo = BK1080_GetFreqLoLimit(gEeprom.FM_Band);
    if (gEeprom.FM_SelectedFrequency < lo ||
        gEeprom.FM_SelectedFrequency > BK1080_GetFreqHiLimit(gEeprom.FM_Band))
        gEeprom.FM_SelectedFrequency = lo;

    EEPROM_ReadBuffer(EEPROM_K5RX_FM_CHANNEL_BASE, gFM_Channels, sizeof(gFM_Channels));
    FM_ConfigureChannelState();
}
#endif

static void SETTINGS_InitK5RXEEPROM(void)
{
    gK5RXHeaderValid = false;
    SETTINGS_SetK5RXDefaults();
    memset(gK5RXChannelCache, 0, sizeof(gK5RXChannelCache));
    memset(gMR_ChannelAttributes, 0xFF, sizeof(gMR_ChannelAttributes));
    memset(gMR_ChannelExclude, 0, sizeof(gMR_ChannelExclude));

    if (!SETTINGS_HasValidK5RXHeader())
        return;

    gK5RXHeaderValid = true;
    SETTINGS_LoadK5RXSettings();
    SETTINGS_LoadK5RXVfoIndices();
    if (!gEeprom.VFO_OPEN) {
        gEeprom.ScreenChannel[0] = gEeprom.MrChannel[0];
        gEeprom.ScreenChannel[1] = gEeprom.MrChannel[1];
    }
    SETTINGS_LoadK5RXChannelAttributes();
#ifdef ENABLE_FMRADIO
    SETTINGS_LoadK5RXFM();
#endif
}
#endif

void SETTINGS_InitEEPROM(void)
{
#ifdef ENABLE_K5RX_CUSTOM_EEPROM
    SETTINGS_InitK5RXEEPROM();
#else
    uint8_t Data[16] = {0};
    // 0E70..0E77
    EEPROM_ReadBuffer(0x0E70, Data, 8);
    gEeprom.CHAN_1_CALL          = IS_MR_CHANNEL(Data[0]) ? Data[0] : MR_CHANNEL_FIRST;
    gEeprom.SQUELCH_LEVEL        = (Data[1] < 10) ? Data[1] : 1;
    gEeprom.TX_TIMEOUT_TIMER     = (Data[2] > 4 && Data[2] < 180) ? Data[2] : 11;
    #ifdef ENABLE_NOAA
        gEeprom.NOAA_AUTO_SCAN   = (Data[3] <  2) ? Data[3] : false;
    #endif
    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        gEeprom.KEY_LOCK = (Data[4] & 0x01) != 0;
        gEeprom.MENU_LOCK = (Data[4] & 0x02) != 0;
        gEeprom.SET_KEY = ((Data[4] >> 2) & 0x0F) > 4 ? 0 : (Data[4] >> 2) & 0x0F;
    #else
        gEeprom.KEY_LOCK             = (Data[4] <  2) ? Data[4] : false;
    #endif
    #ifdef ENABLE_VOX
        gEeprom.VOX_SWITCH       = (Data[5] <  2) ? Data[5] : false;
        gEeprom.VOX_LEVEL        = (Data[6] < 10) ? Data[6] : 1;
    #endif
    gEeprom.MIC_SENSITIVITY      = (Data[7] <  5) ? Data[7] : 4;

    // 0E78..0E7F
    EEPROM_ReadBuffer(0x0E78, Data, 8);
    gEeprom.BACKLIGHT_MAX         = (Data[0] & 0xF) <= 10 ? (Data[0] & 0xF) : 10;
    gEeprom.BACKLIGHT_MIN         = (Data[0] >> 4) < gEeprom.BACKLIGHT_MAX ? (Data[0] >> 4) : 0;
#ifdef ENABLE_BLMIN_TMP_OFF
    gEeprom.BACKLIGHT_MIN_STAT    = BLMIN_STAT_ON;
#endif
    gEeprom.CHANNEL_DISPLAY_MODE  = (Data[1] < 4) ? Data[1] : MDF_FREQUENCY;    // 4 instead of 3 - extra display mode
    gEeprom.CROSS_BAND_RX_TX      = (Data[2] < 3) ? Data[2] : CROSS_BAND_OFF;
    gEeprom.BATTERY_SAVE          = (Data[3] < 6) ? Data[3] : 4;
    gEeprom.DUAL_WATCH            = (Data[4] < 3) ? Data[4] : DUAL_WATCH_CHAN_A;
    gEeprom.BACKLIGHT_TIME        = (Data[5] < 62) ? Data[5] : 12;
    #ifdef ENABLE_FEAT_F4HWN_NARROWER
        gEeprom.TAIL_TONE_ELIMINATION = Data[6] & 0x01;
        gSetting_set_nfm = (Data[6] >> 1) & 0x01;
    #else
        gEeprom.TAIL_TONE_ELIMINATION = (Data[6] < 2) ? Data[6] : false;
    #endif

    #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
        gEeprom.VFO_OPEN = Data[7] & 0x01;
        gEeprom.CURRENT_STATE = (Data[7] >> 1) & 0x07;
        gEeprom.CURRENT_LIST = (Data[7] >> 4) & 0x07;
    #else
        gEeprom.VFO_OPEN              = (Data[7] < 2) ? Data[7] : true;
    #endif

    // 0E80..0E87
    EEPROM_ReadBuffer(0x0E80, Data, 8);
    gEeprom.ScreenChannel[0]   = IS_VALID_CHANNEL(Data[0]) ? Data[0] : (FREQ_CHANNEL_FIRST + BAND6_400MHz);
    gEeprom.ScreenChannel[1]   = IS_VALID_CHANNEL(Data[3]) ? Data[3] : (FREQ_CHANNEL_FIRST + BAND6_400MHz);
    gEeprom.MrChannel[0]       = IS_MR_CHANNEL(Data[1])    ? Data[1] : MR_CHANNEL_FIRST;
    gEeprom.MrChannel[1]       = IS_MR_CHANNEL(Data[4])    ? Data[4] : MR_CHANNEL_FIRST;
    gEeprom.FreqChannel[0]     = IS_FREQ_CHANNEL(Data[2])  ? Data[2] : (FREQ_CHANNEL_FIRST + BAND6_400MHz);
    gEeprom.FreqChannel[1]     = IS_FREQ_CHANNEL(Data[5])  ? Data[5] : (FREQ_CHANNEL_FIRST + BAND6_400MHz);
#ifdef ENABLE_NOAA
    gEeprom.NoaaChannel[0] = IS_NOAA_CHANNEL(Data[6])  ? Data[6] : NOAA_CHANNEL_FIRST;
    gEeprom.NoaaChannel[1] = IS_NOAA_CHANNEL(Data[7])  ? Data[7] : NOAA_CHANNEL_FIRST;
#endif

#ifdef ENABLE_FMRADIO
    {   // 0E88..0E8F
        struct
        {
            uint16_t selFreq;
            uint8_t  selChn;
            uint8_t  isMrMode:1;
            uint8_t  band:2;
            //uint8_t  space:2;
        } __attribute__((packed)) fmCfg;
        EEPROM_ReadBuffer(0x0E88, &fmCfg, 4);

        gEeprom.FM_Band = fmCfg.band;
        //gEeprom.FM_Space = fmCfg.space;
        gEeprom.FM_SelectedFrequency = 
            (fmCfg.selFreq >= BK1080_GetFreqLoLimit(gEeprom.FM_Band) && fmCfg.selFreq <= BK1080_GetFreqHiLimit(gEeprom.FM_Band)) ? 
                fmCfg.selFreq : BK1080_GetFreqLoLimit(gEeprom.FM_Band);
            
        gEeprom.FM_SelectedChannel = fmCfg.selChn;
        gEeprom.FM_IsMrMode        = fmCfg.isMrMode;
    }

    // 0E40..0E67
    EEPROM_ReadBuffer(0x0E40, gFM_Channels, sizeof(gFM_Channels));
    FM_ConfigureChannelState();
#endif

    // 0E90..0E97
    EEPROM_ReadBuffer(0x0E90, Data, 8);
    gEeprom.BEEP_CONTROL                 = Data[0] & 1;
    gEeprom.KEY_M_LONG_PRESS_ACTION      = ((Data[0] >> 1) < ACTION_OPT_LEN) ? (Data[0] >> 1) : ACTION_OPT_NONE;
    gEeprom.KEY_1_SHORT_PRESS_ACTION     = (Data[1] < ACTION_OPT_LEN) ? Data[1] : ACTION_OPT_MONITOR;
    gEeprom.KEY_1_LONG_PRESS_ACTION      = (Data[2] < ACTION_OPT_LEN) ? Data[2] : ACTION_OPT_NONE;
    gEeprom.KEY_2_SHORT_PRESS_ACTION     = (Data[3] < ACTION_OPT_LEN) ? Data[3] : ACTION_OPT_SCAN;
    gEeprom.KEY_2_LONG_PRESS_ACTION      = (Data[4] < ACTION_OPT_LEN) ? Data[4] : ACTION_OPT_NONE;
    gEeprom.SCAN_RESUME_MODE             = (Data[5] < 105)            ? Data[5] : 14;
    gEeprom.AUTO_KEYPAD_LOCK             = (Data[6] < 41)             ? Data[6] : 0;
#ifdef ENABLE_FEAT_F4HWN
    gEeprom.POWER_ON_DISPLAY_MODE        = (Data[7] < 6)              ? Data[7] : POWER_ON_DISPLAY_MODE_VOLTAGE;
#else
    gEeprom.POWER_ON_DISPLAY_MODE        = (Data[7] < 4)              ? Data[7] : POWER_ON_DISPLAY_MODE_VOLTAGE;
#endif

    // 0E98..0E9F
    #ifdef ENABLE_PWRON_PASSWORD
        EEPROM_ReadBuffer(0x0E98, Data, 8);
        memcpy(&gEeprom.POWER_ON_PASSWORD, Data, 4);
    #endif

    // 0EA0..0EA7
    EEPROM_ReadBuffer(0x0EA0, Data, 8);
    #ifdef ENABLE_VOICE
    gEeprom.VOICE_PROMPT = (Data[0] < 3) ? Data[0] : VOICE_PROMPT_ENGLISH;
    #endif
    #ifdef ENABLE_RSSI_BAR
        if((Data[1] < 200 && Data[1] > 90) && (Data[2] < Data[1]-9 && Data[1] < 160  && Data[2] > 50)) {
            gEeprom.S0_LEVEL = Data[1];
            gEeprom.S9_LEVEL = Data[2];
        }
        else {
            gEeprom.S0_LEVEL = 130;
            gEeprom.S9_LEVEL = 76;
        }
    #endif

    // 0EA8..0EAF
    EEPROM_ReadBuffer(0x0EA8, Data, 8);
    #ifdef ENABLE_ALARM
        gEeprom.ALARM_MODE                 = (Data[0] <  2) ? Data[0] : true;
    #endif
    gEeprom.ROGER                          = (Data[1] <  3) ? Data[1] : ROGER_MODE_OFF;
    gEeprom.REPEATER_TAIL_TONE_ELIMINATION = (Data[2] < 11) ? Data[2] : 0;
    gEeprom.TX_VFO                         = (Data[3] <  2) ? Data[3] : 0;
    gEeprom.BATTERY_TYPE                   = (Data[4] < BATTERY_TYPE_UNKNOWN) ? Data[4] : BATTERY_TYPE_1600_MAH;

    // 0ED0..0ED7
    EEPROM_ReadBuffer(0x0ED0, Data, 8);
    gEeprom.DTMF_SIDE_TONE               = (Data[0] <   2) ? Data[0] : true;

#ifdef ENABLE_DTMF_CALLING
    gEeprom.DTMF_SEPARATE_CODE           = DTMF_ValidateCodes((char *)(Data + 1), 1) ? Data[1] : '*';
    gEeprom.DTMF_GROUP_CALL_CODE         = DTMF_ValidateCodes((char *)(Data + 2), 1) ? Data[2] : '#';
    gEeprom.DTMF_DECODE_RESPONSE         = (Data[3] <   4) ? Data[3] : 0;
    gEeprom.DTMF_auto_reset_time         = (Data[4] <  61) ? Data[4] : (Data[4] >= 5) ? Data[4] : 10;
#endif
    gEeprom.DTMF_PRELOAD_TIME            = (Data[5] < 101) ? Data[5] * 10 : 300;
    gEeprom.DTMF_FIRST_CODE_PERSIST_TIME = (Data[6] < 101) ? Data[6] * 10 : 100;
    gEeprom.DTMF_HASH_CODE_PERSIST_TIME  = (Data[7] < 101) ? Data[7] * 10 : 100;

    // 0ED8..0EDF
    EEPROM_ReadBuffer(0x0ED8, Data, 8);
    gEeprom.DTMF_CODE_PERSIST_TIME  = (Data[0] < 101) ? Data[0] * 10 : 100;
    gEeprom.DTMF_CODE_INTERVAL_TIME = (Data[1] < 101) ? Data[1] * 10 : 100;
#ifdef ENABLE_DTMF_CALLING
    gEeprom.PERMIT_REMOTE_KILL      = (Data[2] <   2) ? Data[2] : true;

    // 0EE0..0EE7

    EEPROM_ReadBuffer(0x0EE0, Data, sizeof(gEeprom.ANI_DTMF_ID));
    if (DTMF_ValidateCodes((char *)Data, sizeof(gEeprom.ANI_DTMF_ID))) {
        memcpy(gEeprom.ANI_DTMF_ID, Data, sizeof(gEeprom.ANI_DTMF_ID));
    } else {
        strcpy(gEeprom.ANI_DTMF_ID, "123");
    }


    // 0EE8..0EEF
    EEPROM_ReadBuffer(0x0EE8, Data, sizeof(gEeprom.KILL_CODE));
    if (DTMF_ValidateCodes((char *)Data, sizeof(gEeprom.KILL_CODE))) {
        memcpy(gEeprom.KILL_CODE, Data, sizeof(gEeprom.KILL_CODE));
    } else {
        strcpy(gEeprom.KILL_CODE, "ABCD9");
    }

    // 0EF0..0EF7
    EEPROM_ReadBuffer(0x0EF0, Data, sizeof(gEeprom.REVIVE_CODE));
    if (DTMF_ValidateCodes((char *)Data, sizeof(gEeprom.REVIVE_CODE))) {
        memcpy(gEeprom.REVIVE_CODE, Data, sizeof(gEeprom.REVIVE_CODE));
    } else {
        strcpy(gEeprom.REVIVE_CODE, "9DCBA");
    }
#endif

    // 0EF8..0F07
    EEPROM_ReadBuffer(0x0EF8, Data, sizeof(gEeprom.DTMF_UP_CODE));
    if (DTMF_ValidateCodes((char *)Data, sizeof(gEeprom.DTMF_UP_CODE))) {
        memcpy(gEeprom.DTMF_UP_CODE, Data, sizeof(gEeprom.DTMF_UP_CODE));
    } else {
        strcpy(gEeprom.DTMF_UP_CODE, "12345");
    }

    // 0F08..0F17
    EEPROM_ReadBuffer(0x0F08, Data, sizeof(gEeprom.DTMF_DOWN_CODE));
    if (DTMF_ValidateCodes((char *)Data, sizeof(gEeprom.DTMF_DOWN_CODE))) {
        memcpy(gEeprom.DTMF_DOWN_CODE, Data, sizeof(gEeprom.DTMF_DOWN_CODE));
    } else {
        strcpy(gEeprom.DTMF_DOWN_CODE, "54321");
    }

    // 0F18..0F1F
    EEPROM_ReadBuffer(0x0F18, Data, 8);
    gEeprom.SCAN_LIST_DEFAULT = (Data[0] < 6) ? Data[0] : 0;  // we now have 'all' channel scan option

    // Fake data
    /*
    gEeprom.SCAN_LIST_ENABLED[0] = 0;
    gEeprom.SCAN_LIST_ENABLED[1] = 0;
    gEeprom.SCAN_LIST_ENABLED[2] = 0;

    gEeprom.SCANLIST_PRIORITY_CH1[0] =  0;
    gEeprom.SCANLIST_PRIORITY_CH2[0] =  2;

    gEeprom.SCANLIST_PRIORITY_CH1[1] =  14;
    gEeprom.SCANLIST_PRIORITY_CH2[1] =  15;

    gEeprom.SCANLIST_PRIORITY_CH1[2] =  40;
    gEeprom.SCANLIST_PRIORITY_CH2[2] =  41;
    */

    // Fix me probably after Chirp update...
    for (unsigned int i = 0; i < 3; i++)
    {
        gEeprom.SCAN_LIST_ENABLED[i] = (Data[1] >> i) & 1;
    }

    for (unsigned int i = 0; i < 3; i++)
    {
        const unsigned int j = 1 + (i * 2);
        gEeprom.SCANLIST_PRIORITY_CH1[i] =  Data[j + 1];
        gEeprom.SCANLIST_PRIORITY_CH2[i] =  Data[j + 2];
    }

    // 0F40..0F47
    EEPROM_ReadBuffer(0x0F40, Data, 8);
    gSetting_F_LOCK            = (Data[0] < F_LOCK_LEN) ? Data[0] : F_LOCK_DEF;
#ifndef ENABLE_FEAT_F4HWN
    gSetting_350TX             = (Data[1] < 2) ? Data[1] : false;  // was true
#endif
#ifdef ENABLE_DTMF_CALLING
    gSetting_KILLED            = (Data[2] < 2) ? Data[2] : false;
#endif
#ifndef ENABLE_FEAT_F4HWN
    gSetting_200TX             = (Data[3] < 2) ? Data[3] : false;
    gSetting_500TX             = (Data[4] < 2) ? Data[4] : false;
#endif
    gSetting_350EN             = (Data[5] < 2) ? Data[5] : true;
#ifdef ENABLE_FEAT_F4HWN
    gSetting_ScrambleEnable    = false;
#else
    gSetting_ScrambleEnable    = (Data[6] < 2) ? Data[6] : true;
#endif

    //gSetting_TX_EN             = (Data[7] & (1u << 0)) ? true : false;
    gSetting_live_DTMF_decoder = !!(Data[7] & (1u << 1));
    gSetting_battery_text      = (((Data[7] >> 2) & 3u) <= 2) ? (Data[7] >> 2) & 3 : 2;
    #ifdef ENABLE_AUDIO_BAR
        gSetting_mic_bar       = !!(Data[7] & (1u << 4));
    #endif
    #ifndef ENABLE_FEAT_F4HWN
        #ifdef ENABLE_AM_FIX
            gSetting_AM_fix        = !!(Data[7] & (1u << 5));
        #endif
    #endif
    gSetting_backlight_on_tx_rx = (Data[7] >> 6) & 3u;

    if (!gEeprom.VFO_OPEN)
    {
        gEeprom.ScreenChannel[0] = gEeprom.MrChannel[0];
        gEeprom.ScreenChannel[1] = gEeprom.MrChannel[1];
    }

    // 0D60..0E27
    EEPROM_ReadBuffer(0x0D60, gMR_ChannelAttributes, sizeof(gMR_ChannelAttributes));
    for(uint16_t i = 0; i < sizeof(gMR_ChannelAttributes); i++) {
        ChannelAttributes_t *att = &gMR_ChannelAttributes[i];
        if(att->__val == 0xff){
            att->__val = 0;
            att->band = 0x7;
        }
        gMR_ChannelExclude[i] = false;
    }

        // 0F30..0F3F
        EEPROM_ReadBuffer(0x0F30, gCustomAesKey, sizeof(gCustomAesKey));
        bHasCustomAesKey = false;
        #ifndef ENABLE_FEAT_F4HWN
            for (unsigned int i = 0; i < ARRAY_SIZE(gCustomAesKey); i++)
            {
                if (gCustomAesKey[i] != 0xFFFFFFFFu)
                {
                    bHasCustomAesKey = true;
                    return;
                }
            }
        #endif

    #ifdef ENABLE_FEAT_F4HWN
        // 1FF0..0x1FF7
        EEPROM_ReadBuffer(0x1FF0, Data, 8);
        gSetting_set_pwr = (((Data[7] & 0xF0) >> 4) < 7) ? ((Data[7] & 0xF0) >> 4) : 0;
        gSetting_set_ptt = (((Data[7] & 0x0F)) < 2) ? ((Data[7] & 0x0F)) : 0;

        gSetting_set_tot = (((Data[6] & 0xF0) >> 4) < 4) ? ((Data[6] & 0xF0) >> 4) : 0;
        gSetting_set_eot = (((Data[6] & 0x0F)) < 4) ? ((Data[6] & 0x0F)) : 0;

        /*
        int tmp = ((Data[5] & 0xF0) >> 4);

        gSetting_set_inv = (((tmp >> 0) & 0x01) < 2) ? ((tmp >> 0) & 0x01): 0;
        gSetting_set_lck = (((tmp >> 1) & 0x01) < 2) ? ((tmp >> 1) & 0x01): 0;
        gSetting_set_met = (((tmp >> 2) & 0x01) < 2) ? ((tmp >> 2) & 0x01): 0;
        gSetting_set_gui = (((tmp >> 3) & 0x01) < 2) ? ((tmp >> 3) & 0x01): 0;
        gSetting_set_ctr = (((Data[5] & 0x0F)) > 00 && ((Data[5] & 0x0F)) < 16) ? ((Data[5] & 0x0F)) : 10;

        gSetting_set_tmr = ((Data[4] & 1) < 2) ? (Data[4] & 1): 0;
        */

        int tmp = (Data[5] & 0xF0) >> 4;

#ifdef ENABLE_FEAT_F4HWN_INV
        gSetting_set_inv = (tmp >> 0) & 0x01;
#else
        gSetting_set_inv = 0;
#endif
        gSetting_set_lck = (tmp >> 1) & 0x01;
        gSetting_set_met = (tmp >> 2) & 0x01;
        gSetting_set_gui = (tmp >> 3) & 0x01;

#ifdef ENABLE_FEAT_F4HWN_CTR
        int ctr_value = Data[5] & 0x0F;
        gSetting_set_ctr = (ctr_value > 0 && ctr_value < 16) ? ctr_value : 10;
#else
        gSetting_set_ctr = 10;
#endif

        gSetting_set_tmr = Data[4] & 0x01;
#ifdef ENABLE_FEAT_F4HWN_SLEEP
        gSetting_set_off = (Data[4] >> 1) > 120 ? 60 : (Data[4] >> 1); 
#endif

        // Warning
        // Be aware, Data[3] is use by Spectrum
        // Warning

        // And set special session settings for actions
        gSetting_set_ptt_session = gSetting_set_ptt;
        gEeprom.KEY_LOCK_PTT = gSetting_set_lck;
    #endif
#endif
}

void SETTINGS_LoadCalibration(void)
{
//  uint8_t Mic;

    EEPROM_ReadBuffer(0x1EC0, gEEPROM_RSSI_CALIB[3], 8);
    memcpy(gEEPROM_RSSI_CALIB[4], gEEPROM_RSSI_CALIB[3], 8);
    memcpy(gEEPROM_RSSI_CALIB[5], gEEPROM_RSSI_CALIB[3], 8);
    memcpy(gEEPROM_RSSI_CALIB[6], gEEPROM_RSSI_CALIB[3], 8);

    EEPROM_ReadBuffer(0x1EC8, gEEPROM_RSSI_CALIB[0], 8);
    memcpy(gEEPROM_RSSI_CALIB[1], gEEPROM_RSSI_CALIB[0], 8);
    memcpy(gEEPROM_RSSI_CALIB[2], gEEPROM_RSSI_CALIB[0], 8);

    EEPROM_ReadBuffer(0x1F40, gBatteryCalibration, 12);
    if (gBatteryCalibration[0] >= 5000)
    {
        gBatteryCalibration[0] = 1900;
        gBatteryCalibration[1] = 2000;
    }
    gBatteryCalibration[5] = 2300;

    #ifdef ENABLE_VOX
        EEPROM_ReadBuffer(0x1F50 + (gEeprom.VOX_LEVEL * 2), &gEeprom.VOX1_THRESHOLD, 2);
        EEPROM_ReadBuffer(0x1F68 + (gEeprom.VOX_LEVEL * 2), &gEeprom.VOX0_THRESHOLD, 2);
    #endif

    //EEPROM_ReadBuffer(0x1F80 + gEeprom.MIC_SENSITIVITY, &Mic, 1);
    //gEeprom.MIC_SENSITIVITY_TUNING = (Mic < 32) ? Mic : 15;
    gEeprom.MIC_SENSITIVITY_TUNING = gMicGain_dB2[gEeprom.MIC_SENSITIVITY];

    {
        struct
        {
            int16_t  BK4819_XtalFreqLow;
            uint16_t EEPROM_1F8A;
            uint16_t EEPROM_1F8C;
            uint8_t  VOLUME_GAIN;
            uint8_t  DAC_GAIN;
        } __attribute__((packed)) Misc;

        // radio 1 .. 04 00 46 00 50 00 2C 0E
        // radio 2 .. 05 00 46 00 50 00 2C 0E
        EEPROM_ReadBuffer(0x1F88, &Misc, 8);

        gEeprom.BK4819_XTAL_FREQ_LOW = (Misc.BK4819_XtalFreqLow >= -1000 && Misc.BK4819_XtalFreqLow <= 1000) ? Misc.BK4819_XtalFreqLow : 0;
        gEEPROM_1F8A                 = Misc.EEPROM_1F8A & 0x01FF;
        gEEPROM_1F8C                 = Misc.EEPROM_1F8C & 0x01FF;
        gEeprom.VOLUME_GAIN          = (Misc.VOLUME_GAIN < 64) ? Misc.VOLUME_GAIN : 58;
        gEeprom.DAC_GAIN             = (Misc.DAC_GAIN    < 16) ? Misc.DAC_GAIN    : 8;

        #ifdef ENABLE_FEAT_F4HWN
            gEeprom.VOLUME_GAIN_BACKUP   = gEeprom.VOLUME_GAIN;
        #endif

        BK4819_WriteRegister(BK4819_REG_3B, 22656 + gEeprom.BK4819_XTAL_FREQ_LOW);
//      BK4819_WriteRegister(BK4819_REG_3C, gEeprom.BK4819_XTAL_FREQ_HIGH);
    }
}

uint32_t SETTINGS_FetchChannelFrequency(const int channel)
{
#ifdef ENABLE_K5RX_CUSTOM_EEPROM
    if (channel < MR_CHANNEL_FIRST || channel > MR_CHANNEL_LAST)
        return 0;
    const SETTINGS_K5RX_ChannelRecord_t *record = SETTINGS_GetK5RXChannelRecord((channel_t)channel);
    return record != NULL ? record->frequency : 0;
#else
    struct
    {
        uint32_t frequency;
        uint32_t offset;
    } __attribute__((packed)) info;

    EEPROM_ReadBuffer(channel * 16, &info, sizeof(info));

    return info.frequency;
#endif
}

void SETTINGS_FetchChannelName(char *s, const int channel)
{
    if (s == NULL)
        return;

    s[0] = 0;

    if (channel < 0)
        return;

    if (!RADIO_CheckValidChannel(channel, false, 0))
        return;

#ifdef ENABLE_K5RX_CUSTOM_EEPROM
    if (!gK5RXHeaderValid)
        return;
    EEPROM_ReadBuffer(SETTINGS_K5RXChannelNameOffset((channel_t)channel), s, EEPROM_K5RX_CHANNEL_NAME_LENGTH);
#else
    EEPROM_ReadBuffer(0x0F50 + (channel * 16), s, 10);
#endif

    int i;
    for (i = 0; i < 10; i++)
        if (s[i] < 32 || s[i] > 127)
            break;                // invalid char

    s[i--] = 0;                   // null term

    while (i >= 0 && s[i] == 32)  // trim trailing spaces
        s[i--] = 0;               // null term
}

void SETTINGS_FactoryReset(bool bIsAll)
{
#ifdef ENABLE_K5RX_CUSTOM_EEPROM
    SETTINGS_ResetK5RXMutable(bIsAll);
    return;
#endif
    uint16_t i;
    uint8_t  Template[8];

    memset(Template, 0xFF, sizeof(Template));

    //for (i = 0x0C80; i < 0x1E00; i += 8)
    for (i = 0x0000; i < 0x1E00; i += 8)
    {
        if (
            !(i >= 0x0EE0 && i < 0x0F18) &&         // ANI ID + DTMF codes
            !(i >= 0x0F30 && i < 0x0F50) &&         // AES KEY + F LOCK + Scramble Enable
            !(i >= 0x1C00 && i < 0x1E00) &&         // DTMF contacts
            !(i >= 0x0EB0 && i < 0x0ED0) &&         // Welcome strings
            !(i >= 0x0EA0 && i < 0x0EA8) &&         // Voice Prompt
            (bIsAll ||
            (
                !(i >= 0x0D60 && i < 0x0E28) &&     // MR Channel Attributes
                !(i >= 0x0F18 && i < 0x0F30) &&     // Scan List
                !(i >= 0x0F50 && i < 0x1C00) &&     // MR Channel Names
                !(i >= 0x0E40 && i < 0x0E70) &&     // FM Channels
                !(i >= 0x0E88 && i < 0x0E90)        // FM settings
                ))
            )
        {
            EEPROM_WriteBuffer(i, Template);
        }
    }

    if (bIsAll)
    {
        RADIO_InitInfo(gRxVfo, FREQ_CHANNEL_FIRST + BAND6_400MHz, 43350000);

        #ifdef ENABLE_FEAT_F4HWN_RESET_CHANNEL
            // set the first few memory channels
            for (i = 0; i < ARRAY_SIZE(gDefaultFrequencyTable); i++)
            {
                const uint32_t Frequency   = gDefaultFrequencyTable[i];
                gRxVfo->freq_config_RX.Frequency = Frequency;
                gRxVfo->freq_config_TX.Frequency = Frequency;
                gRxVfo->Band               = FREQUENCY_GetBand(Frequency);
                SETTINGS_SaveChannel(MR_CHANNEL_FIRST + i, 0, gRxVfo, 2);
            }
        #endif

        #ifdef ENABLE_FEAT_F4HWN
            EEPROM_WriteBuffer(0x1FF0, Template);
        #endif
    }
}

#ifdef ENABLE_FMRADIO
void SETTINGS_SaveFM(void)
{
#ifdef ENABLE_K5RX_CUSTOM_EEPROM
    if (!gK5RXHeaderValid)
        return;
    const uint16_t channelBase = EEPROM_K5RX_FM_CHANNEL_BASE;
    SETTINGS_WriteK5RXBytes(EEPROM_K5RX_FM_CONFIG_BASE, gEeprom.FM_ConfigRaw, sizeof(gEeprom.FM_ConfigRaw));
#else
    union {
        struct {
            uint16_t selFreq;
            uint8_t  selChn;
            uint8_t  isMrMode:1;
            uint8_t  band:2;
            //uint8_t  space:2;
        };
        uint8_t __raw[8];
    } __attribute__((packed)) fmCfg;
    const uint16_t configBase = EEPROM_FM_SETTINGS_BASE;
    const uint16_t channelBase = EEPROM_FM_CHANNEL_BASE;

    memset(fmCfg.__raw, 0xFF, sizeof(fmCfg.__raw));
    fmCfg.selChn   = gEeprom.FM_SelectedChannel;
    fmCfg.selFreq  = gEeprom.FM_SelectedFrequency;
    fmCfg.isMrMode = gEeprom.FM_IsMrMode;
    fmCfg.band     = gEeprom.FM_Band;
    //fmCfg.space    = gEeprom.FM_Space;
    EEPROM_WriteBuffer(configBase, fmCfg.__raw);
#endif

    for (unsigned i = 0; i < 5; i++)
        EEPROM_WriteBuffer(channelBase + (i * 8), &gFM_Channels[i * 4]);
}
#endif

void SETTINGS_SaveVfoIndices(void)
{
#ifdef ENABLE_K5RX_CUSTOM_EEPROM
    SETTINGS_SaveK5RXVfoIndices();
    return;
#endif
    uint8_t State[8];

    #ifndef ENABLE_NOAA
        EEPROM_ReadBuffer(0x0E80, State, sizeof(State));
    #endif

    State[0] = gEeprom.ScreenChannel[0];
    State[1] = gEeprom.MrChannel[0];
    State[2] = gEeprom.FreqChannel[0];
    State[3] = gEeprom.ScreenChannel[1];
    State[4] = gEeprom.MrChannel[1];
    State[5] = gEeprom.FreqChannel[1];
    #ifdef ENABLE_NOAA
        State[6] = gEeprom.NoaaChannel[0];
        State[7] = gEeprom.NoaaChannel[1];
    #endif

    EEPROM_WriteBuffer(0x0E80, State);
}

void SETTINGS_SaveSettings(void)
{
#ifdef ENABLE_K5RX_CUSTOM_EEPROM
    SETTINGS_SaveK5RXSettings();
    return;
#endif
    uint8_t  State[8];
    uint8_t tmp = 0;

    #ifdef ENABLE_PWRON_PASSWORD
        uint32_t Password[2];
    #endif

    State[0] = gEeprom.CHAN_1_CALL;
    State[1] = gEeprom.SQUELCH_LEVEL;
    State[2] = gEeprom.TX_TIMEOUT_TIMER;
    #ifdef ENABLE_NOAA
        State[3] = gEeprom.NOAA_AUTO_SCAN;
    #else
        State[3] = false;
    #endif

    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        State[4] = (gEeprom.KEY_LOCK ? 0x01 : 0) | (gEeprom.MENU_LOCK ? 0x02 :0) | ((gEeprom.SET_KEY & 0x0F) << 2);
    #else
        State[4] = gEeprom.KEY_LOCK;
    #endif

    #ifdef ENABLE_VOX
        State[5] = gEeprom.VOX_SWITCH;
        State[6] = gEeprom.VOX_LEVEL;
    #else
        State[5] = false;
        State[6] = 0;
    #endif
    State[7] = gEeprom.MIC_SENSITIVITY;
    EEPROM_WriteBuffer(0x0E70, State);

    State[0] = (gEeprom.BACKLIGHT_MIN << 4) + gEeprom.BACKLIGHT_MAX;
    State[1] = gEeprom.CHANNEL_DISPLAY_MODE;
    State[2] = gEeprom.CROSS_BAND_RX_TX;
    State[3] = gEeprom.BATTERY_SAVE;
    State[4] = gEeprom.DUAL_WATCH;

    #ifdef ENABLE_FEAT_F4HWN
        if(!gSaveRxMode)
        {
            State[2] = gCB;
            State[4] = gDW;
        }
        if(gBackLight)
        {
            State[5] = gBacklightTimeOriginal;
        }
        else
        {
            State[5] = gEeprom.BACKLIGHT_TIME;
        }
    #else
        State[5] = gEeprom.BACKLIGHT_TIME;
    #endif

    #ifdef ENABLE_FEAT_F4HWN_NARROWER
        State[6] = (gEeprom.TAIL_TONE_ELIMINATION & 0x01) | ((gSetting_set_nfm & 0x03) << 1);
    #else
        State[6] = gEeprom.TAIL_TONE_ELIMINATION;
    #endif

    #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
        State[7] = (gEeprom.VFO_OPEN & 0x01) | ((gEeprom.CURRENT_STATE & 0x07) << 1) | ((gEeprom.SCAN_LIST_DEFAULT & 0x07) << 4);
    #else
        State[7] = gEeprom.VFO_OPEN;
    #endif
    EEPROM_WriteBuffer(0x0E78, State);

    State[0] = gEeprom.BEEP_CONTROL;
    State[0] |= gEeprom.KEY_M_LONG_PRESS_ACTION << 1;
    State[1] = gEeprom.KEY_1_SHORT_PRESS_ACTION;
    State[2] = gEeprom.KEY_1_LONG_PRESS_ACTION;
    State[3] = gEeprom.KEY_2_SHORT_PRESS_ACTION;
    State[4] = gEeprom.KEY_2_LONG_PRESS_ACTION;
    State[5] = gEeprom.SCAN_RESUME_MODE;
    State[6] = gEeprom.AUTO_KEYPAD_LOCK;
    State[7] = gEeprom.POWER_ON_DISPLAY_MODE;
    EEPROM_WriteBuffer(0x0E90, State);

    #ifdef ENABLE_PWRON_PASSWORD
        memset(Password, 0xFF, sizeof(Password));
        Password[0] = gEeprom.POWER_ON_PASSWORD;
        EEPROM_WriteBuffer(0x0E98, Password);
    #endif

    memset(State, 0xFF, sizeof(State));
#ifdef ENABLE_VOICE
    State[0] = gEeprom.VOICE_PROMPT;
#endif
#ifdef ENABLE_RSSI_BAR
    State[1] = gEeprom.S0_LEVEL;
    State[2] = gEeprom.S9_LEVEL;
#endif
    EEPROM_WriteBuffer(0x0EA0, State);


    #if defined(ENABLE_ALARM) || defined(ENABLE_TX1750)
        State[0] = gEeprom.ALARM_MODE;
    #else
        State[0] = false;
    #endif
    State[1] = gEeprom.ROGER;
    State[2] = gEeprom.REPEATER_TAIL_TONE_ELIMINATION;
    State[3] = gEeprom.TX_VFO;
    State[4] = gEeprom.BATTERY_TYPE;
    EEPROM_WriteBuffer(0x0EA8, State);

    State[0] = gEeprom.DTMF_SIDE_TONE;
#ifdef ENABLE_DTMF_CALLING
    State[1] = gEeprom.DTMF_SEPARATE_CODE;
    State[2] = gEeprom.DTMF_GROUP_CALL_CODE;
    State[3] = gEeprom.DTMF_DECODE_RESPONSE;
    State[4] = gEeprom.DTMF_auto_reset_time;
#endif
    State[5] = gEeprom.DTMF_PRELOAD_TIME / 10U;
    State[6] = gEeprom.DTMF_FIRST_CODE_PERSIST_TIME / 10U;
    State[7] = gEeprom.DTMF_HASH_CODE_PERSIST_TIME / 10U;
    EEPROM_WriteBuffer(0x0ED0, State);

    memset(State, 0xFF, sizeof(State));
    State[0] = gEeprom.DTMF_CODE_PERSIST_TIME / 10U;
    State[1] = gEeprom.DTMF_CODE_INTERVAL_TIME / 10U;
#ifdef ENABLE_DTMF_CALLING
    State[2] = gEeprom.PERMIT_REMOTE_KILL;
#endif
    EEPROM_WriteBuffer(0x0ED8, State);

    State[0] = gEeprom.SCAN_LIST_DEFAULT;

    tmp = 0;

    if (gEeprom.SCAN_LIST_ENABLED[0] == 1)
        tmp = tmp | (1 << 0);
    if (gEeprom.SCAN_LIST_ENABLED[1] == 1)
        tmp = tmp | (1 << 1);
    if (gEeprom.SCAN_LIST_ENABLED[2] == 1)
        tmp = tmp | (1 << 2);

    State[1] = tmp;
    State[2] = gEeprom.SCANLIST_PRIORITY_CH1[0];
    State[3] = gEeprom.SCANLIST_PRIORITY_CH2[0];
    State[4] = gEeprom.SCANLIST_PRIORITY_CH1[1];
    State[5] = gEeprom.SCANLIST_PRIORITY_CH2[1];
    State[6] = gEeprom.SCANLIST_PRIORITY_CH1[2];
    State[7] = gEeprom.SCANLIST_PRIORITY_CH2[2];
    EEPROM_WriteBuffer(0x0F18, State);

    memset(State, 0xFF, sizeof(State));
    State[0]  = gSetting_F_LOCK;
#ifndef ENABLE_FEAT_F4HWN
    State[1]  = gSetting_350TX;
#endif
#ifdef ENABLE_DTMF_CALLING
    State[2]  = gSetting_KILLED;
#endif
#ifndef ENABLE_FEAT_F4HWN
    State[3]  = gSetting_200TX;
    State[4]  = gSetting_500TX;
#endif
    State[5]  = gSetting_350EN;
#ifdef ENABLE_FEAT_F4HWN
    State[6]  = false;
#else
    State[6]  = gSetting_ScrambleEnable;
#endif

    //if (!gSetting_TX_EN)             State[7] &= ~(1u << 0);
    if (!gSetting_live_DTMF_decoder) State[7] &= ~(1u << 1);
    State[7] = (State[7] & ~(3u << 2)) | ((gSetting_battery_text & 3u) << 2);
    #ifdef ENABLE_AUDIO_BAR
        if (!gSetting_mic_bar)           State[7] &= ~(1u << 4);
    #endif
    #ifndef ENABLE_FEAT_F4HWN
        #ifdef ENABLE_AM_FIX
            if (!gSetting_AM_fix)            State[7] &= ~(1u << 5);
        #endif
    #endif
    State[7] = (State[7] & ~(3u << 6)) | ((gSetting_backlight_on_tx_rx & 3u) << 6);

    EEPROM_WriteBuffer(0x0F40, State);

#ifdef ENABLE_FEAT_F4HWN
    EEPROM_ReadBuffer(0x1FF0, State, sizeof(State));

    //memset(State, 0xFF, sizeof(State));

    /*
    tmp = 0;

    if(gSetting_set_tmr == 1)
        tmp = tmp | (1 << 0);

    State[4] = tmp;

    tmp = 0;

    if(gSetting_set_inv == 1)
        tmp = tmp | (1 << 0);
    if (gSetting_set_lck == 1)
        tmp = tmp | (1 << 1);
    if (gSetting_set_met == 1)
        tmp = tmp | (1 << 2);
    if (gSetting_set_gui == 1)
        tmp = tmp | (1 << 3);
    */

#ifdef ENABLE_FEAT_F4HWN_SLEEP 
    State[4] = (gSetting_set_off << 1) | (gSetting_set_tmr & 0x01);
#else
    State[4] = gSetting_set_tmr ? (1 << 0) : 0;
#endif

    tmp =   (gSetting_set_inv << 0) |
            (gSetting_set_lck << 1) |
            (gSetting_set_met << 2) |
            (gSetting_set_gui << 3);

    State[5] = ((tmp << 4) | (gSetting_set_ctr & 0x0F));
    State[6] = ((gSetting_set_tot << 4) | (gSetting_set_eot & 0x0F));
    State[7] = ((gSetting_set_pwr << 4) | (gSetting_set_ptt & 0x0F));

    gEeprom.KEY_LOCK_PTT = gSetting_set_lck;

    EEPROM_WriteBuffer(0x1FF0, State);
#endif

#ifdef ENABLE_FEAT_F4HWN_VOL
    SETTINGS_WriteCurrentVol();
#endif
}

void SETTINGS_SaveChannel(channel_t Channel, uint8_t VFO, const VFO_Info_t *pVFO, uint8_t Mode)
{
#ifdef ENABLE_K5RX_CUSTOM_EEPROM
    if (!gK5RXHeaderValid)
        return;
    if (IS_MR_CHANNEL(Channel)) {
        if (Mode >= 2) {
            SETTINGS_SaveK5RXChannelRecord(Channel, pVFO);
#ifndef ENABLE_KEEP_MEM_NAME
            SETTINGS_SaveChannelName(Channel, "");
#else
            if (Mode >= 3 && pVFO != NULL)
                SETTINGS_SaveChannelName(Channel, pVFO->Name);
#endif
        }
        return;
    }
    if (IS_FREQ_CHANNEL(Channel))
        SETTINGS_SaveK5RXVfoRuntime(VFO, Channel - FREQ_CHANNEL_FIRST, pVFO);
    return;
#endif
#ifdef ENABLE_NOAA
    if (IS_NOAA_CHANNEL(Channel))
        return;
#endif

    uint16_t OffsetVFO = Channel * 16;

    if (IS_FREQ_CHANNEL(Channel)) { // it's a VFO, not a channel
        OffsetVFO  = (VFO == 0) ? 0x0C80 : 0x0C90;
        OffsetVFO += (Channel - FREQ_CHANNEL_FIRST) * 32;
    }

    if (Mode >= 2 || IS_FREQ_CHANNEL(Channel)) { // copy VFO to a channel
        union {
            uint8_t _8[8];
            uint32_t _32[2];
        } State;

        State._32[0] = pVFO->freq_config_RX.Frequency;
        State._32[1] = pVFO->TX_OFFSET_FREQUENCY;
        EEPROM_WriteBuffer(OffsetVFO + 0, State._32);

        State._8[0] =  pVFO->freq_config_RX.Code;
        State._8[1] =  pVFO->freq_config_TX.Code;
        State._8[2] = (pVFO->freq_config_TX.CodeType << 4) | pVFO->freq_config_RX.CodeType;
        State._8[3] = (pVFO->Modulation << 4) | pVFO->TX_OFFSET_FREQUENCY_DIRECTION;
        State._8[4] = 0
            | (pVFO->TX_LOCK << 6)
            | (pVFO->BUSY_CHANNEL_LOCK << 5)
            | (pVFO->OUTPUT_POWER      << 2)
            | (pVFO->CHANNEL_BANDWIDTH << 1)
            | (pVFO->FrequencyReverse  << 0);
        State._8[5] = ((pVFO->DTMF_PTT_ID_TX_MODE & 7u) << 1)
#ifdef ENABLE_DTMF_CALLING
            | ((pVFO->DTMF_DECODING_ENABLE & 1u) << 0)
#endif
        ;
        State._8[6] =  pVFO->STEP_SETTING;
#ifdef ENABLE_FEAT_F4HWN
        State._8[7] =  0;
#else
        State._8[7] =  pVFO->SCRAMBLING_TYPE;
#endif
        EEPROM_WriteBuffer(OffsetVFO + 8, State._8);

        SETTINGS_UpdateChannel(Channel, pVFO, true, true, true);

        if (IS_MR_CHANNEL(Channel)) {
#ifndef ENABLE_KEEP_MEM_NAME
            // clear/reset the channel name
            SETTINGS_SaveChannelName(Channel, "");
#else
            if (Mode >= 3) {
                SETTINGS_SaveChannelName(Channel, pVFO->Name);
            }
#endif
        }
    }

}

void SETTINGS_SaveBatteryCalibration(const uint16_t * batteryCalibration)
{
    uint16_t buf[4];
    EEPROM_WriteBuffer(0x1F40, batteryCalibration);
    EEPROM_ReadBuffer( 0x1F48, buf, sizeof(buf));
    buf[0] = batteryCalibration[4];
    buf[1] = batteryCalibration[5];
    EEPROM_WriteBuffer(0x1F48, buf);
}

void SETTINGS_SaveChannelName(channel_t channel, const char * name)
{
#ifdef ENABLE_K5RX_CUSTOM_EEPROM
    uint8_t k5rxName[EEPROM_K5RX_CHANNEL_NAME_LENGTH] = {0};

    if (!gK5RXHeaderValid || !IS_MR_CHANNEL(channel))
        return;
    if (name != NULL)
        memcpy(k5rxName, name, MIN(strlen(name), EEPROM_K5RX_CHANNEL_NAME_LENGTH));
    SETTINGS_WriteK5RXBytes(SETTINGS_K5RXChannelNameOffset(channel), k5rxName, sizeof(k5rxName));
    return;
#endif
    uint16_t offset = channel * 16;
    uint8_t buf[16] = {0};
    memcpy(buf, name, MIN(strlen(name), 10u));
    EEPROM_WriteBuffer(0x0F50 + offset, buf);
    EEPROM_WriteBuffer(0x0F58 + offset, buf + 8);
}

void SETTINGS_UpdateChannel(channel_t channel, const VFO_Info_t *pVFO, bool keep, bool check, bool save)
{
#ifdef ENABLE_K5RX_CUSTOM_EEPROM
    (void)check;
    if (!IS_MR_CHANNEL(channel))
        return;
    if (keep && pVFO != NULL) {
        gMR_ChannelAttributes[channel].band = pVFO->Band;
        gMR_ChannelAttributes[channel].compander = pVFO->Compander;
        gMR_ChannelAttributes[channel].scanlist1 = pVFO->SCANLIST1_PARTICIPATION;
        gMR_ChannelAttributes[channel].scanlist2 = pVFO->SCANLIST2_PARTICIPATION;
        gMR_ChannelAttributes[channel].scanlist3 = pVFO->SCANLIST3_PARTICIPATION;
        if (save)
            SETTINGS_SaveK5RXChannelRecord(channel, pVFO);
    } else if (save && gK5RXHeaderValid) {
        uint8_t data[EEPROM_K5RX_CHANNEL_RECORD_SIZE];
        memset(data, 0xFF, sizeof(data));
        EEPROM_WriteBuffer(SETTINGS_K5RXChannelRecordOffset(channel), data);
        memset(&gK5RXChannelCache[channel], 0, sizeof(gK5RXChannelCache[channel]));
        gMR_ChannelAttributes[channel].__val = 0xFF;
        SETTINGS_SaveChannelName(channel, "");
    }
    return;
#endif
#ifdef ENABLE_NOAA
    if (!IS_NOAA_CHANNEL(channel))
#endif
    {
        uint8_t  state[8];
        ChannelAttributes_t  att = {
            .band = 0x7,
            .compander = 0,
            .scanlist1 = 0,
            .scanlist2 = 0,
            .scanlist3 = 0,
            };        // default attributes

        uint16_t offset = 0x0D60 + (channel & ~7u);
        EEPROM_ReadBuffer(offset, state, sizeof(state));

        if (keep) {
            att.band = pVFO->Band;
            att.scanlist1 = pVFO->SCANLIST1_PARTICIPATION;
            att.scanlist2 = pVFO->SCANLIST2_PARTICIPATION;
            att.scanlist3 = pVFO->SCANLIST3_PARTICIPATION;
            att.compander = pVFO->Compander;
            if (check && state[channel & 7u] == att.__val)
                return; // no change in the attributes
        }

        state[channel & 7u] = att.__val;

#ifdef ENABLE_FEAT_F4HWN
        if(save)
        {
            EEPROM_WriteBuffer(offset, state);
        }
#else
        EEPROM_WriteBuffer(offset, state);
#endif

        gMR_ChannelAttributes[channel] = att;

        if (IS_MR_CHANNEL(channel)) {   // it's a memory channel
            if (!keep) {
                // clear/reset the channel name
                SETTINGS_SaveChannelName(channel, "");
            }
        }
    }
}

void SETTINGS_WriteBuildOptions(void)
{
    uint8_t State[8];

#ifdef ENABLE_K5RX_CUSTOM_EEPROM
    if (!gK5RXHeaderValid)
        return;
    memset(State, 0xFF, sizeof(State));
#elif defined(ENABLE_FEAT_F4HWN)
    EEPROM_ReadBuffer(0x1FF0, State, sizeof(State));
#else
    memset(State, 0xFF, sizeof(State));
#endif
    
State[0] = 0
#ifdef ENABLE_FMRADIO
    | (1 << 0)
#endif
#ifdef ENABLE_NOAA
    | (1 << 1)
#endif
#ifdef ENABLE_VOICE
    | (1 << 2)
#endif
#ifdef ENABLE_VOX
    | (1 << 3)
#endif
#ifdef ENABLE_ALARM
    | (1 << 4)
#endif
#ifdef ENABLE_TX1750
    | (1 << 5)
#endif
#ifdef ENABLE_PWRON_PASSWORD
    | (1 << 6)
#endif
#ifdef ENABLE_DTMF_CALLING
    | (1 << 7)
#endif
;

State[1] = 0
#ifdef ENABLE_FLASHLIGHT
    | (1 << 0)
#endif
#ifdef ENABLE_WIDE_RX
    | (1 << 1)
#endif
#ifdef ENABLE_BYP_RAW_DEMODULATORS
    | (1 << 2)
#endif
#ifdef ENABLE_FEAT_F4HWN_GAME
    | (1 << 3)
#endif
#ifdef ENABLE_AM_FIX
    | (1 << 4)
#endif
#ifdef ENABLE_SPECTRUM
    | (1 << 5)
#endif
#ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
    | (1 << 6)
#endif
;
#ifdef ENABLE_K5RX_CUSTOM_EEPROM
    EEPROM_WriteBuffer(EEPROM_K5RX_BUILD_OPTIONS_BASE, State);
#else
    EEPROM_WriteBuffer(0x1FF0, State);
#endif
}

#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
    void SETTINGS_WriteCurrentState(void)
    {
        uint8_t State[8];
#ifdef ENABLE_K5RX_CUSTOM_EEPROM
        if (!gK5RXHeaderValid)
            return;
        EEPROM_ReadBuffer(EEPROM_K5RX_SETTINGS_GENERAL_BASE + 16u, State, sizeof(State));
        gEeprom.CURRENT_LIST = gEeprom.SCAN_LIST_DEFAULT;
        State[5] = gEeprom.CURRENT_STATE;
        State[6] = gEeprom.CURRENT_LIST;
        EEPROM_WriteBuffer(EEPROM_K5RX_SETTINGS_GENERAL_BASE + 16u, State);
#else
        EEPROM_ReadBuffer(0x0E78, State, sizeof(State));
        //State[3] = (gEeprom.CURRENT_STATE << 4) | (gEeprom.BATTERY_SAVE & 0x0F);
        State[7] = (gEeprom.VFO_OPEN & 0x01) | ((gEeprom.CURRENT_STATE & 0x07) << 1) | ((gEeprom.SCAN_LIST_DEFAULT & 0x07) << 4);
        EEPROM_WriteBuffer(0x0E78, State);
#endif
    }
#endif

#ifdef ENABLE_FEAT_F4HWN_VOL
    void SETTINGS_WriteCurrentVol(void)
    {
#ifdef ENABLE_K5RX_CUSTOM_EEPROM
        // K5RX schema has no persisted volume field. Never repurpose calibration bytes.
        return;
#else
        uint8_t State[8];
        EEPROM_ReadBuffer(0x1F88, State, sizeof(State));
        State[6] = gEeprom.VOLUME_GAIN;
        EEPROM_WriteBuffer(0x1F88, State);
#endif
    }
#endif

#ifdef ENABLE_FEAT_F4HWN
void SETTINGS_ResetTxLock(void)
{
#ifdef ENABLE_K5RX_CUSTOM_EEPROM
    // K5RX channel records contain no TX lock state.
    return;
#else
    uint8_t State[8];
    for(uint8_t channel = 0; channel < 200; channel++)
    {
        uint16_t OffsetVFO = channel * 16;
        EEPROM_ReadBuffer(OffsetVFO + 8, State, sizeof(State));
        State[4] |= (1 << 6);
        EEPROM_WriteBuffer(OffsetVFO + 8, State);
    }
#endif
}
#endif
