#include "Logic.h"
#include "Helper.h"
#include "OpenKNX.h"
#include "PCA9632.h"
#include "Timer.h"
#include "TimerRestore.h"

uint8_t Logic::sMagicWord[] = {0xAE, 0x49, 0xD2, 0x9F};
Timer &Logic::sTimer = Timer::instance();                      // singleton
TimerRestore &Logic::sTimerRestore = TimerRestore::instance(); // singleton

char Logic::sDiagnoseBuffer[16] = {0};

uint16_t Logic::flashSize()
{
    // Version + Data (Channel * Inputs * (Dpt + Value))
    return 1 + (LOG_ChannelsFirmware * 2 * 5);
}

const std::string Logic::name()
{
    return "Logic";
}

Logic::Logic()
{
    LogicChannel::sLogic = this;
}

Logic::~Logic()
{
}

LogicChannel *Logic::getChannel(uint8_t iChannelId)
{
    return mChannel[iChannelId];
}

uint8_t Logic::getChannelId(LogicChannel *iChannel)
{
    uint8_t lResult = -1;
    for (uint8_t lIndex = 0; lIndex < mNumChannels; lIndex++)
    {
        if (mChannel[lIndex] == iChannel)
        {
            lResult = lIndex;
            break;
        }
    }
    return lResult;
}

void Logic::addKoLookup(uint16_t iKoNumber, uint8_t iChannelId, uint8_t iIOIndex)
{
    // first implementation, in future we use sorted insert
    mKoLookup[mNumKoLookups].koNumber = iKoNumber;
    mKoLookup[mNumKoLookups].channelIndex = iChannelId;
    mKoLookup[mNumKoLookups].ioIndex = iIOIndex;
    if (mNumKoLookups < cCountKoLookups)
        mNumKoLookups++;
}

bool Logic::getKoLookup(uint16_t iKoNumber, sKoLookup **iKoLookup)
{
    sKoLookup *lIterator = *iKoLookup;
    if (*iKoLookup == 0)
        lIterator = &mKoLookup[0];
    else
        lIterator++;
    while (lIterator->koNumber > 0)
    {
        if (lIterator->koNumber == iKoNumber)
        {
            *iKoLookup = lIterator;
            return true;
        }
        else
        {
            lIterator++;
        }
    }
    return false;
}


// REVIEW
// bool Logic::prepareChannels()
void Logic::prepareChannels()
{
    // bool lResult = false;
    logInfo("prepareChannels");
    for (uint8_t lIndex = 0; lIndex < mNumChannels; lIndex++)
    {
        // Important: lResult has to be the last argument in this OR,
        // otherwise prepareChannel might be not called
        // lResult = mChannel[lIndex]->prepareChannel() || lResult;
        mChannel[lIndex]->prepareChannel();
    }
}

// we trigger all associated internal inputs with the new value
void Logic::processAllInternalInputs(LogicChannel *iChannel, bool iValue)
{
    // search for any internal input associated to this channel
    uint8_t lChannelId = getChannelId(iChannel);
    for (uint8_t lIndex = 0; lIndex < mNumChannels; lIndex++)
    {
        LogicChannel *lChannel = mChannel[lIndex];
        lChannel->processInternalInputs(lChannelId, iValue);
    }
}


void Logic::processAfterStartupDelay()
{
    logInfo("afterStartupDelay");

    if (ParamLOG_VacationRead)
        KoLOG_Vacation.requestObjectRead();

    prepareChannels();
}

void Logic::processReadRequests()
{
    // TODO obsolete durch kompakteres firstLoop()
    // static bool sLogicProcessReadRequestsCalled = false;
    static uint32_t sDelay = 19000;

    // // the following code should be called only once after initial startup delay
    // if (!sLogicProcessReadRequestsCalled)
    // {
    //     if (knx.paramByte(LOG_VacationRead) & LOG_VacationReadMask)
    //     {
    //         knx.getGroupObject(LOG_KoVacation).requestObjectRead();
    //     }
    //     // we put this here, because all other places are too early, knx stack is not up to date to send according init values
    //     // got from flash storage
    //     prepareChannels();
    //     sLogicProcessReadRequestsCalled = true;
    // }
    // date and time are red from bus every minute until a response is received
    if (ParamLOG_ReadTimeDate)
    {
        eTimeValid lValid = sTimer.isTimerValid();
        if (delayCheck(sDelay, 30000) && lValid != tmValid)
        {
            logInfoP("Time Valid? %i", lValid);
            sDelay = millis();
            if (lValid != tmMinutesValid)
                KoLOG_Time.requestObjectRead();
            if (lValid != tmDateValid)
                KoLOG_Date.requestObjectRead();
        }
        // if date and/or time is known, we read also summertime information
        if (sDelay > 0 && lValid == tmValid)
        {
            sDelay = 0;
            KoLOG_IsSummertime.requestObjectRead();
        }
    }
}

void Logic::readFlash(const uint8_t *iBuffer, const uint16_t iSize)
{
    if (iSize == 0) // first call - without data
        return;

    uint8_t lVersion = openknx.flash.readByte();
    if (lVersion != 1) // version unknown
    {
        logDebugP("Wrong version of flash data (%i)", lVersion);
        return;
    }

    uint8_t lMaxChannels = (iSize - 1) / (2 * 5);
    logDebugP("Reading channel data from flash (%i/%i)", lMaxChannels, mNumChannels);
    for (uint8_t lIndex = 0; lIndex < MIN(mNumChannels, lMaxChannels); lIndex++)
    {
        mChannel[lIndex]->restore();
    }
}

void Logic::writeFlash()
{
    openknx.flash.writeByte(1); // Version
    for (uint8_t lIndex = 0; lIndex < mNumChannels; lIndex++)
    {
        mChannel[lIndex]->save();
    }
}

// on input level, all dpt > 1 values are converted to bool by the according converter
void Logic::processInputKo(GroupObject &iKo)
{
    // we have to check first, if external KO are used
    sKoLookup *lKoLookup = nullptr;
    while (getKoLookup(iKo.asap(), &lKoLookup))
    {
        LogicChannel *lChannel = mChannel[lKoLookup->channelIndex];
        lChannel->processInput(lKoLookup->ioIndex);
    }

    
    long min = LOG_KoOffset + LOG_KoKOfE1;
    long max = LOG_KoOffset + LOG_KoKOfE1 + mNumChannels * LOG_KoBlockSize;


    if (iKo.asap() == LOG_KoTime) {
        if (ParamLOG_CombinedTimeDate) {
            KNXValue value = "";

            // first ensure we have a valid data-time content
            // (including the correct length)
            if (iKo.tryValue(value, getDPT(VAL_DPT_19))) {

                // use raw value, as current version of knx do not provide access to all fields
                // TODO DPT19: check integration of extended DPT19 access into knx or OpenKNX-Commons
                // size is ensured to be 8 Byte
                uint8_t *raw = iKo.valueRef();

                /*
                const bool flagFault = raw[6] & 0x80;
                // ignore working day (WD, NWD): raw[6] & 0x40, raw[6] & 0x20
                const bool flagNoYear = raw[6] & 0x10;
                const bool flagNoDate = raw[6] & 0x08;
                // ignore NDOW: raw[6] & 0x04
                const bool flagNoTime = raw[6] & 0x02;
                const bool flagSuti = raw[6] & 0x01;
                // ignore quality of clock (CLQ): raw[7] & 0x80
                // ignore synchronisation source reliablity (SRC): raw[7] & 0x40
                */

                // ignore inputs with:
                // * F - fault
                // * NY - missing year
                // * ND - missing date
                // * NT - missing time
                if (!(raw[6] & (DPT19_FAULT | DPT19_NO_YEAR | DPT19_NO_DATE | DPT19_NO_TIME))) {
                    struct tm lTmp = value;
                    sTimer.setDateTimeFromBus(&lTmp);
                    const bool lSummertime = raw[6] & DPT19_SUMMERTIME;
                    if (ParamLOG_SummertimeAll == VAL_STIM_FROM_DPT19)
                        sTimer.setIsSummertime(lSummertime);
                }
            }
        } else {
            KNXValue value = "";
            // ensure we have a valid time content
            if (iKo.tryValue(value, getDPT(VAL_DPT_10))) {
                struct tm lTmp = value;
                sTimer.setTimeFromBus(&lTmp);
            }
        }
    } else if (iKo.asap() == LOG_KoDate) {
        KNXValue value = "";
        // ensure we have a valid date content
        if (iKo.tryValue(value, getDPT(VAL_DPT_11))) {
            struct tm lTmp = value;
        sTimer.setDateFromBus(&lTmp);
    }
    } else if (iKo.asap() == LOG_KoIsSummertime) {
        sTimer.setIsSummertime(iKo.value(getDPT(VAL_DPT_1)));
    } else if (iKo.asap() == LOG_KoDiagnose) {
        processDiagnoseCommand(iKo);
    }
#ifdef BUZZER_PIN
    else if (iKo.asap() == LOG_KoBuzzerLock)
    {
        // turn off buzzer in case of lock
        if (iKo.value(getDPT(VAL_DPT_1)))
            noTone(BUZZER_PIN);
    }
#endif
#ifdef I2C_RGBLED_DEVICE_ADDRESS
    else if (iKo.asap() == LOG_KoLedLock)
    {
        // turn off LED in case of lock
        if (iKo.value(getDPT(VAL_DPT_1)))
            PCA9632_SetColor(0, 0, 0);
    }
#endif
    // REVIEW: Wäre dieser Check nicht im LogicChannel besser aufgehoben?
    else if (iKo.asap() >= LOG_KoOffset + LOG_KoKOfE1 && iKo.asap() < LOG_KoOffset + LOG_KoKOfE1 + mNumChannels * LOG_KoBlockSize)
    {
        uint16_t lKoNumber = iKo.asap() - LOG_KoOffset - LOG_KoKOfE1;
        uint8_t lChannelId = lKoNumber / LOG_KoBlockSize;
        uint8_t lIOIndex = lKoNumber % LOG_KoBlockSize + 1;
        LogicChannel *lChannel = mChannel[lChannelId];
        lChannel->processInput(lIOIndex);
    }

}

char *Logic::initDiagnose(GroupObject &iKo)
{
    memcpy(sDiagnoseBuffer, iKo.valueRef(), 14);
    return sDiagnoseBuffer;
}

char *Logic::getDiagnoseBuffer()
{
    return sDiagnoseBuffer;
}

bool Logic::processDiagnoseCommand()
{
    bool lResult = false;
    // diagnose is interactive and reacts on commands
    switch (sDiagnoseBuffer[0])
    {
        case 'l': {
            // Command l<nn>: Logic inputs and output of last execution
            // find channel and dispatch
            uint8_t lIndex = (sDiagnoseBuffer[1] - '0') * 10 + sDiagnoseBuffer[2] - '0' - 1;
            if (lIndex < LOG_ChannelCount) {
                lResult = mChannel[lIndex]->processDiagnoseCommand(sDiagnoseBuffer);
            } else {
                // ignore invalid channel
                lResult = false;
            }
            break;
        }
        case 't': {
            // return internal time (might differ from external
            uint8_t lHour = sTimer.getHour();
            uint8_t lMinute = sTimer.getMinute();
            uint8_t lSecond = sTimer.getSecond();
            uint8_t lDay = sTimer.getDay();
            uint8_t lMonth = sTimer.getMonth();
            // this if prevents stupid warnings
            if (lHour < 24 && lMinute < 60 && lSecond < 60 && lDay < 32 && lMonth < 13)
                snprintf(sDiagnoseBuffer, 15, "%02d:%02d:%02d %02d.%02d", lHour, lMinute, lSecond, lDay, lMonth);
            lResult = true;
            break;
        }
        case 'r': {
            if (sDiagnoseBuffer[1] == 'e')
            {
                // return sunrise and sunset for a specific elevation teSDD,
                // where S=Sign(+,-) and DD ist elevation in degree
                if (sDiagnoseBuffer[2] == '-' || sDiagnoseBuffer[2] == '+')
                {
                    double lDegree = ((sDiagnoseBuffer[3] - '0') * 10 + sDiagnoseBuffer[4] - '0');
                    uint8_t lMinute = ((sDiagnoseBuffer[5] - '0') * 10 + sDiagnoseBuffer[6] - '0'); 
                    lDegree = (lDegree + lMinute / 60.0) * (sDiagnoseBuffer[2] == '+' ? 1 : -1);
                    sTime lSunrise;
                    sTime lSunset;
                    sTimer.getSunDegree(SUN_SUNRISE, lDegree, &lSunrise);
                    sTimer.getSunDegree(SUN_SUNSET, lDegree, &lSunset);
                    // this if prevents stupid warnings
                    if (lSunrise.hour >= 0 && lSunrise.hour < 24 && lSunrise.minute >= 0 && lSunrise.minute < 60 && lSunset.hour >= 0 && lSunset.hour < 24 && lSunset.minute >= 0 && lSunset.minute < 60)
                        snprintf(sDiagnoseBuffer, 15, "R%02d:%02d S%02d:%02d", lSunrise.hour, lSunrise.minute, lSunset.hour, lSunset.minute);
                }
                else
                {
                    snprintf(sDiagnoseBuffer, 15, "TRY re-0600");
                }
                lResult = true;
            }
            else
            {
                // return sunrise and sunset
                sTime *lSunrise = sTimer.getSunInfo(SUN_SUNRISE);
                sTime *lSunset = sTimer.getSunInfo(SUN_SUNSET);
                // this if prevents stupid warnings
                if (lSunrise->hour >= 0 && lSunrise->hour < 24 && lSunrise->minute >= 0 && lSunrise->minute < 60 && lSunset->hour >= 0 && lSunset->hour < 24 && lSunset->minute >= 0 && lSunset->minute < 60)
                snprintf(sDiagnoseBuffer, 15, "R%02d:%02d S%02d:%02d", lSunrise->hour, lSunrise->minute, lSunset->hour, lSunset->minute);
                lResult = true;
            }
            break;
        }
        case 'o': {
            // calculate easter date
            snprintf(sDiagnoseBuffer, 15, "O%02d.%02d", sTimer.getEaster()->day, sTimer.getEaster()->month);
            lResult = true;
            break;
        }
        case 'm': {
            snprintf(sDiagnoseBuffer, 15, "%i", freeMemory());
            lResult = true;
            break;
        }
        default:
            lResult = false;
            break;
    }
    return lResult;
}

void Logic::processDiagnoseCommand(GroupObject &iKo)
{
    // this method is called as soon as iKo is changed
    // an external change is expected
    // because this iKo also is changed within this method,
    // the method is called again. This might result in
    // an endless loop. This is prevented by the isCalled pattern.
    static bool sIsCalled = false;
    if (!sIsCalled)
    {
        sIsCalled = true;
        // diagnose is interactive and reacts on commands
        initDiagnose(iKo);
        if (processDiagnoseCommand())
            outputDiagnose(iKo);
        sIsCalled = false;
    }
};

void Logic::outputDiagnose(GroupObject &iKo)
{
    sDiagnoseBuffer[15] = 0;
    iKo.value(sDiagnoseBuffer, getDPT(VAL_DPT_16));
    logInfo("Diagnose: %s\n", sDiagnoseBuffer);
}

void Logic::debug()
{
    logInfoP("Logik-LOG_ChannelsFirmware (in Firmware): %d\n", LOG_ChannelsFirmware);
    logInfoP("Logik-gNumChannels (in knxprod):  %d\n", mNumChannels);

    // logInfo("Aktuelle Zeit: %s", sTimer.getTimeAsc());
    sTimer.debug();
#ifdef ARDUINO_ARCH_RP2040
    logInfoP("Free Heap: %i\n", rp2040.getFreeHeap());
#endif
}

void Logic::setup()
{
    // check for hidden parameters
    logInfoP("Setting: Buzzer available: %d", ParamLOG_BuzzerInstalled);
    logInfoP("Setting: RGBLed available: %d", ParamLOG_LedInstalled);
    // setup channels, not possible in constructor, because knx is not configured there
    // get number of channels from knxprod
    mNumChannels = ParamLOG_NumChannels;
    if (LOG_ChannelsFirmware < mNumChannels)
    {
        char lErrorText[80];
        sprintf(lErrorText, "FATAL: Firmware compiled for %d channels, but knxprod needs %d channels!\n", LOG_ChannelsFirmware, mNumChannels);
        fatalError(FATAL_LOG_WRONG_CHANNEL_COUNT, lErrorText);
    }
    for (uint8_t lIndex = 0; lIndex < mNumChannels; lIndex++)
    {
        mChannel[lIndex] = new LogicChannel(lIndex);
    }
    // setup buzzer
#ifdef BUZZER_PIN
    pinMode(BUZZER_PIN, OUTPUT);
#endif
    //bool lTimezoneSign = ParamLOG_TimezoneSign;
    int8_t lTimezone = ParamLOG_Timezone;
    //lTimezone = lTimezone * (lTimezoneSign ? -1 : 1);
    bool lUseSummertime = (ParamLOG_SummertimeAll == VAL_STIM_FROM_INTERN);
    sTimer.setup(ParamLOG_Longitude, ParamLOG_Latitude, ParamLOG_Timezone, lUseSummertime, knx.paramInt(LOG_Neujahr)); //do not fetch just ParamLOG_Neujahr here, we need the whole bitfield
    // for TimerRestore we prepare all Timer channels
    for (uint8_t lIndex = 0; lIndex < mNumChannels; lIndex++)
    {
        LogicChannel *lChannel = mChannel[lIndex];
        lChannel->startTimerRestoreState();
    }
}
void Logic::loop()
{
    if(!openknx.afterStartupDelay())
        return;
    uint32_t lLoopTime = millis();
    processReadRequests();
    sTimer.loop(); // clock and timer async methods
    // we loop on all channels and execute pipeline
    
    uint8_t lChannelsProcessed = 0;
    // for (uint8_t lIndex = 0; lIndex < mNumChannels; lIndex++)
    while (lChannelsProcessed < mNumChannels && openknx.freeLoopTime())
    {
        LogicChannel *lChannel = mChannel[mChannelIterator++];
        if (sTimer.minuteChanged())
            lChannel->startTimerInput();
        lChannel->loop();
        lChannelsProcessed++;
        // the following operations are done only once after iteration of all channels
        if (mChannelIterator >= mNumChannels)
        {
            mChannelIterator = 0;
            // here we do actions which happen after all channels are iterated
            if (sTimer.minuteChanged())
            {
                sendHoliday();
                sTimer.clearMinuteChanged();
            }
            processTimerRestore();
        }
    }
    if (lChannelsProcessed < mNumChannels)
        logInfoP("did not process all channels during loop, just %i channels", lChannelsProcessed);
    if (millis()-lLoopTime > 1)
        logInfoP("LoopTime: %i", millis()-lLoopTime);
}

// start timer implementation
void Logic::processTimerRestore()
{
    static uint32_t sTimerRestoreDelay = 1;
    // if (!knx.configured())
    //     return;

    if (sTimerRestoreDelay == 0)
        return;
    if (sTimer.isTimerValid() == tmValid && delayCheck(sTimerRestoreDelay, 500))
    {
        sTimerRestoreDelay = millis();
        if (sTimerRestoreDelay == 0)
            sTimerRestoreDelay = 1; // prevent set to 0 in case of timer overflow
        if (sTimerRestore.getDayIteration() < 365)
        {
            if (sTimerRestore.getDayIteration() == 0)
            {
                // initialize RestoreTimer
                sTimerRestore.setup(sTimer);
            }
            else
            {
                sTimerRestore.decreaseDay();
            }
            // loopSubmodules();
        }
        else
        {
            // stop timer restore processing in logic...
            sTimerRestoreDelay = 0;
            // ... and in each channel
            for (uint8_t lIndex = 0; lIndex < mNumChannels; lIndex++)
            {
                LogicChannel *lChannel = mChannel[lIndex];
                lChannel->stopTimerRestoreState();
            }
        }
    }
}

// send holiday information on bus
void Logic::sendHoliday()
{
    if (sTimer.holidayChanged())
    {
        // write the newly calculated holiday information into KO (can be read externally)
        
        KoLOG_Holiday1.valueNoSend(sTimer.holidayToday(), getDPT(VAL_DPT_5));
        KoLOG_Holiday2.valueNoSend(sTimer.holidayTomorrow(), getDPT(VAL_DPT_5));
        sTimer.clearHolidayChanged();
        if (ParamLOG_HolidaySend)
        {
            // and send it, if requested by application setting
            KoLOG_Holiday1.objectWritten();
            KoLOG_Holiday2.objectWritten();
        }
    }
}
