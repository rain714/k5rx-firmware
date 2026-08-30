
#include <stddef.h>
#include <string.h>

#include "app/app.h"
#include "app/chFrScanner.h"
#ifdef ENABLE_AM_FIX
#include "am_fix.h"
#endif
#include "audio.h"
#ifdef ENABLE_K5RX_FAST_SCAN
#include "driver/bk4819-regs.h"
#include "driver/bk4819.h"
#include "driver/eeprom.h"
#include "eeprom-layout.h"
#endif
#include "functions.h"
#include "misc.h"
#include "radio.h"
#ifdef ENABLE_K5RX_FAST_SCAN
#include "scheduler.h"
#endif
#include "settings.h"
//#include "debugging.h"

int8_t            gScanStateDir;
bool              gScanKeepResult;
bool              gScanPauseMode;

#ifdef ENABLE_SCAN_RANGES
uint32_t          gScanRangeStart;
uint32_t          gScanRangeStop;
#endif

typedef enum {
    SCAN_NEXT_CHAN_SCANLIST1 = 0,
    SCAN_NEXT_CHAN_SCANLIST2,
    SCAN_NEXT_CHAN_DUAL_WATCH,
    SCAN_NEXT_CHAN_MR,
    SCAN_NEXT_NUM
} scan_next_chan_t;

scan_next_chan_t    currentScanList;
uint32_t            initialFrqOrChan;
uint8_t             initialCROSS_BAND_RX_TX;

#ifndef ENABLE_FEAT_F4HWN
    uint32_t lastFoundFrqOrChan;
#else
    uint32_t lastFoundFrqOrChan;
    uint32_t lastFoundFrqOrChanOld;
#endif

static void NextFreqChannel(void);
static bool NextMemChannel(void);

#ifdef ENABLE_K5RX_FAST_SCAN
#define SCAN_FAST_RSSI_MARGIN             16u
#define SCAN_FAST_SQUELCH_MARGIN           8u
#define SCAN_FAST_WEAK_MARGIN              8u
#define SCAN_FAST_RSSI_MAX             65535u
#define SCAN_FAST_SETTLE_US             5400u
#define SCAN_FAST_HF_VHF_BOUNDARY_HZ 28000000u
#define SCAN_FAST_NOISE_MARGIN              5u
#define SCAN_FAST_NOISE_ADAPT_UP_SAMPLES    8
#define SCAN_FAST_NOISE_ADAPT_DOWN_SAMPLES 16
#define SCAN_FAST_WARMUP_SAMPLES           32u
#define SCAN_FAST_WARMUP_MIN_CHANNELS       4u

typedef enum {
    SCAN_FAST_STATE_INACTIVE = 0,
    SCAN_FAST_STATE_READY,
    SCAN_FAST_STATE_SETTLING,
} scan_fast_state_t;

typedef enum {
    SCAN_FAST_STEP_PENDING = 0,
    SCAN_FAST_STEP_QUIET,
    SCAN_FAST_STEP_FULL_TUNE,
} scan_fast_step_result_t;

static uint16_t scanFastChannelBaseline[EEPROM_K5RX_CHANNEL_COUNT];
static uint16_t scanFastReg30;
static bool scanFastReg30Valid;
static uint32_t scanFastPrevFrequency;
static ModulationMode_t scanFastModulation = MODULATION_UKNOWN;
static uint8_t scanFastSquelchOpenRssiVhf;
static uint8_t scanFastSquelchOpenRssiUhf;
static bool scanFastLastFullTuneCandidate;
static channel_t scanFastPendingBaselineChannel = CHANNEL_NONE;
static uint16_t scanFastPendingBaselineRssi;
static uint8_t scanFastPendingNoise;
static uint16_t scanFastRateCurrent500ms;
static uint16_t scanFastRateEmaQ2;
static bool scanFastRateEmaInitialized;
static scan_fast_state_t scanFastState;
static channel_t scanFastProbeChannel = CHANNEL_NONE;
static uint32_t scanFastProbeSettleStartUs;
static bool scanFastNoiseReferenceValid;
static uint8_t scanFastNoiseReference;
static int8_t scanFastNoiseAdaptScore;
static uint8_t scanFastWarmupSamples;
static uint8_t scanFastWarmupTopNoise[4];
static bool scanFastWarmupEnabled;

static bool ScanFastScopeHasAtLeastChannels(uint8_t minimum)
{
    channel_t channel = MR_CHANNEL_FIRST;
    channel_t first = CHANNEL_NONE;
    uint8_t count = 0;

    while (count < minimum) {
        channel = RADIO_FindNextChannel(channel, RADIO_CHANNEL_UP, true, gEeprom.SCAN_LIST_DEFAULT);
        if (channel == CHANNEL_NONE)
            return false;
        if (first == CHANNEL_NONE)
            first = channel;
        else if (channel == first)
            return false;
        count++;
        channel = channel == MR_CHANNEL_LAST ? MR_CHANNEL_FIRST : (channel_t)(channel + 1u);
    }
    return true;
}

static void ScanFastResetNoiseState(void)
{
    scanFastNoiseReferenceValid = false;
    scanFastNoiseReference = 0;
    scanFastNoiseAdaptScore = 0;
    scanFastWarmupSamples = 0;
    memset(scanFastWarmupTopNoise, 0, sizeof(scanFastWarmupTopNoise));
    scanFastWarmupEnabled = ScanFastScopeHasAtLeastChannels(SCAN_FAST_WARMUP_MIN_CHANNELS);
    scanFastPendingNoise = 0;
}

static void ScanFastWarmupAddNoise(uint8_t noise)
{
    for (unsigned int i = 0; i < ARRAY_SIZE(scanFastWarmupTopNoise); i++) {
        if (noise > scanFastWarmupTopNoise[i]) {
            for (unsigned int j = ARRAY_SIZE(scanFastWarmupTopNoise) - 1u; j > i; j--)
                scanFastWarmupTopNoise[j] = scanFastWarmupTopNoise[j - 1u];
            scanFastWarmupTopNoise[i] = noise;
            break;
        }
    }
    if (scanFastWarmupSamples < UINT8_MAX)
        scanFastWarmupSamples++;
    if (scanFastWarmupSamples >= SCAN_FAST_WARMUP_SAMPLES) {
        scanFastNoiseReference = scanFastWarmupTopNoise[ARRAY_SIZE(scanFastWarmupTopNoise) - 1u];
        scanFastNoiseReferenceValid = true;
        scanFastWarmupEnabled = false;
    }
}

static void ScanFastUpdateNoiseReference(uint8_t noise)
{
    if (!scanFastNoiseReferenceValid) {
        scanFastNoiseReference = noise;
        scanFastNoiseReferenceValid = true;
        scanFastNoiseAdaptScore = 0;
        return;
    }
    if (noise > scanFastNoiseReference) {
        if (scanFastNoiseAdaptScore < SCAN_FAST_NOISE_ADAPT_UP_SAMPLES)
            scanFastNoiseAdaptScore++;
        if (scanFastNoiseAdaptScore >= SCAN_FAST_NOISE_ADAPT_UP_SAMPLES) {
            scanFastNoiseReference++;
            scanFastNoiseAdaptScore = 0;
        }
    } else if (noise < scanFastNoiseReference) {
        if (scanFastNoiseAdaptScore > -SCAN_FAST_NOISE_ADAPT_DOWN_SAMPLES)
            scanFastNoiseAdaptScore--;
        if (scanFastNoiseAdaptScore <= -SCAN_FAST_NOISE_ADAPT_DOWN_SAMPLES) {
            scanFastNoiseReference--;
            scanFastNoiseAdaptScore = 0;
        }
    }
}

static bool ScanFastNoiseCandidate(uint8_t noise)
{
    if (!scanFastNoiseReferenceValid)
        return false;
    const uint8_t threshold = scanFastNoiseReference > SCAN_FAST_NOISE_MARGIN ?
        (uint8_t)(scanFastNoiseReference - SCAN_FAST_NOISE_MARGIN) : 0u;
    return noise <= threshold;
}

static uint16_t ScanFastSaturatingAdd(uint16_t value, uint16_t add)
{
    return value > SCAN_FAST_RSSI_MAX - add ? SCAN_FAST_RSSI_MAX : (uint16_t)(value + add);
}

static uint16_t ScanFastSaturatingSub(uint16_t value, uint16_t sub)
{
    return value > sub ? (uint16_t)(value - sub) : 0;
}

static void ScanFastResetState(void)
{
    uint8_t threshold;

    scanFastReg30Valid = false;
    scanFastPrevFrequency = 0;
    scanFastModulation = MODULATION_UKNOWN;
    scanFastLastFullTuneCandidate = false;
    scanFastPendingBaselineChannel = CHANNEL_NONE;
    scanFastPendingBaselineRssi = 0;
    scanFastPendingNoise = 0;
    scanFastState = SCAN_FAST_STATE_INACTIVE;
    scanFastProbeChannel = CHANNEL_NONE;
    scanFastProbeSettleStartUs = 0;

    if (gEeprom.SQUELCH_LEVEL == 0) {
        scanFastSquelchOpenRssiVhf = 0;
        scanFastSquelchOpenRssiUhf = 0;
        return;
    }
    EEPROM_ReadBuffer(EEPROM_CAL_SQL_VHF_BASE + gEeprom.SQUELCH_LEVEL, &threshold, 1);
#ifdef ENABLE_SQUELCH_MORE_SENSITIVE
    threshold /= 2u;
#endif
    scanFastSquelchOpenRssiVhf = threshold;
    EEPROM_ReadBuffer(EEPROM_CAL_SQL_UHF_BASE + gEeprom.SQUELCH_LEVEL, &threshold, 1);
#ifdef ENABLE_SQUELCH_MORE_SENSITIVE
    threshold /= 2u;
#endif
    scanFastSquelchOpenRssiUhf = threshold;
}

static void ScanFastResetRateWindow(void)
{
    scanFastRateCurrent500ms = 0;
    scanFastRateEmaQ2 = 0;
    scanFastRateEmaInitialized = false;
}

void CHFRSCANNER_FastRateTimeSlice500ms(void)
{
    if (gScanStateDir == SCAN_OFF || gSetting_fast_scan_mode == FAST_SCAN_MODE_NORMAL) {
        ScanFastResetRateWindow();
        return;
    }

    const uint16_t currentQ2 = (uint16_t)(scanFastRateCurrent500ms << 3);
    scanFastRateCurrent500ms = 0;

    if (!scanFastRateEmaInitialized) {
        scanFastRateEmaQ2 = currentQ2;
        scanFastRateEmaInitialized = true;
    } else if (currentQ2 >= scanFastRateEmaQ2) {
        scanFastRateEmaQ2 += (uint16_t)((currentQ2 - scanFastRateEmaQ2) >> 2);
    } else {
        scanFastRateEmaQ2 -= (uint16_t)((scanFastRateEmaQ2 - currentQ2) >> 2);
    }
}

uint16_t CHFRSCANNER_FastChannelsPerSec(void)
{
    return (uint16_t)((scanFastRateEmaQ2 + 2u) >> 2);
}

static void ScanFastApplyChannelShape(ModulationMode_t modulation)
{
    if (scanFastModulation == modulation)
        return;
    scanFastModulation = modulation;
#ifdef ENABLE_AM_FIX
    if (modulation == MODULATION_AM && gSetting_AM_fix) {
        const bool savedAmFix = gSetting_AM_fix;
        gSetting_AM_fix = false;
        RADIO_SetModulation(modulation);
        gSetting_AM_fix = savedAmFix;
        AM_fix_enable(false);
    } else
#endif
        RADIO_SetModulation(modulation);
#ifdef ENABLE_AM_FIX
    BK4819_SetFilterBandwidth(BK4819_FILTER_BW_WIDE, true);
#else
    BK4819_SetFilterBandwidth(BK4819_FILTER_BW_WIDE, false);
#endif
}

static void ScanFastTune(uint32_t frequency)
{
    if (scanFastPrevFrequency == 0 ||
        ((frequency < SCAN_FAST_HF_VHF_BOUNDARY_HZ) !=
         (scanFastPrevFrequency < SCAN_FAST_HF_VHF_BOUNDARY_HZ)))
        BK4819_PickRXFilterPathBasedOnFrequency(frequency);
    scanFastPrevFrequency = frequency;
    BK4819_SetFrequency(frequency);
    BK4819_WriteRegister(BK4819_REG_30, 0);
    BK4819_WriteRegister(BK4819_REG_30, scanFastReg30);
}

static uint16_t ScanFastGetOpenSquelchThreshold(uint32_t frequency)
{
    return FREQUENCY_GetBand(frequency) < BAND4_174MHz ? scanFastSquelchOpenRssiVhf : scanFastSquelchOpenRssiUhf;
}

static bool ScanFastIsCandidate(uint16_t rssi, uint32_t frequency, uint16_t baseline)
{
    if (baseline == SCAN_FAST_RSSI_MAX)
        return true;
    const uint16_t baselineTrigger = ScanFastSaturatingAdd(baseline, SCAN_FAST_RSSI_MARGIN);
    const uint16_t squelchTrigger = ScanFastSaturatingSub(ScanFastGetOpenSquelchThreshold(frequency), SCAN_FAST_SQUELCH_MARGIN);
    const uint16_t rssiWithMargin = ScanFastSaturatingAdd(rssi, SCAN_FAST_WEAK_MARGIN);
    return rssiWithMargin >= baselineTrigger && rssiWithMargin >= squelchTrigger;
}

static void ScanFastUpdateChannelBaseline(channel_t channel, uint16_t rssi)
{
    if (channel >= EEPROM_K5RX_CHANNEL_COUNT)
        return;
    uint16_t *baseline = &scanFastChannelBaseline[channel];
    if (*baseline == SCAN_FAST_RSSI_MAX)
        *baseline = rssi;
    else
        *baseline = (uint16_t)((7u * *baseline + rssi + 4u) >> 3);
}

bool CHFRSCANNER_FastActive(void)
{
    return scanFastState != SCAN_FAST_STATE_INACTIVE;
}

void CHFRSCANNER_FastAppUpdate(void)
{
    if (!CHFRSCANNER_FastActive())
        return;
    if (gScanStateDir == SCAN_OFF) {
        scanFastState = SCAN_FAST_STATE_INACTIVE;
        return;
    }
    if (!IS_MR_CHANNEL(gNextMrChannel) || gSetting_fast_scan_mode == FAST_SCAN_MODE_NORMAL ||
        gEeprom.SQUELCH_LEVEL == 0 || gPttIsPressed) {
        scanFastState = SCAN_FAST_STATE_INACTIVE;
        gScanPauseDelayIn_10ms = 1;
        gScheduleScanListen = false;
        return;
    }
    gScanPauseDelayIn_10ms = 0;
    gScheduleScanListen = false;
    (void)NextMemChannel();
}

static scan_fast_step_result_t ScanFastStepChannel(channel_t channel)
{
    const SETTINGS_K5RX_ChannelRecord_t *info;

    if (scanFastState == SCAN_FAST_STATE_SETTLING) {
        if (scanFastProbeChannel != channel) {
            scanFastState = SCAN_FAST_STATE_INACTIVE;
            return SCAN_FAST_STEP_FULL_TUNE;
        }
        if (SCHEDULER_NowUs() - scanFastProbeSettleStartUs < SCAN_FAST_SETTLE_US)
            return SCAN_FAST_STEP_PENDING;
        info = SETTINGS_GetK5RXChannelRecord(channel);
        if (info == NULL) {
            scanFastState = SCAN_FAST_STATE_INACTIVE;
            return SCAN_FAST_STEP_FULL_TUNE;
        }
        scanFastState = SCAN_FAST_STATE_READY;
    } else {
        if (gSetting_fast_scan_mode == FAST_SCAN_MODE_NORMAL || gEeprom.SQUELCH_LEVEL == 0)
            return SCAN_FAST_STEP_FULL_TUNE;
        info = SETTINGS_GetK5RXChannelRecord(channel);
        if (info == NULL || (info->modulation != MODULATION_FM && info->modulation != MODULATION_AM))
            return SCAN_FAST_STEP_FULL_TUNE;
        const bool shapeApplied = scanFastModulation != info->modulation;
        ScanFastApplyChannelShape(info->modulation);
        if (!scanFastReg30Valid || shapeApplied) {
            scanFastReg30 = BK4819_ReadRegister(BK4819_REG_30) & ~BK4819_REG_30_MASK_ENABLE_AF_DAC;
            scanFastReg30Valid = true;
        }
        ScanFastTune(info->frequency);
        scanFastProbeChannel = channel;
        scanFastProbeSettleStartUs = SCHEDULER_NowUs();
        scanFastState = SCAN_FAST_STATE_SETTLING;
        return SCAN_FAST_STEP_PENDING;
    }

    (void)BK4819_GetRSSI();
    const uint16_t rssi = BK4819_GetRSSI();
    const uint16_t baseline = scanFastChannelBaseline[channel];
    const bool rssiCandidate = ScanFastIsCandidate(rssi, info->frequency, baseline);
    const uint8_t noise = BK4819_GetExNoiceIndicator();
    bool candidate;
    bool warmupSkip = false;

    if (scanFastWarmupEnabled) {
        ScanFastWarmupAddNoise(noise);
        candidate = false;
        warmupSkip = true;
    } else {
        const bool noiseCandidate = ScanFastNoiseCandidate(noise);
        candidate = baseline == SCAN_FAST_RSSI_MAX && scanFastNoiseReferenceValid ?
            noiseCandidate : (rssiCandidate || noiseCandidate);
    }

    if (!candidate) {
        if (!warmupSkip) {
            ScanFastUpdateChannelBaseline(channel, rssi);
            ScanFastUpdateNoiseReference(noise);
        }
        scanFastPendingBaselineChannel = CHANNEL_NONE;
        scanFastLastFullTuneCandidate = false;
        return SCAN_FAST_STEP_QUIET;
    }

    scanFastPendingBaselineChannel = channel;
    scanFastPendingBaselineRssi = rssi;
    scanFastPendingNoise = noise;
    scanFastLastFullTuneCandidate = true;
    return SCAN_FAST_STEP_FULL_TUNE;
}
#endif

void CHFRSCANNER_Start(const bool storeBackupSettings, const int8_t scan_direction)
{
    if (storeBackupSettings) {
        initialCROSS_BAND_RX_TX = gEeprom.CROSS_BAND_RX_TX;
        gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_OFF;
        gScanKeepResult = false;
    }
    
    RADIO_SelectVfos();

    gNextMrChannel   = gRxVfo->CHANNEL_SAVE;
    currentScanList = SCAN_NEXT_CHAN_SCANLIST1;
#ifdef ENABLE_K5RX_FAST_SCAN
    memset(scanFastChannelBaseline, 0xFF, sizeof(scanFastChannelBaseline));
    ScanFastResetState();
    ScanFastResetNoiseState();
    ScanFastResetRateWindow();
#endif
#ifdef ENABLE_K5RX_CUSTOM_EEPROM
    if (IS_MR_CHANNEL(gNextMrChannel) && !RADIO_ScanScopeHasChannel(gEeprom.SCAN_LIST_DEFAULT)) {
        if (storeBackupSettings) {
            gEeprom.CROSS_BAND_RX_TX = initialCROSS_BAND_RX_TX;
            initialCROSS_BAND_RX_TX = CROSS_BAND_OFF;
        }
        gScanStateDir = SCAN_OFF;
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        return;
    }
#endif
    gScanStateDir    = scan_direction;
#ifdef ENABLE_K5RX_FAST_SCAN
    if (IS_MR_CHANNEL(gNextMrChannel) && gSetting_fast_scan_mode == FAST_SCAN_MODE_FAST && gEeprom.SQUELCH_LEVEL > 0)
        scanFastState = SCAN_FAST_STATE_READY;
#endif

    if (IS_MR_CHANNEL(gNextMrChannel))
    {   // channel mode
        if (storeBackupSettings) {
            initialFrqOrChan = gRxVfo->CHANNEL_SAVE;
            lastFoundFrqOrChan = initialFrqOrChan;
        }
#ifdef ENABLE_K5RX_FAST_SCAN
        if (!CHFRSCANNER_FastActive())
#endif
            (void)NextMemChannel();
    }
    else
    {   // frequency mode
        if (storeBackupSettings) {
            initialFrqOrChan = gRxVfo->freq_config_RX.Frequency;
            lastFoundFrqOrChan = initialFrqOrChan;
        }
        NextFreqChannel();
    }

#ifdef ENABLE_FEAT_F4HWN
    lastFoundFrqOrChanOld = lastFoundFrqOrChan;
#endif

#ifdef ENABLE_K5RX_FAST_SCAN
    gScanPauseDelayIn_10ms = CHFRSCANNER_FastActive() ? 0 : scan_pause_delay_in_2_10ms;
#else
    gScanPauseDelayIn_10ms = scan_pause_delay_in_2_10ms;
#endif
    gScheduleScanListen    = false;
    gRxReceptionMode       = RX_MODE_NONE;
    gScanPauseMode         = false;
}

/*
void CHFRSCANNER_ContinueScanning(void)
{
    if (IS_FREQ_CHANNEL(gNextMrChannel))
    {
        if (gCurrentFunction == FUNCTION_INCOMING)
            APP_StartListening(gMonitor ? FUNCTION_MONITOR : FUNCTION_RECEIVE);
        else
            NextFreqChannel();  // switch to next frequency
    }
    else
    {
        if (gCurrentCodeType == CODE_TYPE_OFF && gCurrentFunction == FUNCTION_INCOMING)
            APP_StartListening(gMonitor ? FUNCTION_MONITOR : FUNCTION_RECEIVE);
        else
            NextMemChannel();    // switch to next channel
    }
    
    gScanPauseMode      = false;
    gRxReceptionMode    = RX_MODE_NONE;
    gScheduleScanListen = false;
}
*/

void CHFRSCANNER_ContinueScanning(void)
{
#ifdef ENABLE_K5RX_FAST_SCAN
    if (scanFastLastFullTuneCandidate && gCurrentFunction != FUNCTION_INCOMING && !g_SquelchLost) {
        if (scanFastPendingBaselineChannel != CHANNEL_NONE)
            ScanFastUpdateChannelBaseline(scanFastPendingBaselineChannel, scanFastPendingBaselineRssi);
        ScanFastUpdateNoiseReference(scanFastPendingNoise);
        scanFastPendingNoise = 0;
        scanFastModulation = MODULATION_UKNOWN;
        scanFastPendingBaselineChannel = CHANNEL_NONE;
        scanFastPendingBaselineRssi = 0;
        scanFastLastFullTuneCandidate = false;
    }
#endif

    if (gCurrentFunction == FUNCTION_INCOMING &&
        (IS_FREQ_CHANNEL(gNextMrChannel) || gCurrentCodeType == CODE_TYPE_OFF))
    {
        APP_StartListening(gMonitor ? FUNCTION_MONITOR : FUNCTION_RECEIVE);
    }
    else
    {
        if (IS_FREQ_CHANNEL(gNextMrChannel)) {
            NextFreqChannel();
        } else {
#ifdef ENABLE_K5RX_FAST_SCAN
            if (gSetting_fast_scan_mode == FAST_SCAN_MODE_FAST && gEeprom.SQUELCH_LEVEL > 0) {
                scanFastState = SCAN_FAST_STATE_READY;
                gScanPauseDelayIn_10ms = 0;
            } else
#endif
                (void)NextMemChannel();
        }
    }

    gScanPauseMode      = false;
    gRxReceptionMode    = RX_MODE_NONE;
    gScheduleScanListen = false;
}

void CHFRSCANNER_Found(void)
{
#ifdef ENABLE_K5RX_FAST_SCAN
    ScanFastResetState();
#endif
    if (gEeprom.SCAN_RESUME_MODE > 80) {
        if (!gScanPauseMode) {
            gScanPauseDelayIn_10ms = scan_pause_delay_in_5_10ms * (gEeprom.SCAN_RESUME_MODE - 80) * 5;
            gScanPauseMode = true;
        }
    } else {
        gScanPauseDelayIn_10ms = 0;
    }

    // gScheduleScanListen is always false...
    gScheduleScanListen = false;

    /*
    if(gEeprom.SCAN_RESUME_MODE > 1 && gEeprom.SCAN_RESUME_MODE < 26)
    {
        if (!gScanPauseMode)
        {
            gScanPauseDelayIn_10ms = scan_pause_delay_in_5_10ms * (gEeprom.SCAN_RESUME_MODE - 1) * 5;
            gScheduleScanListen    = false;
            gScanPauseMode         = true;
        }
    }
    else
    {
        gScanPauseDelayIn_10ms = 0;
        gScheduleScanListen    = false;
    }
    */

    /*
    switch (gEeprom.SCAN_RESUME_MODE)
    {
        case SCAN_RESUME_TO:
            if (!gScanPauseMode)
            {
                gScanPauseDelayIn_10ms = scan_pause_delay_in_1_10ms;
                gScheduleScanListen    = false;
                gScanPauseMode         = true;
            }
            break;

        case SCAN_RESUME_CO:
        case SCAN_RESUME_SE:
            gScanPauseDelayIn_10ms = 0;
            gScheduleScanListen    = false;
            break;
    }
    */

#ifdef ENABLE_FEAT_F4HWN
    lastFoundFrqOrChanOld = lastFoundFrqOrChan;
#endif

    if (IS_MR_CHANNEL(gRxVfo->CHANNEL_SAVE)) { //memory scan
        lastFoundFrqOrChan = gRxVfo->CHANNEL_SAVE;
    }
    else { // frequency scan
        lastFoundFrqOrChan = gRxVfo->freq_config_RX.Frequency;
    }


    gScanKeepResult = true;
}

void CHFRSCANNER_Stop(void)
{
    if(initialCROSS_BAND_RX_TX != CROSS_BAND_OFF) {
        gEeprom.CROSS_BAND_RX_TX = initialCROSS_BAND_RX_TX;
        initialCROSS_BAND_RX_TX = CROSS_BAND_OFF;
    }
    
    gScanStateDir = SCAN_OFF;
#ifdef ENABLE_K5RX_FAST_SCAN
    ScanFastResetState();
    ScanFastResetRateWindow();
#endif

    const uint32_t chFr = gScanKeepResult ? lastFoundFrqOrChan : initialFrqOrChan;
    const bool channelChanged = chFr != initialFrqOrChan;
    if (IS_MR_CHANNEL(gNextMrChannel)) {
        gEeprom.MrChannel[gEeprom.RX_VFO]     = chFr;
        gEeprom.ScreenChannel[gEeprom.RX_VFO] = chFr;
        RADIO_ConfigureChannel(gEeprom.RX_VFO, VFO_CONFIGURE_RELOAD);

        if(channelChanged) {
            SETTINGS_SaveVfoIndices();
            gUpdateStatus = true;
        }
    }
    else {
        gRxVfo->freq_config_RX.Frequency = chFr;
        RADIO_ApplyOffset(gRxVfo);
        RADIO_ConfigureSquelchAndOutputPower(gRxVfo);
        if(channelChanged) {
            SETTINGS_SaveChannel(gRxVfo->CHANNEL_SAVE, gEeprom.RX_VFO, gRxVfo, 1);
        }
    }

    #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
        gEeprom.CURRENT_STATE = 0;
        SETTINGS_WriteCurrentState();
    #endif

    RADIO_SetupRegisters(true);
    gUpdateDisplay = true;
}

static void NextFreqChannel(void)
{
#ifdef ENABLE_SCAN_RANGES
    if(gScanRangeStart) {
        gRxVfo->freq_config_RX.Frequency = APP_SetFreqByStepAndLimits(gRxVfo, gScanStateDir, gScanRangeStart, gScanRangeStop);
    }
    else
#endif
        gRxVfo->freq_config_RX.Frequency = APP_SetFrequencyByStep(gRxVfo, gScanStateDir);

    RADIO_ApplyOffset(gRxVfo);
    RADIO_ConfigureSquelchAndOutputPower(gRxVfo);
    RADIO_SetupRegisters(true);

#ifdef ENABLE_FASTER_CHANNEL_SCAN
    gScanPauseDelayIn_10ms = 9;   // 90ms
#else
    gScanPauseDelayIn_10ms = scan_pause_delay_in_6_10ms;
#endif

    gUpdateDisplay     = true;
}

static bool NextMemChannel(void)
{
#ifdef ENABLE_K5RX_CUSTOM_EEPROM
    static channel_t prev_mr_chan = MR_CHANNEL_FIRST;
    const bool       enabled   = (gEeprom.SCAN_LIST_DEFAULT > 0 && gEeprom.SCAN_LIST_DEFAULT < 4) ? gEeprom.SCAN_LIST_ENABLED[gEeprom.SCAN_LIST_DEFAULT - 1] : true;
    const channel_t  chan1     = (gEeprom.SCAN_LIST_DEFAULT > 0 && gEeprom.SCAN_LIST_DEFAULT < 4) ? gEeprom.SCANLIST_PRIORITY_CH1[gEeprom.SCAN_LIST_DEFAULT - 1] : CHANNEL_NONE;
    const channel_t  chan2     = (gEeprom.SCAN_LIST_DEFAULT > 0 && gEeprom.SCAN_LIST_DEFAULT < 4) ? gEeprom.SCANLIST_PRIORITY_CH2[gEeprom.SCAN_LIST_DEFAULT - 1] : CHANNEL_NONE;
    const channel_t  prev_chan = gNextMrChannel;
    channel_t        chan      = MR_CHANNEL_FIRST;
#ifdef ENABLE_K5RX_FAST_SCAN
    const bool resumedFastProbe = scanFastState == SCAN_FAST_STATE_SETTLING;
#endif
#else
    static unsigned int prev_mr_chan = 0;
    const bool          enabled      = (gEeprom.SCAN_LIST_DEFAULT > 0 && gEeprom.SCAN_LIST_DEFAULT < 4) ? gEeprom.SCAN_LIST_ENABLED[gEeprom.SCAN_LIST_DEFAULT - 1] : true;
    const int           chan1        = (gEeprom.SCAN_LIST_DEFAULT > 0 && gEeprom.SCAN_LIST_DEFAULT < 4) ? gEeprom.SCANLIST_PRIORITY_CH1[gEeprom.SCAN_LIST_DEFAULT - 1] : -1;
    const int           chan2        = (gEeprom.SCAN_LIST_DEFAULT > 0 && gEeprom.SCAN_LIST_DEFAULT < 4) ? gEeprom.SCANLIST_PRIORITY_CH2[gEeprom.SCAN_LIST_DEFAULT - 1] : -1;
    const unsigned int  prev_chan    = gNextMrChannel;
    unsigned int        chan         = 0;
#endif

    //char str[64] = "";

#ifdef ENABLE_K5RX_FAST_SCAN
    if (!resumedFastProbe) {
#endif
    if (enabled)
    {
        switch (currentScanList)
        {
            case SCAN_NEXT_CHAN_SCANLIST1:
                prev_mr_chan = gNextMrChannel;
    
                //sprintf(str, "-> Chan1 %d\n", chan1 + 1);
                //LogUart(str);

#ifdef ENABLE_K5RX_CUSTOM_EEPROM
                if (chan1 != CHANNEL_NONE)
#else
                if (chan1 >= 0)
#endif
                {
                    if (RADIO_CheckValidChannel(chan1, false, gEeprom.SCAN_LIST_DEFAULT))
                    {
                        currentScanList = SCAN_NEXT_CHAN_SCANLIST1;
                        gNextMrChannel   = chan1;
                        break;
                    }
                }

                [[fallthrough]];
            case SCAN_NEXT_CHAN_SCANLIST2:

                //sprintf(str, "-> Chan2 %d\n", chan2 + 1);
                //LogUart(str);

#ifdef ENABLE_K5RX_CUSTOM_EEPROM
                if (chan2 != CHANNEL_NONE)
#else
                if (chan2 >= 0)
#endif
                {
                    if (RADIO_CheckValidChannel(chan2, false, gEeprom.SCAN_LIST_DEFAULT))
                    {
                        currentScanList = SCAN_NEXT_CHAN_SCANLIST2;
                        gNextMrChannel   = chan2;
                        break;
                    }
                }

                [[fallthrough]];
            /*
            case SCAN_NEXT_CHAN_SCANLIST3:
                if (chan3 >= 0)
                {
                    if (RADIO_CheckValidChannel(chan3, false, 0))
                    {
                        currentScanList = SCAN_NEXT_CHAN_SCANLIST3;
                        gNextMrChannel   = chan3;
                        break;
                    }
                }
                [[fallthrough]];
            */
            // this bit doesn't yet work if the other VFO is a frequency
            case SCAN_NEXT_CHAN_DUAL_WATCH:
                // dual watch is enabled - include the other VFO in the scan
//              if (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF)
//              {
//                  chan = (gEeprom.RX_VFO + 1) & 1u;
//                  chan = gEeprom.ScreenChannel[chan];
//                  if (IS_MR_CHANNEL(chan))
//                  {
//                      currentScanList = SCAN_NEXT_CHAN_DUAL_WATCH;
//                      gNextMrChannel   = chan;
//                      break;
//                  }
//              }

            default:
            case SCAN_NEXT_CHAN_MR:
                currentScanList = SCAN_NEXT_CHAN_MR;
                gNextMrChannel   = prev_mr_chan;
#ifdef ENABLE_K5RX_CUSTOM_EEPROM
                chan             = CHANNEL_NONE;
#else
                chan             = 0xff;
#endif
                break;
        }
    }

#ifdef ENABLE_K5RX_CUSTOM_EEPROM
    if (!enabled || chan == CHANNEL_NONE)
#else
    if (!enabled || chan == 0xff)
#endif
    {
        chan = RADIO_FindNextChannel(gNextMrChannel + gScanStateDir, gScanStateDir, true, gEeprom.SCAN_LIST_DEFAULT);
#ifdef ENABLE_K5RX_CUSTOM_EEPROM
        if (chan == CHANNEL_NONE)
#else
        if (chan == 0xFF)
#endif
        {   // no valid channel found
            chan = MR_CHANNEL_FIRST;
        }
        
        gNextMrChannel = chan;

        //sprintf(str, "----> Chan %d\n", chan + 1);
        //LogUart(str);
    }
#ifdef ENABLE_K5RX_FAST_SCAN
    }
#endif

    if (gNextMrChannel != prev_chan
#ifdef ENABLE_K5RX_FAST_SCAN
        || resumedFastProbe
#endif
    )
    {
#ifdef ENABLE_K5RX_FAST_SCAN
        if (!resumedFastProbe && scanFastRateCurrent500ms != UINT16_MAX)
            scanFastRateCurrent500ms++;
        const scan_fast_step_result_t result = ScanFastStepChannel(gNextMrChannel);
        if (result == SCAN_FAST_STEP_PENDING)
            return false;
        if (result == SCAN_FAST_STEP_QUIET) {
            gScanPauseDelayIn_10ms = 0;
            if (enabled && ++currentScanList >= SCAN_NEXT_NUM)
                currentScanList = SCAN_NEXT_CHAN_SCANLIST1;
            return true;
        }
#endif
        gEeprom.MrChannel[    gEeprom.RX_VFO] = gNextMrChannel;
        gEeprom.ScreenChannel[gEeprom.RX_VFO] = gNextMrChannel;

        RADIO_ConfigureChannel(gEeprom.RX_VFO, VFO_CONFIGURE_RELOAD);
        RADIO_SetupRegisters(true);
#ifdef ENABLE_K5RX_FAST_SCAN
        scanFastState = SCAN_FAST_STATE_INACTIVE;
#endif

        gUpdateDisplay = true;
    }

#ifdef ENABLE_FASTER_CHANNEL_SCAN
    gScanPauseDelayIn_10ms = 9;  // 90ms .. <= ~60ms it misses signals (squelch response and/or PLL lock time) ?
#else
    gScanPauseDelayIn_10ms = scan_pause_delay_in_3_10ms;
#endif

    if (enabled)
        if (++currentScanList >= SCAN_NEXT_NUM)
            currentScanList = SCAN_NEXT_CHAN_SCANLIST1;  // back round we go

    return false;
}
