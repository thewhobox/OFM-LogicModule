#include "LogicChannel.h"
#include "Helper.h"
#include "Logic.h"
#include "LogicFunction.h"
#include "PCA9632.h"

#ifndef abs
#define abs(x) ((x) > 0 ? (x) : -(x))
#endif

Logic *LogicChannel::sLogic = nullptr;
Timer &LogicChannel::sTimer = Timer::instance();
TimerRestore &LogicChannel::sTimerRestore = TimerRestore::instance(); // singleton
#if LOGIC_TRACE
char LogicChannel::sFilter[30] = "";
char LogicChannel::sTimeOutputBuffer[10] = "";
#endif

/******************************
 * Constructors
 * ***************************/
LogicChannel::LogicChannel(uint8_t iChannelNumber)
{
    _channelIndex = iChannelNumber;
    // initialize most important runtime fields
    pCurrentPipeline = 0;
    pValidActiveIO = 0;
    pTriggerIO = 0;
    pCurrentIn = 0;
    pCurrentOut = BIT_OUTPUT_INITIAL; // tri-state output, at the beginning we are undefined
    sprintf(pLogPrefix, "C%02i", iChannelNumber + 1);
}

LogicChannel::~LogicChannel()
{
}

/******************************
 * Debug helper
 * ***************************/

const std::string LogicChannel::logPrefix() 
{
    return std::string(pLogPrefix);
}

#if LOGIC_TRACE
char* LogicChannel::logTimeBase(uint16_t iParamIndex) {
    uint16_t lTime = getWordParam(iParamIndex);
    switch (lTime & 0xC000)
    {
    case 0x0000:
        /* seconds */
        sprintf(sTimeOutputBuffer, "%5i s", lTime & 0x3FFF);
        break;
    case 0x4000:
        /* minutes */
        sprintf(sTimeOutputBuffer, "%5i m", lTime & 0x3FFF);
        break;
    case 0x8000:
        /* hours */
        sprintf(sTimeOutputBuffer, "%5i h", lTime & 0x3FFF);
        break;
    case 0xC000:
        /* 1/10 s*/
        sprintf(sTimeOutputBuffer, "%5.1f s", (lTime & 0x3FFF) / 10.0);
        break;
    default:
        sprintf(sTimeOutputBuffer, "(no time)");
        break;
    }
    return sTimeOutputBuffer;
}

// channel debug output
int LogicChannel::logChannel(const char *iFormat, ...)
{
    // char lBuffer[256];
    // uint8_t lBufferPos = channelIndex() * 2;
    // memset(lBuffer, ' ', lBufferPos + 1);
    // lBuffer[lBufferPos] = 'C';
    // sprintf(lBuffer + lBufferPos + 1, "%02i-", channelIndex() + 1);
    // va_list lArgs;
    // va_start(lArgs, iFormat);
    // int lResult = vsnprintf(lBuffer + lBufferPos + 4, 252 - lBufferPos, iFormat, lArgs);
    // va_end(lArgs);
    // logInfoP(lBuffer);
    // return lResult;
    // char lBuffer[255];
    // sprintf(lBuffer, "C%02i-", channelIndex() + 1);
    logIndent((channelIndex() % 20) + 1);
    va_list lArgs;
    va_start(lArgs, iFormat);
    logTraceP(iFormat, lArgs);
    va_end(lArgs);
    logIndent(0);
    return 1;
}

bool LogicChannel::debugFilter()
{
    char lChannel[3];
    bool lReturn = true;
    if (sFilter[0])
    {
        sprintf(lChannel, "%02i", channelIndex() + 1);
        lReturn = (sFilter[0] == lChannel[0]) && (sFilter[1] == lChannel[1]);
        for (uint8_t i = 2; !lReturn && sFilter[i] && i < 30; i = i + 2)
        {
            lReturn = (sFilter[i] == lChannel[0]) && (sFilter[i + 1] == lChannel[1]);
        }
    }
    return lReturn;
}
#endif

/******************************
 * Parameter helper
 * ***************************/
uint32_t LogicChannel::calcParamIndex(uint16_t iParamIndex)
{
    uint32_t lResult = iParamIndex + channelIndex() * LOG_ParamBlockSize + LOG_ParamBlockOffset;
    return lResult;
}

uint8_t LogicChannel::getByteParam(uint16_t iParamIndex)
{
    uint8_t lValue = knx.paramByte(calcParamIndex(iParamIndex));
    return lValue;
}

int8_t LogicChannel::getSByteParam(uint16_t iParamIndex)
{
    uint8_t *lRef = knx.paramData(calcParamIndex(iParamIndex));
    return lRef[0];
}

uint16_t LogicChannel::getWordParam(uint16_t iParamIndex)
{
    return knx.paramWord(calcParamIndex(iParamIndex));
}

int16_t LogicChannel::getSWordParam(uint16_t iParamIndex)
{
    uint8_t *lRef = knx.paramData(calcParamIndex(iParamIndex));
    return lRef[0] * 256 + lRef[1];
}

uint32_t LogicChannel::getIntParam(uint16_t iParamIndex)
{
    return knx.paramInt(calcParamIndex(iParamIndex));
}

int32_t LogicChannel::getSIntParam(uint16_t iParamIndex)
{
    return knx.paramInt(calcParamIndex(iParamIndex));
}

float LogicChannel::getFloatParam(uint16_t iParamIndex)
{
    uint16_t lIndex = calcParamIndex(iParamIndex);
    float lFloat = knx.paramFloat(lIndex, Float_Enc_IEEE754Single);
    return lFloat;
}

uint8_t *LogicChannel::getStringParam(uint16_t iParamIndex)
{
    uint16_t lIndex = calcParamIndex(iParamIndex);
    return knx.paramData(lIndex);
}

uint32_t LogicChannel::getTimeDelayParam(uint16_t iParamIndex, bool iAsSeconds /* = false */)
{
    return getDelayPattern(calcParamIndex(iParamIndex), iAsSeconds);
}

/*******************************
 * ComObject helper
 * ****************************/
// static
uint16_t LogicChannel::calcKoNumber(uint8_t iIOIndex, uint8_t iChannelId)
{
    // do not use iIOIndex = 0
    uint16_t lKoNumber = LOG_KoOffset + iChannelId * LOG_KoBlockSize;
    switch (iIOIndex)
    {
        case IO_Input1:
            lKoNumber += LOG_KoKOfE1;
            break;
        case IO_Input2:
            lKoNumber += LOG_KoKOfE2;
            break;
        case IO_Output:
            lKoNumber += LOG_KoKOfO;
            break;
        default:
            lKoNumber = iChannelId;
            break;
    }
    return lKoNumber;
}

// static
GroupObject *LogicChannel::getKoForChannel(uint8_t iIOIndex, uint8_t iChannelId)
{
    return &knx.getGroupObject(calcKoNumber(iIOIndex, iChannelId));
}

uint16_t LogicChannel::calcKoNumber(uint8_t iIOIndex)
{
    return LogicChannel::calcKoNumber(iIOIndex, channelIndex());
}

GroupObject *LogicChannel::getKo(uint8_t iIOIndex)
{
    // new behaviour since 4.0: We support also external KO for input
    GroupObject *lKo = nullptr;
    uint16_t lExternalAccess = 0;
    bool lUseExternal = false;
    if (iIOIndex == IO_Input1)
    {
        lExternalAccess = ParamLOG_fE1OtherKO; // getWordParam(LOG_fE1OtherKO);
        lUseExternal = ParamLOG_fE1UseOtherKO;
    }
    else if (iIOIndex == IO_Input2)
    {
        lExternalAccess = ParamLOG_fE2OtherKO; // getWordParam(LOG_fE2OtherKO);
        lUseExternal = ParamLOG_fE2UseOtherKO;
    }
    if (lUseExternal)
    {
        uint16_t lKoNumber = lExternalAccess & 0x3FFF; // mask ist for both inputs identical
        lKo = &knx.getGroupObject(lKoNumber);
    }
    if (lKo == nullptr)
        lKo = LogicChannel::getKoForChannel(iIOIndex, channelIndex());
    return lKo;
}

Dpt &LogicChannel::getKoDPT(uint8_t iIOIndex)
{
    uint8_t lDpt;
    switch (iIOIndex)
    {
        case IO_Input1:
            lDpt = ParamLOG_fE1Dpt;
            break;
        case IO_Input2:
            lDpt = ParamLOG_fE2Dpt;
            break;
        case IO_Output:
            lDpt = ParamLOG_fODpt;
            break;
        default:
            lDpt = 0;
            break;
    }
    return getDPT(lDpt);
}

// write value to bus
void LogicChannel::knxWriteBool(uint8_t iIOIndex, bool iValue)
{
#if LOGIC_TRACE
    logChannel("knxWrite KO %d bool value %d", calcKoNumber(iIOIndex), iValue);
#endif
    getKo(iIOIndex)->value(iValue, getKoDPT(iIOIndex));
}

void LogicChannel::knxWriteInt(uint8_t iIOIndex, int32_t iValue)
{
#if LOGIC_TRACE
    logChannel("knxWrite KO %d int value %li", calcKoNumber(iIOIndex), iValue);
#endif
    getKo(iIOIndex)->value((int32_t)iValue, getKoDPT(iIOIndex));
}

void LogicChannel::knxWriteRawInt(uint8_t iIOIndex, int32_t iValue)
{
#if LOGIC_TRACE
    logChannel("knxWrite KO %d int value %li", calcKoNumber(iIOIndex), iValue);
#endif
    GroupObject *lKo = getKo(iIOIndex);
    uint8_t *lValueRef = lKo->valueRef();
    *lValueRef = iValue;
    lKo->objectWritten();
}

void LogicChannel::knxWriteFloat(uint8_t iIOIndex, float iValue)
{
#if LOGIC_TRACE
    logChannel("knxWrite KO %d float value %f", calcKoNumber(iIOIndex), iValue);
#endif
    getKo(iIOIndex)->value(iValue, getKoDPT(iIOIndex));
}

void LogicChannel::knxWriteString(uint8_t iIOIndex, const char *iValue)
{
#if LOGIC_TRACE
    logChannel("knxWrite KO %d string value %s", calcKoNumber(iIOIndex), iValue);
#endif
    getKo(iIOIndex)->value(iValue, getKoDPT(iIOIndex));
}

// send read request on bus
void LogicChannel::knxRead(uint8_t iIOIndex)
{
#if LOGIC_TRACE
    logChannel("knxReadRequest send from KO %d", calcKoNumber(iIOIndex));
#endif
    getKo(iIOIndex)->requestObjectRead();
}

// send reset device to bus
void LogicChannel::knxResetDevice(uint16_t iParamIndex)
{
    uint16_t lAddress = getWordParam(iParamIndex);
    uint16_t lLocalAddress = knx.individualAddress();
#if LOGIC_TRACE
    uint8_t lHigh = lAddress >> 8;
    logChannel("knxResetDevice with PA %d.%d.%d", lHigh >> 4, lHigh & 0xF, lAddress & 0xFF);
#endif
    if (lAddress == lLocalAddress)
    {
        // here we have to do a local restart (restart own device)
        if (knx.beforeRestartCallback() != 0)
            knx.beforeRestartCallback()();
        // Flush the Flash before resetting
        knx.writeMemory();
        knx.platform().restart();
    }
    else
        knx.restart(lAddress);
}

// turn on/off RGBLed
void LogicChannel::setRGBColor(uint16_t iParamIndex)
{
#ifdef I2C_RGBLED_DEVICE_ADDRESS
    if ((getByteParam(LOG_fAlarm) & LOG_fAlarmMask) || !knx.getGroupObject(LOG_KoLedLock).value(getDPT(VAL_DPT_1)))
    {
        uint32_t lRGBColor = getIntParam(iParamIndex);
        uint8_t lRed = lRGBColor >> 24;
        uint8_t lGreen = lRGBColor >> 16;
        uint8_t lBlue = lRGBColor >> 8;
        // we have to map colors to correct pins
        switch (ParamLOG_LedMapping)
        {
            case 2: // R, B, G
                PCA9632_SetColor(lRed, lBlue, lGreen);
                break;
            case 3: // G, R, B
                PCA9632_SetColor(lGreen, lRed, lBlue);
                break;
            case 4: // G, B, R
                PCA9632_SetColor(lGreen, lBlue, lRed);
                break;
            case 5: // B, G, R
                PCA9632_SetColor(lBlue, lGreen, lRed);
                break;
            case 6: // B, R, G
                PCA9632_SetColor(lBlue, lRed, lGreen);
                break;

            default: // R, G, B
                PCA9632_SetColor(lRed, lGreen, lBlue);
                break;
        }
    }
    else
    {
        // in case of lock we turn off led
        PCA9632_SetColor(0, 0, 0);
    }
#endif
}

// turn on/off Buzzer
void LogicChannel::setBuzzer(uint16_t iParamIndex)
{
#ifdef BUZZER_PIN
    // check for global lock and alarm
    if (ParamLOG_fAlarm || !KoLOG_BuzzerLock.value(getDPT(VAL_DPT_1)))
    {
        switch (getByteParam(iParamIndex))
        {
            case VAL_Buzzer_Off:
                noTone(BUZZER_PIN);
                break;
            case VAL_Buzzer_Loud:
                tone(BUZZER_PIN, ParamLOG_BuzzerLoud);
                break;
            case VAL_Buzzer_Silent:
                tone(BUZZER_PIN, ParamLOG_BuzzerSilent);
                break;
            case VAL_Buzzer_Normal:
                tone(BUZZER_PIN, ParamLOG_BuzzerNormal);
                break;
            default:
                break;
        }
    }
    else
    {
        // in case of lock we turn off buzzer
        noTone(BUZZER_PIN);
    }
#endif
}

/********************************
 * Logic helper functions
 * *****************************/

// we get an dpt dependant parameter value for different
// input evaluation
LogicValue LogicChannel::getParamForDelta(uint8_t iDpt, uint16_t iParamIndex)
{
    if (iDpt == VAL_DPT_9 || iDpt == VAL_DPT_14)
    {
        LogicValue lValue = getFloatParam(iParamIndex);
        return lValue;
    }
    else
    {
        LogicValue lValue = (int32_t)getIntParam(iParamIndex);
        return lValue;
    }
}

// we get here numeric params by their DPT
// DPT1,2,5,6,7,8,17,232 => straight forward int values
// DPT2,17 => straight forward byte values
// DPT5001 => scale down to [0..100]
// DPT9 => transport as float
LogicValue LogicChannel::getParamByDpt(uint8_t iDpt, uint16_t iParamIndex)
{
    switch (iDpt)
    {
        case VAL_DPT_1: {
            LogicValue lValue = getByteParam(iParamIndex) != 0;
            return lValue;
        }
        case VAL_DPT_2:
        case VAL_DPT_5:
        case VAL_DPT_17:
        case VAL_DPT_5001: {
            LogicValue lValue = getByteParam(iParamIndex);
            return lValue;
        }
        case VAL_DPT_6: {
            LogicValue lValue = getSByteParam(iParamIndex);
            return lValue;
        }
        case VAL_DPT_7: {
            LogicValue lValue = getWordParam(iParamIndex);
            return lValue;
        }
        case VAL_DPT_8: {
            LogicValue lValue = getSWordParam(iParamIndex);
            return lValue;
        }
        case VAL_DPT_232: {
            LogicValue lValue = getIntParam(iParamIndex);
            return lValue;
        }
        case VAL_DPT_9: 
        case VAL_DPT_14: {
            LogicValue lValue = getFloatParam(iParamIndex);
            return lValue;
        }
        case VAL_DPT_12: {
            LogicValue lValue = getIntParam(iParamIndex);
            return lValue;
        }
        case VAL_DPT_13: {
            LogicValue lValue = getSIntParam(iParamIndex);
            return lValue;
        }
        default: {
            LogicValue lValue = getIntParam(iParamIndex);
            return lValue;
        }
    }
}

// on input level, we have just numeric values, so all DPT are converted to int:
// DPT1,2,5,6,7,8,17,232 => straight forward
// DPT5001 => scale down to [0..100]
// DPT9 => transport as float
LogicValue LogicChannel::getInputValue(uint8_t iIOIndex, uint8_t *eDpt)
{
    // check for constant
    uint16_t lParamIndex = (iIOIndex == 1) ? LOG_fE1Convert : LOG_fE2Convert;
    uint8_t lConvert = (getByteParam(lParamIndex) & LOG_fE1Convert_Mask) >> LOG_fE1Convert_Shift;
    lParamIndex = (iIOIndex == 1) ? LOG_fE1Dpt : LOG_fE2Dpt;
    *eDpt = getByteParam(lParamIndex);
    if (lConvert == VAL_InputConvert_Constant)
    {
        // input value is a constant stored in param memory
        uint16_t lParamIndex = (iIOIndex == 1) ? LOG_fE1LowDelta : LOG_fE2LowDelta;
        LogicValue lValue = getParamByDpt(*eDpt, lParamIndex);
        return lValue;
    }
    else
    {
        GroupObject *lKo = getKo(iIOIndex);
        // based on dpt, we read the correct c type.
        switch (*eDpt)
        {
            case VAL_DPT_2: {
                LogicValue lValue = lKo->valueRef()[0];
                return lValue;
            }
            case VAL_DPT_6: {
                LogicValue lValue = (int8_t)lKo->value(getDPT(VAL_DPT_6));
                return lValue;
            }
            case VAL_DPT_8: {
                LogicValue lValue = (int16_t)lKo->value(getDPT(VAL_DPT_8));
                return lValue;
            }
            case VAL_DPT_12: {
                LogicValue lValue = (uint32_t)lKo->value(getDPT(VAL_DPT_12));
                return lValue;
            }
            // case VAL_DPT_7:
            //     LogicValue lValue = lKo->valueRef()[0] + 256 * lKo->valueRef()[1];
            //     break;
            // case VAL_DPT_232:
            //     lValue =
            //         lKo->valueRef()[0] + 256 * lKo->valueRef()[1] + 65536 * lKo->valueRef()[2];
            //     break;
            case VAL_DPT_9: 
            case VAL_DPT_14: {
                LogicValue lValue = (float)lKo->value(getDPT(*eDpt));
                return lValue;
            } // case VAL_DPT_17:
            default: {
                LogicValue lValue = (int32_t)lKo->value(getDPT(*eDpt));
                return lValue;
            }
        }
    }
}

void LogicChannel::writeConstantValue(uint16_t iParamIndex)
{
    uint8_t lDpt = getByteParam(LOG_fODpt);
    switch (lDpt)
    {
        uint8_t lValueByte;
        case VAL_DPT_1:
            bool lValueBool;
            lValueBool = getByteParam(iParamIndex) != 0;
            knxWriteBool(IO_Output, lValueBool);
            break;
        case VAL_DPT_2:
            lValueByte = getByteParam(iParamIndex);
            knxWriteRawInt(IO_Output, lValueByte);
            break;
        case VAL_DPT_5:
        case VAL_DPT_5001: // correct value is calculated by dpt handling
            lValueByte = getByteParam(iParamIndex);
            knxWriteInt(IO_Output, lValueByte);
            break;
        case VAL_DPT_17:
            lValueByte = getByteParam(iParamIndex) - 1;
            knxWriteInt(IO_Output, lValueByte);
            break;
        case VAL_DPT_6:
            int8_t lValueShort;
            lValueShort = getSByteParam(iParamIndex);
            knxWriteRawInt(IO_Output, lValueShort);
            break;
        case VAL_DPT_7:
            uint16_t lValueUWord;
            lValueUWord = getWordParam(iParamIndex);
            knxWriteInt(IO_Output, lValueUWord);
            break;
        case VAL_DPT_8:
            int16_t lValueSWord;
            lValueSWord = getSWordParam(iParamIndex);
            knxWriteInt(IO_Output, lValueSWord);
            break;
        case VAL_DPT_9:
        case VAL_DPT_14:
            float lValueFloat;
            lValueFloat = getFloatParam(iParamIndex);
            knxWriteFloat(IO_Output, lValueFloat);
            break;
        case VAL_DPT_12:
            uint32_t lValueInt;
            lValueInt = getIntParam(iParamIndex);
            knxWriteInt(IO_Output, lValueInt);
            break;
        case VAL_DPT_13:
            int32_t lValueSInt;
            lValueSInt = getSIntParam(iParamIndex);
            knxWriteInt(IO_Output, lValueSInt);
            break;
        case VAL_DPT_16:
            uint8_t *lValueStr;
            lValueStr = getStringParam(iParamIndex);
            knxWriteString(IO_Output, (char *)lValueStr);
            break;
        case VAL_DPT_232:
            int32_t lValueRGB;
            lValueRGB = getIntParam(iParamIndex) >> 8;
            knxWriteInt(IO_Output, lValueRGB);
            break;
        default:
            break;
    }
}

void LogicChannel::writeParameterValue(uint8_t iIOIndex)
{
    uint8_t lInputDpt;
    LogicValue lValue = getInputValue(iIOIndex, &lInputDpt);
    writeValue(lValue, lInputDpt);
}

void LogicChannel::writeFunctionValue(uint16_t iParamIndex)
{
    uint8_t lFunction = getByteParam(iParamIndex);
    uint8_t lDptE1;
    uint8_t lDptE2;
    LogicValue lE1 = getInputValue(BIT_EXT_INPUT_1, &lDptE1);
    LogicValue lE2 = getInputValue(BIT_EXT_INPUT_2, &lDptE2);
    uint8_t lDptOut = getByteParam(LOG_fODpt);
    LogicValue lValue = LogicFunction::callFunction(lFunction, lDptE1, lE1, lDptE2, lE2, &lDptOut);
    writeValue(lValue, lDptOut);
}

void LogicChannel::writeValue(LogicValue iValue, uint8_t iDpt)
{
    uint8_t lDpt = getByteParam(LOG_fODpt);
    uint8_t lValueByte;
    switch (lDpt)
    {
        case VAL_DPT_1:
            knxWriteBool(IO_Output, (bool)iValue);
            break;
        case VAL_DPT_2:
            lValueByte = iValue;
            lValueByte &= 3;
            knxWriteRawInt(IO_Output, lValueByte);
            break;
        case VAL_DPT_5:
        case VAL_DPT_5001:
            knxWriteInt(IO_Output, (uint8_t)iValue);
            break;
        case VAL_DPT_6:
            knxWriteInt(IO_Output, (int8_t)iValue);
            break;
            // lValueByte = lValue;
            // // DPT5 means, that input value range is [0..100], output value range is
            // // [0..255]
            // lValueByte = (lValueByte / 100.0) * 255.0;
            // knxWrite(0, lValueByte);
            // break;
        case VAL_DPT_7:
            // iValue = (uint16_t)abs((int16_t)iValue);
            knxWriteInt(IO_Output, (uint16_t)iValue);
            break;
        case VAL_DPT_8:
            knxWriteInt(IO_Output, (int16_t)iValue);
            break;
        case VAL_DPT_9:
        case VAL_DPT_14:
            knxWriteFloat(IO_Output, (float)iValue);
            break;
        case VAL_DPT_16:
            knxWriteString(IO_Output, ((const char *)iValue));
            break;
        case VAL_DPT_17:
            lValueByte = abs((int8_t)iValue);
            lValueByte &= 0x3F;
            knxWriteInt(IO_Output, lValueByte);
            break;
        case VAL_DPT_12:
        case VAL_DPT_13:
        case VAL_DPT_232:
            knxWriteInt(IO_Output, iValue);
            break;
        default:
            break;
    }
}

/********************************
 * Logic functions
 *******************************/
bool LogicChannel::isInputActive(uint8_t iIOIndex)
{
    uint8_t lIsActive = getByteParam((iIOIndex == IO_Input1) ? LOG_fE1 : LOG_fE2) & BIT_INPUT_MASK;
    if (lIsActive == 0)
    {
        // input might be also activated by a delta input converter, means from the other input
        lIsActive = (getByteParam((iIOIndex == IO_Input2) ? LOG_fE1Convert : LOG_fE2Convert) >> LOG_fE1Convert_Shift) & 1;
    }
    return (lIsActive > 0);
}

bool LogicChannel::isInputValid(uint8_t iIOIndex)
{
    return (pValidActiveIO & iIOIndex);
}

// channel startup delay
void LogicChannel::startStartup()
{
    pOnDelay = millis();
    pCurrentPipeline |= PIP_STARTUP;
#if LOGIC_TRACE
    if (debugFilter())
        logChannel("startStartup: wait for %s", logTimeBase(LOG_fChannelDelayTime));
#endif
}

void LogicChannel::processStartup()
{
    if (delayCheck(pOnDelay, ParamLOG_fChannelDelayTimeMS))
    {
        // we waited enough, remove pipeline marker
#if LOGIC_TRACE
        if (debugFilter())
            logChannel("endedStartup: waited %i ms", millis() - pOnDelay);
#endif
        pCurrentPipeline &= ~PIP_STARTUP;
        pCurrentPipeline |= PIP_RUNNING;
        pOnDelay = 0;
    }
}

void LogicChannel::processInput(uint8_t iIOIndex)
{
    if (iIOIndex == 0 || iIOIndex == 3)
        return;
    uint16_t lParamBase = (iIOIndex == 1) ? LOG_fE1 : LOG_fE2;
    // we have now an event for an input, first we check, if this input is active
    uint8_t lActive = getByteParam(lParamBase) & BIT_INPUT_MASK;
    if (lActive > 0)
    {
        // this input is we start convert for this input
        startConvert(iIOIndex);
        // we also add that this input was used and is now valid
        pValidActiveIO |= iIOIndex;
    }
    // this input might also be used for delta conversion in the other input
    uint16_t lOtherParamBase = (iIOIndex == 2) ? LOG_fE1 : LOG_fE2;
    uint8_t lConverter = getByteParam(lOtherParamBase) >> LOG_fE1Convert_Shift;
    if (lConverter & 1)
    {
        // delta conversion, we start convert for the other input
        startConvert(3 - iIOIndex);
        // we also add that this input was used and is now valid
        pValidActiveIO |= iIOIndex;
    }
}

// we send an ReadRequest if reading from input 1 should be repeated
void LogicChannel::processRepeatInput1()
{
    uint32_t lRepeatTime = ParamLOG_fE1RepeatTimeMS;

    if (delayCheck(pInputProcessing.repeatInput1Delay, lRepeatTime))
    {
        knxRead(IO_Input1);
        pInputProcessing.repeatInput1Delay = millis();
        if (lRepeatTime == 0)
            pCurrentPipeline &= ~PIP_REPEAT_INPUT1;
    }
}

// we send an ReadRequest if reading from input 1 should be repeated
void LogicChannel::processRepeatInput2()
{
    uint32_t lRepeatTime = ParamLOG_fE2RepeatTimeMS;

    if (delayCheck(pInputProcessing.repeatInput2Delay, lRepeatTime))
    {
        knxRead(IO_Input2);
        pInputProcessing.repeatInput2Delay = millis();
        if (lRepeatTime == 0)
            pCurrentPipeline &= ~PIP_REPEAT_INPUT2;
    }
}

void LogicChannel::stopRepeatInput(uint8_t iIOIndex)
{
    // repeated read on an input KO is stopped in following cases:
    // 1. There is one single read on startup and this read was executed (is solved in processRepeatInputX())
    // 2. There is one single read on startup, the read was not yet executed (channel is not running) but
    //    nevertheless the telegram was received (i.E. through an other read of a running channel)
    // 3. There is a continuous read with condition "until telegram received"
    uint16_t lRepeatInputBit;
    uint32_t lRepeatTime;
    bool lJustOneTelegram;

    switch (iIOIndex)
    {
        case IO_Input1:
            lRepeatInputBit = PIP_REPEAT_INPUT1;
            lRepeatTime = ParamLOG_fE1RepeatTimeMS;
            lJustOneTelegram = ParamLOG_fE1DefaultRepeat; 
            break;
        case IO_Input2:
            lRepeatInputBit = PIP_REPEAT_INPUT2;
            lRepeatTime = ParamLOG_fE2RepeatTimeMS;
            lJustOneTelegram = ParamLOG_fE2DefaultRepeat;
            break;
        default:
            return;
            break;
    }
    // if (!lJustOneTelegram || (pCurrentPipeline & PIP_RUNNING))
    //     return;
    if (pCurrentPipeline & lRepeatInputBit)
    {
        if (lRepeatTime == 0 || lJustOneTelegram)
            pCurrentPipeline &= ~lRepeatInputBit;
    }
}

void LogicChannel::startConvert(uint8_t iIOIndex)
{
    if (iIOIndex == 1 || iIOIndex == 2)
    {
        pCurrentPipeline |= (iIOIndex == 1) ? PIP_CONVERT_INPUT1 : PIP_CONVERT_INPUT2;
        stopRepeatInput(iIOIndex);
    }
}

bool LogicChannel::checkConvertValues(uint16_t iParamValues, uint8_t iDpt, int32_t iValue)
{
    bool lValueOut = false;
    uint8_t lValueSize = 1;
    uint8_t lNumValues = 1;
    uint8_t lValid = getByteParam(iParamValues + 7); // validity array

    switch (iDpt)
    {
        case VAL_DPT_2:
            lNumValues = 4;
            lValid = 0xFF;
            break;
        case VAL_DPT_5:
        case VAL_DPT_5001:
        case VAL_DPT_6:
            lNumValues = 7;
            break;
        case VAL_DPT_7:
        case VAL_DPT_8:
            lNumValues = 3;
            lValueSize = 2;
            break;
        case VAL_DPT_17:
            lNumValues = 8;
            lValid = 0xFF;
            break;
        default:
            break;
    }
    for (uint8_t lIndex = 0, lShift = 0x80; lIndex < lNumValues && !lValueOut; lIndex++, lShift >>= 1)
    {
        if (lValid & lShift)
        {
            // we check just valid values
            LogicValue lValue = (int32_t)getParamByDpt(iDpt, iParamValues + lIndex * lValueSize);
            lValueOut = (iValue == (int32_t)lValue);
        }
    }
    return lValueOut;
}

void LogicChannel::processConvertInput(uint8_t iIOIndex)
{
    uint16_t lParamBase = (iIOIndex == 1) ? LOG_fE1 : LOG_fE2;
    uint16_t lParamLow = (iIOIndex == 1) ? LOG_fE1LowDelta : LOG_fE2LowDelta;
    uint8_t lConvert = ParamLOG_fE1Convert;
    bool lValueOut = 0;
    // get input value
    uint8_t lDpt;
    LogicValue lValue1In = getInputValue(iIOIndex, &lDpt);
    LogicValue lValue2In = 0L;
    LogicValue lDiff = 0L;
    uint8_t lDptValue2 = 0;
    // uint8_t lDptResult = 0;
    if ((lConvert < VAL_InputConvert_Values) && (lConvert & 1))
    {
        // in case of delta conversion get the other input value
        lValue2In = getInputValue(3 - iIOIndex, &lDptValue2);
    }
    else if (lConvert == VAL_InputConvert_Constant)
    {
        pValidActiveIO |= iIOIndex;
    }
    uint8_t lUpperBound = 0;
    bool lDoDefault = false;
    switch (lDpt)
    {
        case VAL_DPT_1:
            lValueOut = lValue1In;
#if LOGIC_TRACE
            if (debugFilter())
            {
                logChannel("processConvertInput E%i DPT1: In=Out=%i", iIOIndex, lValueOut);
            }
#endif
            break;
        case VAL_DPT_17:
            // there might be 8 possible scenes to check
            lUpperBound = 8; // we start with 2
            lValue1In = (uint8_t)((uint8_t)lValue1In + 1);
        case VAL_DPT_2:
            // there might be 4 possible "Zwangsführung" values to check
            if (lUpperBound == 0)
                lUpperBound = 4; // we start with 2
            // scenes or Zwangsführung have no intervals, but multiple single values
            for (size_t lScene = 0; lScene < lUpperBound && lValueOut == 0; lScene++)
            {
                uint8_t lValue = getByteParam(lParamLow + lScene);
                lValueOut = ((uint8_t)lValue1In == lValue);
            }
            break;
#if LOGIC_TRACE
            if (debugFilter())
            {
                if (lDpt == VAL_DPT_17)
                {
                    logChannel("processConvertInput E%i DPT17: In=%i, Out=%i", iIOIndex, lValue1In, lValueOut);
                }
                else
                {
                    logChannel("processConvertInput E%i DPT2: In=%i, Out=%i", iIOIndex, lValue1In, lValueOut);
                }
            }
#endif
        default:
            lDoDefault = true;
            break;
    }
    if (lDoDefault)
    {
        // for all remaining DPT we determine the input value by an converter module
        switch (lConvert)
        {
            case VAL_InputConvert_Interval:
                lValueOut = (lValue1In >= getParamByDpt(lDpt, lParamLow + 0)) && (lValue1In <= getParamByDpt(lDpt, lParamLow + 4));
                // lValueOut = uValueGreaterThanOrEquals(lValue1In, getParamByDpt(lDpt, lParamLow + 0), lDpt, lDpt) &&
                //             uValueLessThanOrEquals(lValue1In, getParamByDpt(lDpt, lParamLow + 4), lDpt, lDpt);
#if LOGIC_TRACE
                if (debugFilter())
                {
                    logChannel("processConvertInput E%i Interval: In=%i, Out=%i", iIOIndex, lValue1In, lValueOut);
                }
#endif
                break;
            case VAL_InputConvert_DeltaInterval:
                lDiff = lValue1In - lValue2In;
                // lDiff = uValueSubtract(lValue1In, lValue2In, lDpt, lDptValue2);
                // lDptResult = (lDpt == VAL_DPT_9 || lDptValue2 == VAL_DPT_9) ? VAL_DPT_9 : lDpt;
                if (lDpt != VAL_DPT_9 && lDpt != VAL_DPT_14) lDpt = VAL_DPT_13;
                lValueOut = (lDiff >= getParamByDpt(lDpt, lParamLow + 0)) && (lDiff <= getParamByDpt(lDpt, lParamLow + 4));
                // lValueOut = uValueGreaterThanOrEquals(lDiff, getParamByDpt(lDpt, lParamLow + 0), lDptResult, lDpt) &&
                //             uValueLessThanOrEquals(lDiff, getParamByDpt(lDpt, lParamLow + 4), lDptResult, lDpt);
#if LOGIC_TRACE
                if (debugFilter())
                {
                    logChannel("processConvertInput E%i DeltaInterval: In1=%i, In2=%i, Delta=%i, Out=%i", iIOIndex, lValue1In, lValue2In, lValue1In - lValue2In, lValueOut);
                }
#endif
                break;
            case VAL_InputConvert_Hysterese:
                lValueOut = pCurrentIn & iIOIndex; // retrieve old result, will be send if current value is in Hysterese interval
                if (lValue1In <= getParamByDpt(lDpt, lParamLow + 0))
                    lValueOut = false;
                if (lValue1In >= getParamByDpt(lDpt, lParamLow + 4))
                    lValueOut = true;
                    // if (uValueLessThanOrEquals(lValue1In, getParamByDpt(lDpt, lParamLow + 0), lDpt, lDpt))
                    //     lValueOut = false;
                    // if (uValueGreaterThanOrEquals(lValue1In, getParamByDpt(lDpt, lParamLow + 4), lDpt, lDpt))
                    //     lValueOut = true;
#if LOGIC_TRACE
                if (debugFilter())
                {
                    logChannel("processConvertInput E%i Hysterese: In=%i, Out=%i", iIOIndex, lValue1In, lValueOut);
                }
#endif
                break;
            case VAL_InputConvert_DeltaHysterese:
                lValueOut = pCurrentIn & iIOIndex; // retrieve old result, will be send if current value is in Hysterese interval
                lDiff = lValue1In - lValue2In;
                // lDiff = uValueSubtract(lValue1In, lValue2In, lDpt, lDptValue2);
                // lDptResult = (lDpt == VAL_DPT_9 || lDptValue2 == VAL_DPT_9) ? VAL_DPT_9 : lDpt;
                if (lDpt != VAL_DPT_9 && lDpt != VAL_DPT_14) lDpt = VAL_DPT_13;
                if (lValue1In <= getParamByDpt(lDpt, lParamLow + 0))
                    lValueOut = false;
                if (lValue1In >= getParamByDpt(lDpt, lParamLow + 4))
                    lValueOut = true;
                    // if (uValueLessThanOrEquals(lDiff, getParamByDpt(lDpt, lParamLow + 0), lDptResult, lDpt))
                    //     lValueOut = false;
                    // if (uValueGreaterThanOrEquals(lDiff, getParamByDpt(lDpt, lParamLow + 4), lDptResult, lDpt))
                    //     lValueOut = true;
#if LOGIC_TRACE
                if (debugFilter())
                {
                    logChannel("processConvertInput E%i DeltaHysterese: In1=%i, In2=%i, Delta=%i, Out=%i", iIOIndex, lValue1In, lValue2In, lValue1In - lValue2In, lValueOut);
                }
#endif
                break;
            case VAL_InputConvert_Values:
                lValueOut = checkConvertValues(lParamLow, lDpt, lValue1In);
#if LOGIC_TRACE
                if (debugFilter())
                {
                    logChannel("processConvertInput E%i SingleValues: In=%i, Out=%i", iIOIndex, lValue1In, lValueOut);
                }
#endif
                break;
            case VAL_InputConvert_Constant:
                lValueOut = true;
#if LOGIC_TRACE
                if (debugFilter())
                {
                    logChannel("processConvertInput E%i Constant (%i): Out=%i", iIOIndex, lValue1In, lValueOut);
                }
#endif
                break;
            default:
                // do nothing, wrong converter id
#if LOGIC_TRACE
                if (debugFilter())
                {
                    logChannel("processConvertInput E%i: no Execution, wrong convert id", iIOIndex);
                }
#endif
                break;
        }
    }
    // remove processing flag from pipeline
    pCurrentPipeline &= (iIOIndex == IO_Input1) ? ~PIP_CONVERT_INPUT1 : ~PIP_CONVERT_INPUT2;
    // start logic processing for this input
    startLogic(iIOIndex, lValueOut);
}

void LogicChannel::startLogic(uint8_t iIOIndex, bool iValue)
{
    // invert input
    bool lValue = iValue;
    uint16_t lParamBase = (iIOIndex == BIT_EXT_INPUT_1) ? LOG_fE1 : 
                          (iIOIndex == BIT_EXT_INPUT_2) ? LOG_fE2 : 
                          (iIOIndex == BIT_INT_INPUT_1) ? LOG_fI1 : LOG_fI2;
    uint8_t lInput = getByteParam(lParamBase);
    if (iIOIndex == BIT_INT_INPUT_1)
        lInput >>= 4;
    if ((lInput & BIT_INPUT_MASK) == 2)
        lValue = !iValue;
    // set according input bit
    pCurrentIn &= ~iIOIndex;
    pCurrentIn |= iIOIndex * lValue;
    // // set the validity bit
    // pValidActiveIO |= iIOIndex;
    // set the trigger bit
    pTriggerIO |= iIOIndex;
    // finally set the pipeline bit
    pCurrentPipeline |= PIP_LOGIC_EXECUTE;
#if LOGIC_TRACE
    if (debugFilter())
    {
        logChannel("startLogic: Input %s%i; Value %i", (iIOIndex & (BIT_EXT_INPUT_1 | BIT_EXT_INPUT_2)) ? "E" : "I", (iIOIndex & (BIT_EXT_INPUT_1 | BIT_INT_INPUT_1)) ? 1 : 2, lValue);
    }
#endif
}

// Processing parametrized logic
void LogicChannel::processLogic()
{
    /* Logic execution bit is set from any method which changes input values */
    uint8_t lValidInputs = pValidActiveIO & BIT_INPUT_MASK;
    uint8_t lActiveInputs = (pValidActiveIO >> 4) & BIT_INPUT_MASK;
    uint8_t lCurrentInputs = pCurrentIn & lValidInputs;
    bool lCurrentOutput = (pCurrentOut & BIT_OUTPUT_LOGIC);
    bool lInitialOutput = (pCurrentOut & BIT_OUTPUT_INITIAL);
    bool lNewOutput = false;
    bool lValidOutput = false;
#if LOGIC_TRACE
    bool lDebugValid = false;
    const char *lDebugLogic = "Invalid input";
#endif
    // first deactivate execution in pipeline
    pCurrentPipeline &= ~PIP_LOGIC_EXECUTE;
    // we have to delete all trigger if output pipeline is not started
    if (ParamLOG_fCalculate == 0 || lValidInputs == lActiveInputs)
    {
        // we process only if all inputs are valid or the user requested invalid evaluation
        uint8_t lLogic = ParamLOG_fDisable ? 0 : ParamLOG_fLogic;
        uint8_t lOnes = 0;
        switch (lLogic)
        {
            case VAL_Logic_And:
                // AND handles invalid inputs as 1
                // Check if all bits are set -> logical AND of all input bits
                lNewOutput = (lCurrentInputs == lActiveInputs);
                lValidOutput = true;
#if LOGIC_TRACE
                lDebugLogic = "AND";
#endif
                break;
            case VAL_Logic_Or:
                // Check if any bit is set -> logical OR of all input bits
                lNewOutput = (lCurrentInputs > 0);
                lValidOutput = true;
#if LOGIC_TRACE
                lDebugLogic = "OR";
#endif
                break;
            case VAL_Logic_ExOr:
                // EXOR handles invalid inputs as non existing
                // count valid bits in input mask
                for (size_t lBit = 1; lBit < BIT_INPUT_MASK; lBit <<= 1)
                    lOnes += (lCurrentInputs & lBit) > 0;
                // Check if we have an odd number of bits -> logical EXOR of all input bits
                lNewOutput = (lOnes % 2 == 1);
                lValidOutput = true;
#if LOGIC_TRACE
                lDebugLogic = "EXOR";
#endif
                break;
            case VAL_Logic_Switch:
                // Switch cannot handle invalid inputs but is based on telegrams (trigger in this class)
                // An ON-trigger on input 1 turns OUTPUT to 1 (Set of FlipFlop)
                if ((BIT_EXT_INPUT_1 & pTriggerIO & lCurrentInputs) || (BIT_INT_INPUT_1 & pTriggerIO & lCurrentInputs))
                {
                    lNewOutput = true;
                    lValidOutput = true;
                }
                else if ((BIT_EXT_INPUT_2 & pTriggerIO & lCurrentInputs) || (BIT_INT_INPUT_2 & pTriggerIO & lCurrentInputs))
                {
                    lNewOutput = false;
                    lValidOutput = true;
                }
#if LOGIC_TRACE
                lDebugLogic = "SWITCH";
#endif
                break;
            case VAL_Logic_Gate:
                // GATE works a little bit more complex
                // E1 OR I1 are the data inputs
                // E2 OR I2 are the gate inputs
                // Invalid data is handled as ???
                {
                    // Invalid gate is a closed gate (0), as described in app doc
                    // if the behaviour should be changed (invalid is open),
                    // just change the init (for gate and previous) to true.
                    bool lGate = false;
                    bool lPreviousGate = false;
                    // check if gate input is valid
                    if (lValidInputs & (BIT_EXT_INPUT_2 | BIT_INT_INPUT_2))
                    {
                        // get the current gate state
                        lGate = (lCurrentInputs & (BIT_EXT_INPUT_2 | BIT_INT_INPUT_2));
                        // get the previous gate state
                        lPreviousGate = pCurrentIn & BIT_PREVIOUS_GATE;
                        // set previous gate state for next roundtrip
                        pCurrentIn &= ~BIT_PREVIOUS_GATE;
                        // in case gate is closed again immediately we do not store the open state for next roundtrip...
                        bool lIsTriggeredGate = ParamLOG_fTGate;
                        if (lGate && !lIsTriggeredGate)
                            pCurrentIn |= BIT_PREVIOUS_GATE;
                        // ... and we delete the gate input
                        if (lIsTriggeredGate)
                            pCurrentIn &= ~(BIT_EXT_INPUT_2 | BIT_INT_INPUT_2);
                    }
                    uint8_t lGateState = 2 * lPreviousGate + lGate;
                    uint8_t lOnGateTrigger = 0xFF;
                    switch (lGateState)
                    {
                        case VAL_Gate_Closed_Open: // was closed and opens now
                            lOnGateTrigger = ParamLOG_fTriggerGateOpen;
                        case VAL_Gate_Open_Close: // was open and closes now
                        {
                            if (lOnGateTrigger == 0xFF)
                                lOnGateTrigger = ParamLOG_fTriggerGateClose;
                            lValidOutput = true;
                            switch (lOnGateTrigger)
                            {
                                case VAL_Gate_Send_Off:
                                    lNewOutput = false;
                                    break;
                                case VAL_Gate_Send_On:
                                    lNewOutput = true;
                                    break;
                                case VAL_Gate_Send_Input:
                                    lNewOutput = (lCurrentInputs & (BIT_EXT_INPUT_1 | BIT_INT_INPUT_1));
                                    break;
                                default: // same as VAL_Gate_Send_Nothing
                                    lValidOutput = false;
                                    break;
                            }
                        }
                        break;
                        case VAL_Gate_Open_Open: // was open and stays open
                            lNewOutput = (lCurrentInputs & (BIT_EXT_INPUT_1 | BIT_INT_INPUT_1));
                            lValidOutput = true;
                            break;
                        default: // same as VAL_Gate_Closed_Close
                            lValidOutput = false;
                            break;
                    }
#if LOGIC_TRACE
                    lDebugLogic = "TOR";
#endif
                }
                break;
            case VAL_Logic_Timer:
                lNewOutput = (lCurrentInputs & BIT_EXT_INPUT_2);
                lValidOutput = true;
#if LOGIC_TRACE
                lDebugLogic = "TIMER";
#endif
                break;
            default:
#if LOGIC_TRACE
                lDebugLogic = "Invalid Logic";
#endif
                break;
        }
        // now there is a new Output value and we know, if it is valid
        // lets check, if we send this value through the pipeline
        // and if not, we have to delete all trigger
        if (lValidOutput)
        {
            uint8_t lTrigger = ParamLOG_fTrigger;
            uint8_t lHandleFirstProcessing = (lTrigger & 0x30);
            lTrigger &= BIT_INPUT_MASK;
            if (lHandleFirstProcessing == 0)
                pCurrentIn |= BIT_FIRST_PROCESSING;
            if ((lTrigger == 0 && (lNewOutput != lCurrentOutput || lInitialOutput)) ||    /* Just Changes  */
                (lTrigger & pTriggerIO) > 0 ||                                            /* each telegram on specific input */
                (lHandleFirstProcessing > 0 && (pCurrentIn & BIT_FIRST_PROCESSING) == 0)) /* first processing */
            {
                // set the output value (first delete BIT_OUTPUT and then set the value
                // of lNewOutput)
                pCurrentOut = (pCurrentOut & ~BIT_OUTPUT_LOGIC) | (lNewOutput * BIT_OUTPUT_LOGIC);
                // in case that first processing should be skipped, this happens here
                if (pCurrentIn & BIT_FIRST_PROCESSING || lHandleFirstProcessing == BIT_FIRST_PROCESSING)
                {
#if LOGIC_TRACE
                    lDebugValid = true;
                    if (debugFilter())
                    {
                        logChannel("endedLogic: Logic %s, Value %i", lDebugLogic, lNewOutput);
                    }
#endif
                    // now we start stairlight processing
                    startStairlight(lNewOutput);
                }
                else
                {
                    // if first telegram is suppressed, we nevertheless
                    // remove the initial marker.
                    pCurrentOut &= ~BIT_OUTPUT_INITIAL;
                }
                pCurrentIn |= BIT_FIRST_PROCESSING; // first processing was done
            }
#if LOGIC_TRACE
            else
            {
                lDebugValid = true;
                if (debugFilter())
                {
                    if (lTrigger == 0 && lNewOutput == lCurrentOutput)
                    {
                        logChannel("endedLogic: No execution, Logic %s, Value %i (Value not changed)", lDebugLogic, lNewOutput);
                    }
                    else if ((lTrigger & pTriggerIO) == 0)
                    {
                        logChannel("endedLogic: No execution, Logic %s, Value %i (Input was not a trigger)", lDebugLogic, lNewOutput);
                    }
                    else if (lHandleFirstProcessing > 0 && (pCurrentIn & BIT_FIRST_PROCESSING) > 0)
                    {
                        logChannel("endedLogic: No execution, Logic %s, Value %i (Skipped first processing)", lDebugLogic, lNewOutput);
                    }
                }
            }
#endif
        }
    }
#if LOGIC_TRACE
    if (!lDebugValid && debugFilter())
    {
        logChannel("endedLogic: No execution, Logic %s", lDebugLogic);
    }
#endif
    pCurrentIODebug = (pCurrentIn & BIT_INPUT_MASK) | ((pCurrentOut & BIT_OUTPUT_LOGIC) ? BIT_OUTPUT_DEBUG : 0);
    // reset trigger as soon as this logic is executed
    pTriggerIO = 0;
}

void LogicChannel::startStairlight(bool iOutput)
{
    if (ParamLOG_fOStair)
    {
        if (iOutput)
        {
            // if stairlight is not running yet, we switch first the output to on
            if ((pCurrentPipeline & PIP_STAIRLIGHT) == 0)
                startOnDelay();
            // stairlight should also be switched on
            bool lRetrigger = ParamLOG_fORetrigger;
            if ((pCurrentPipeline & PIP_STAIRLIGHT) == 0 || lRetrigger)
            {
                // stairlight is not running or may be re-triggered
                // we init the stairlight timer
#if LOGIC_TRACE
                if (debugFilter())
                    logChannel(lRetrigger ? "retriggerStairlight: %s" : "startStairlight: %s", logTimeBase(LOG_fOStairtimeTime));
#endif
                pStairlightDelay = delayTimerInit();
                pCurrentPipeline |= PIP_STAIRLIGHT;
                startBlink();
            }
        }
        else
        {
            // if stairlight is not running yet, we switch the output to off
            if ((pCurrentPipeline & PIP_STAIRLIGHT) == 0)
                startOffDelay();
            // stairlight should be switched off
            bool lOff = ParamLOG_fOStairOff;
            if (lOff)
            {
                // stairlight might be switched off,
                // we set the timer to 0
                pStairlightDelay = 0;
#if LOGIC_TRACE
                if (debugFilter())
                    logChannel("turnOffStairlight: %s", logTimeBase(LOG_fOStairtimeTime));
#endif
            }
        }
    }
    else if (iOutput)
    {
        // an output without stairlight is forwarded to switch on processing
        startOnDelay();
    }
    else
    {
        startOffDelay();
    }
}

void LogicChannel::processStairlight()
{
    if (pStairlightDelay == 0 || delayCheck(pStairlightDelay, ParamLOG_fOStairtimeTimeMS))
    {
#if LOGIC_TRACE
        if (debugFilter())
        {
            if (pCurrentPipeline & PIP_BLINK)
                logChannel("endedBlink");
            logChannel("endedStairlight: %s", logTimeBase(LOG_fOStairtimeTime));
        }
#endif
        // stairlight time is over, we switch off, also potential blinking
        pCurrentPipeline &= ~(PIP_STAIRLIGHT | PIP_BLINK);
        // we start switchOffProcessing
        startOffDelay();
    }
}

void LogicChannel::startBlink()
{
    uint32_t lBlinkTime = ParamLOG_fOBlinkTimeMS;
    if (lBlinkTime > 0)
    {
#if LOGIC_TRACE
        if (debugFilter())
            logChannel("startBlink: BlinkTime %8.1f s", lBlinkTime / 10.0);
#endif
        pBlinkDelay = millis();
        pCurrentPipeline |= PIP_BLINK;
        pCurrentOut |= BIT_OUTPUT_BLINK;
    }
}

void LogicChannel::processBlink()
{
    uint32_t lBlinkTime = ParamLOG_fOBlinkTimeMS;
    if (delayCheck(pBlinkDelay, lBlinkTime))
    {
        bool lOn = (pCurrentOut & BIT_OUTPUT_BLINK);
        if (!lOn)
        {
#if LOGIC_TRACE
            if (debugFilter())
                logChannel("processBlink: On");
#endif
            pCurrentOut |= BIT_OUTPUT_BLINK;
            startOnDelay();
        }
        else
        {
#if LOGIC_TRACE
            if (debugFilter())
                logChannel("processBlink: Off");
#endif
            pCurrentOut &= ~BIT_OUTPUT_BLINK;
            startOffDelay();
        }
        pBlinkDelay = millis();
    }
}

// delays the on signal by defined duration
void LogicChannel::startOnDelay()
{
    // if on delay is already running, there are options:
    //    1. second on does nothing, the rest of delay time passes
    //    2. second on restarts delay time
    //    3. second on switches immediately on
    //    4. an off stops on delay
    uint8_t lOnDelayRepeat = ParamLOG_fODelayOnRepeat;
    if ((pCurrentPipeline & PIP_ON_DELAY) == 0)
    {
        // on delay is not running, we start it
        pOnDelay = delayTimerInit();
        pCurrentPipeline |= PIP_ON_DELAY;
#if LOGIC_TRACE
        if (debugFilter())
            logChannel("startOnDelay: Time %s", logTimeBase(LOG_fODelayOnTime));
#endif
    }
    else
    {
        // we have a new on value, we look how to process in case of repetition
        switch (lOnDelayRepeat)
        {
            case VAL_Delay_Immediate:
                // end pipeline and switch immediately
                // cData->currentPipeline &= ~PIP_ON_DELAY;
                // StartOnOffRepeat(cData, iChannel, true);
                pOnDelay = 0;
#if LOGIC_TRACE
                if (debugFilter())
                    logChannel("startOnDelay: Second ON, turn on immediately");
#endif
                break;
            case VAL_Delay_Extend:
                pOnDelay = delayTimerInit();
#if LOGIC_TRACE
                if (debugFilter())
                    logChannel("startOnDelay: Second ON, extend delay by %s", logTimeBase(LOG_fODelayOnTime));
#endif
                break;
            default:
#if LOGIC_TRACE
                if (debugFilter())
                    logChannel("startOnDelay: Second ON, simply continue, remaining %li ms", millis() - pOnDelay);
#endif
                break;
        }
    }
    uint8_t lOffDelayReset = ParamLOG_fODelayOffReset;
    // if requested, this on stops an off delay
    if ((lOffDelayReset > 0) && (pCurrentPipeline & PIP_OFF_DELAY) > 0)
    {
#if LOGIC_TRACE
        if (debugFilter())
            logChannel("endedOffDelay: ON during OffDelay");
#endif
        pCurrentPipeline &= ~PIP_OFF_DELAY;
        // there might be an additional option necessary:
        // - an additional ON stops processing
        // currently we do this by default
        // pCurrentPipeline &= ~PIP_ON_DELAY;
    }
}

void LogicChannel::processOnDelay()
{
    uint32_t lOnDelay = ParamLOG_fODelayOnTimeMS;
    if (pOnDelay == 0 || delayCheck(pOnDelay, lOnDelay))
    {
#if LOGIC_TRACE
        if (debugFilter())
            logChannel("endedOnDelay: Normal delay time %s", logTimeBase(LOG_fODelayOnTime));
#endif
        // delay time is over, we turn off pipeline
        pCurrentPipeline &= ~PIP_ON_DELAY;
        // we start repeatOnProcessing
        startOutputFilter(true);
    }
}

// delays the off signal by defined duration
void LogicChannel::startOffDelay()
{
    // if off delay is already running, there are options:
    //    1. second off switches immediately off
    //    2. second off restarts delay time
    //    3. an on stops off delay
    uint8_t lOffDelayRepeat = ParamLOG_fODelayOffRepeat;
    if ((pCurrentPipeline & PIP_OFF_DELAY) == 0)
    {
        pOffDelay = delayTimerInit();
        pCurrentPipeline |= PIP_OFF_DELAY;
#if LOGIC_TRACE
        if (debugFilter())
            logChannel("startOffDelay: Time %s", logTimeBase(LOG_fODelayOffTime));
#endif
    }
    else
    {
        // we have a new on value, we look how to process in case of repetition
        switch (lOffDelayRepeat)
        {
            case VAL_Delay_Immediate:
                // end pipeline and switch immediately
                // cData->currentPipeline &= ~PIP_OFF_DELAY;
                // StartOnOffRepeat(cData, iChannel, false);
                pOffDelay = 0;
#if LOGIC_TRACE
                if (debugFilter())
                    logChannel("startOffDelay: Second OFF, turn off immediately");
#endif
                break;
            case VAL_Delay_Extend:
                pOffDelay = delayTimerInit();
#if LOGIC_TRACE
                if (debugFilter())
                    logChannel("startOffDelay: Second OFF, extend delay by %s", logTimeBase(LOG_fODelayOffTime));
#endif
                break;
            default:
#if LOGIC_TRACE
                if (debugFilter())
                    logChannel("startOffDelay: Second OFF, simply continue, remaining %li ms", millis() - pOffDelay);
#endif
                break;
        }
    }
    uint8_t lOnDelayReset = ParamLOG_fODelayOnReset;
    // if requested, this off stops an on delay
    if ((lOnDelayReset > 0) && (pCurrentPipeline & PIP_ON_DELAY) > 0)
    {
#if LOGIC_TRACE
        if (debugFilter())
            logChannel("endedOnDelay: OFF during OnDelay");
#endif
        pCurrentPipeline &= ~PIP_ON_DELAY;
        // there might be an additional option necessary:
        // - an additional OFF stops processing
        // currently we do this by default
        // pCurrentPipeline &= ~PIP_OFF_DELAY;
    }
}

void LogicChannel::processOffDelay()
{
    uint32_t lOffDelay = ParamLOG_fODelayOffTimeMS;
    if (pOffDelay == 0 || delayCheck(pOffDelay, lOffDelay))
    {
#if LOGIC_TRACE
        if (debugFilter())
            logChannel("endedOffDelay: Normal delay time %s", logTimeBase(LOG_fODelayOffTime));
#endif
        // delay time is over, we turn off pipeline
        pCurrentPipeline &= ~PIP_OFF_DELAY;
        // we start repeatOffProcessing
        startOutputFilter(false);
    }
}

// Output filter prevents repetition of 0 or 1 values
void LogicChannel::startOutputFilter(bool iOutput)
{
    uint8_t lAllow = ParamLOG_fOOutputFilter;
    bool lLastOutput = (pCurrentOut & BIT_OUTPUT_PREVIOUS);
    bool lInitialOutput = (pCurrentOut & BIT_OUTPUT_INITIAL);
    bool lContinue = false;
    switch (lAllow)
    {
        case VAL_AllowRepeat_All:
            lContinue = true;
            break;
        case VAL_AllowRepeat_On:
            lContinue = (iOutput || iOutput != lLastOutput || lInitialOutput);
            break;
        case VAL_AllowRepeat_Off:
            lContinue = (!iOutput || iOutput != lLastOutput || lInitialOutput);
            break;
        default: // VAL_AllowRepeat_None
            lContinue = (iOutput != lLastOutput || lInitialOutput);
            break;
    }
    if (lContinue)
    {
        pCurrentPipeline &= ~(PIP_OUTPUT_FILTER_OFF | PIP_OUTPUT_FILTER_ON);
        pCurrentPipeline |= iOutput ? PIP_OUTPUT_FILTER_ON : PIP_OUTPUT_FILTER_OFF;
        pCurrentOut &= ~(BIT_OUTPUT_PREVIOUS | BIT_OUTPUT_INITIAL); // output is not initial anymore
        if (iOutput)
            pCurrentOut |= BIT_OUTPUT_PREVIOUS;
    }
}
void LogicChannel::processOutputFilter()
{
    if (pCurrentPipeline & PIP_OUTPUT_FILTER_ON)
        startOnOffRepeat(true);
    else if (pCurrentPipeline & PIP_OUTPUT_FILTER_OFF)
        startOnOffRepeat(false);
    pCurrentPipeline &= ~(PIP_OUTPUT_FILTER_OFF | PIP_OUTPUT_FILTER_ON);
}

// starts On-Off-Repeat
void LogicChannel::startOnOffRepeat(bool iOutput)
{
    // with repeat, we first process the output and then we repeat the signal
    // if repeat is already active, we wait until next cycle
    if (iOutput)
    {
        if ((pCurrentPipeline & PIP_ON_REPEAT) == 0)
        {
            pRepeatOnOffDelay = millis();
            pCurrentPipeline &= ~PIP_OFF_REPEAT;
            processOutput(iOutput);
            if (ParamLOG_fORepeatOnTimeMS > 0)
            {
                pCurrentPipeline |= PIP_ON_REPEAT;
#if LOGIC_TRACE
                if (debugFilter())
                    logChannel("startOnRepeat: Every %s", logTimeBase(LOG_fORepeatOnTime));
#endif
            }
        }
    }
    else
    {
        if ((pCurrentPipeline & PIP_OFF_REPEAT) == 0)
        {
            pRepeatOnOffDelay = millis();
            pCurrentPipeline &= ~PIP_ON_REPEAT;
            processOutput(iOutput);
            if (ParamLOG_fORepeatOffTimeMS > 0)
            {
                pCurrentPipeline |= PIP_OFF_REPEAT;
#if LOGIC_TRACE
                if (debugFilter())
                    logChannel("startOffRepeat: Every %s", logTimeBase(LOG_fORepeatOffTime));
#endif
            }
        }
    }
}

void LogicChannel::processOnOffRepeat()
{
    uint32_t lRepeat = 0;
    bool lValue;

    // we can handle On/Off repeat in one method, because they are alternative and never
    // set both in parallel
    if (pCurrentPipeline & PIP_ON_REPEAT)
    {
        lRepeat = ParamLOG_fORepeatOnTimeMS;
        lValue = true;
    }
    if (pCurrentPipeline & PIP_OFF_REPEAT)
    {
        lRepeat = ParamLOG_fORepeatOffTimeMS;
        lValue = false;
    }

    if (delayCheck(pRepeatOnOffDelay, lRepeat))
    {
#if LOGIC_TRACE
        if (debugFilter())
        {
            if (lValue)
                logChannel("processOnRepeat: After %s", logTimeBase(LOG_fORepeatOnTime));
            else
                logChannel("processOffRepeat: After %s", logTimeBase(LOG_fORepeatOffTime));
        }
#endif
        // delay time is over, we repeat the output
        processOutput(lValue);
        // and we restart repeat counter
        pRepeatOnOffDelay = millis();
    }
}

// we trigger all associated internal inputs with the new value
void LogicChannel::processInternalInputs(uint8_t iChannelId, bool iValue)
{
    uint8_t lInput1 = ParamLOG_fI1;
    if (lInput1 > 0)
    {
        uint8_t lFunction1 = ParamLOG_fI1Function;
        if (lFunction1 == (iChannelId + 1))
        {
#if LOGIC_TRACE
            if (debugFilter())
                logChannel("processInternalInputs: Input I1, Value %i", iValue);
#endif
            startLogic(BIT_INT_INPUT_1, iValue);
            // we also add that this input was used and is now valid
            pValidActiveIO |= BIT_INT_INPUT_1;
        }
    }
    uint8_t lInput2 = ParamLOG_fI2;
    if (lInput2 > 0)
    {
        uint8_t lFunction2 = ParamLOG_fI2Function;
        if (lFunction2 == (iChannelId + 1))
        {
#if LOGIC_TRACE
            if (debugFilter())
                logChannel("processInternalInputs: Input I2, Value %i", iValue);
#endif
            startLogic(BIT_INT_INPUT_2, iValue);
            // we also add that this input was used and is now valid
            pValidActiveIO |= BIT_INT_INPUT_2;
        }
    }
}

bool LogicChannel::processDiagnoseCommand(char *cBuffer)
{
    bool lResult = false;
    switch (cBuffer[0])
    {
        case 'l': {
            char v[5];
            // here we find the last IO state
            uint8_t lValidInput = pValidActiveIO & BIT_INPUT_MASK;
            uint8_t lCurrentIO = pCurrentIODebug & 0x1F;
            // input values
            for (uint8_t i = 0; i < 4; i++)
            {
                if (lValidInput & 1)
                {
                    // input is valid, we present its value
                    v[i] = (lCurrentIO & 1) ? '1' : '0';
                }
                else
                {
                    // invalid input
                    v[i] = 'x';
                }
                lValidInput >>= 1;
                lCurrentIO >>= 1;
            }
            // output value
            if ((pCurrentPipeline & PIP_RUNNING) && (pCurrentIn & BIT_FIRST_PROCESSING))
            {
                v[4] = (lCurrentIO & 1) ? '1' : '0';
            }
            else
            {
                v[4] = 'x';
            }
            // list state of logic of last execution
            sprintf(cBuffer, "A%c B%c C%c D%c Q%c", v[0], v[1], v[2], v[3], v[4]);
            lResult = true;
            break;
        }
        default:
            break;
    }
    return lResult;
}

// process the output itself
void LogicChannel::processOutput(bool iValue)
{
    bool lInternalInputs = ((iValue && ParamLOG_fOInternalOn) || (!iValue && ParamLOG_fOInternalOff));
    if (lInternalInputs)
        LogicChannel::sLogic->processAllInternalInputs(this, iValue);
#if LOGIC_TRACE
    if (debugFilter())
    {
        logChannel("processOutput: Value %i", iValue);
    }
#endif
    if (iValue)
    {
        uint8_t lOn = ParamLOG_fOOn;
        switch (lOn)
        {
            case VAL_Out_Constant:
                writeConstantValue(LOG_fOOnDpt1);
                break;
            case VAL_Out_ValE1:
                writeParameterValue(IO_Input1);
                break;
            case VAL_Out_ValE2:
                writeParameterValue(IO_Input2);
                break;
            case VAL_Out_Function:
                writeFunctionValue(LOG_fOOnFunction);
                break;
            case VAL_Out_ReadRequest:
                knxRead(IO_Output);
                break;
            case VAL_Out_ResetDevice:
                knxResetDevice(LOG_fOOnDpt1);
                break;
            case VAL_Out_Buzzer:
                setBuzzer(LOG_fOOnDpt1);
                break;
            case VAL_Out_RGBLed:
                setRGBColor(LOG_fOOnDpt1);
                break;
            default:
                // there is no output parametrized
                break;
        }
    }
    else
    {
        uint8_t lOff = ParamLOG_fOOff;
        switch (lOff)
        {
            case VAL_Out_Constant:
                writeConstantValue(LOG_fOOffDpt1);
                break;
            case VAL_Out_ValE1:
                writeParameterValue(IO_Input1);
                break;
            case VAL_Out_ValE2:
                writeParameterValue(IO_Input2);
                break;
            case VAL_Out_Function:
                writeFunctionValue(LOG_fOOffFunction);
                break;
            case VAL_Out_ReadRequest:
                knxRead(IO_Output);
                break;
            case VAL_Out_ResetDevice:
                knxResetDevice(LOG_fOOffDpt1);
                break;
            case VAL_Out_Buzzer:
                setBuzzer(LOG_fOOffDpt1);
                break;
            case VAL_Out_RGBLed:
                setRGBColor(LOG_fOOffDpt1);
                break;
            default:
                // there is no output parametrized
                break;
        }
    }
}

bool LogicChannel::checkDpt(uint8_t iIOIndex, uint8_t iDpt)
{
    uint8_t lDpt;
    switch (iIOIndex)
    {
        case IO_Input1:
            lDpt = ParamLOG_fE1Dpt;
            break;
        case IO_Input2:
            lDpt = ParamLOG_fE2Dpt;
            break;
        case IO_Output:
            lDpt = ParamLOG_fODpt;
            break;
        default:
            return false;
    }
    return lDpt == iDpt;
}

bool LogicChannel::readOneInputFromFlash(uint8_t iIOIndex)
{
    // bool lResult = false;
    // const uint8_t *lFlashBuffer = sLogic->getFlash();
    // // first check, if Flash contains valid values
    // if (lFlashBuffer != nullptr)
    //     lResult = true;
    // // Now check, if the DPT for requested KO is valid
    // // DPT might have changed due to new programming after last save
    // uint16_t lAddress = USERDATA_DPT_OFFSET + channelIndex() * 2 + iIOIndex - 1;
    // if (lResult)
    //     lResult = checkDpt(iIOIndex, lFlashBuffer[lAddress]);
    // // if the dpt is ok, we get the ko value
    // if (lResult)
    // {
    //     lAddress = USERDATA_KO_OFFSET + channelIndex() * 8 + (iIOIndex - 1) * 4;
    //     GroupObject *lKo = getKo(iIOIndex);
    //     for (uint8_t lIndex = 0; lIndex < lKo->valueSize(); lIndex++)
    //         lKo->valueRef()[lIndex] = lFlashBuffer[lAddress + lIndex];
    //     // lKo->commFlag(ComFlag::Ok);
    //     lKo->objectWritten(); // we set the restored KO as valid for read (if L-Flat is set) and as sending (if Ü-Flag is set)
    // }
    // return lResult;
    return false;
}

void LogicChannel::restore()
{
    restore(IO_Input1);
    restore(IO_Input2);
}

void LogicChannel::restore(uint8_t iIOIndex)
{
    uint8_t lDpt = openknx.flash.readByte();
    uint8_t *lValue = openknx.flash.read(4);

    if (!checkDpt(iIOIndex, lDpt))
        return;

    logInfoP("      Input%i:  DPT %i  DATA: %02X %02X %02X %02X", iIOIndex, lDpt, lValue[0], lValue[1], lValue[2], lValue[3]);

    GroupObject *lKo = getKo(iIOIndex);

    for (uint8_t lIndex = 0; lIndex < lKo->valueSize(); lIndex++)
        lKo->valueRef()[lIndex] = lValue[lIndex];

    // if (iIOIndex == 1)
    //     mFlashLoadedInput1[channelIndex()] = true;
    // else
    //     mFlashLoadedInput2[channelIndex()] = true;

    lKo->commFlag(ComFlag::Ok);
    // lKo->objectWritten(); // we set the restored KO as valid for read (if L-Flat is set) and as sending (if Ü-Flag is set)
}

void LogicChannel::save()
{
    saveKoDpt(IO_Input1);
    saveKoValue(IO_Input1);

    saveKoDpt(IO_Input2);
    saveKoValue(IO_Input2);
}

void LogicChannel::saveKoDpt(uint8_t iIOIndex)
{
    uint8_t lDpt = 0xFF;
    if (isInputActive(iIOIndex) && isInputValid(iIOIndex))
    {
        // now get input default value
        uint8_t lParInput = getByteParam(iIOIndex == 1 ? LOG_fE1Default : LOG_fE2Default);
        if (lParInput & VAL_InputDefault_EEPROM)
        {
            // if the default is Flash, we get correct dpt
            lDpt = getByteParam(iIOIndex == 1 ? LOG_fE1Dpt : LOG_fE2Dpt);
        }
    }

    // logInfo("LogicChannel", "%02X ", lDpt);
    openknx.flash.writeByte(lDpt);
}

void LogicChannel::saveKoValue(uint8_t iIOIndex)
{
    GroupObject *lKo = getKo(iIOIndex);
    if (lKo->valueSize() > 4)
    {
        openknx.flash.writeInt(0x0); // 4 bytes
        return;
    }

    openknx.flash.write(lKo->valueRef(), lKo->valueSize());
    openknx.flash.write((uint8_t)0x0, (4 - lKo->valueSize()));
}

// returns true, if any DPT from Flash does not fit to according input DPT.
// in such a case the DPTs have to be written to Flash again
// bool LogicChannel::prepareChannel()
void LogicChannel::prepareChannel()
{
    // bool lResult = false;
    bool lInput1Flash = false;
    bool lInput2Flash = false;
    uint8_t lLogicFunction = ParamLOG_fDisable ? 0 : ParamLOG_fLogic;

    // logChannel("       prepareChannel");
    if (lLogicFunction == 5)
    {
        // timer implementation, timer is on ext input 2
        pValidActiveIO |= BIT_EXT_INPUT_2 >> 4;
        startStartup();
    }
    else if (lLogicFunction > 0)
    {
        // function is active, we process input presets
        // external input 1
        if (isInputActive(IO_Input1))
        {
            // input is active, we set according flag
            pValidActiveIO |= BIT_EXT_INPUT_1 << 4;
            // prepare input for external KO
            uint16_t lExternalKo = getWordParam(LOG_fE1OtherKO);
            if (lExternalKo & 0x8000) // LOG_fE1UseOtherKOMask)
            {
                sLogic->addKoLookup(lExternalKo & 0x03FFF, channelIndex(), IO_Input1);
            }
            // prepare input for cyclic read
            pInputProcessing.repeatInput1Delay = ParamLOG_fE1RepeatTimeMS;
            if (pInputProcessing.repeatInput1Delay)
            {
                pInputProcessing.repeatInput1Delay = millis();
                pCurrentPipeline |= PIP_REPEAT_INPUT1;
            }
            // now set input default value
            uint8_t lParInput = getByteParam(LOG_fE1Default);
            // should default be fetched from Flash
            if (lParInput & VAL_InputDefault_EEPROM)
            {
                // we expect, that the KO was loaded from Flash, if applicable
                GroupObject *lKo = getKo(IO_Input1);
                lInput1Flash = (lKo->commFlag() != ComFlag::Uninitialized);
                if (lInput1Flash)
                    lKo->objectWritten();
                else
                    lParInput &= ~VAL_InputDefault_EEPROM;
            }
            switch (lParInput)
            {
                case VAL_InputDefault_Read:
                    /* to read immediately we activate repeated read pipeline with 0 delay */
                    pInputProcessing.repeatInput1Delay = 0;
                    pCurrentPipeline |= PIP_REPEAT_INPUT1;
                    break;

                case VAL_InputDefault_False:
                    /* we clear bit for E1 and mark this value as valid */
                    startLogic(BIT_EXT_INPUT_1, false);
                    // we also add that this input was used and is now valid
                    pValidActiveIO |= BIT_EXT_INPUT_1;
                    break;

                case VAL_InputDefault_True:
                    /* we set bit for E1 and mark this value as valid */
                    startLogic(BIT_EXT_INPUT_1, true);
                    // we also add that this input was used and is now valid
                    pValidActiveIO |= BIT_EXT_INPUT_1;
                    break;

                default:
                    /* do nothing, value is invalid */
                    break;
            }
        }
        // external input 2
        if (isInputActive(IO_Input2))
        {
            // input is active, we set according flag
            pValidActiveIO |= BIT_EXT_INPUT_2 << 4;
            // prepare input for external KO
            uint16_t lExternalKo = getWordParam(LOG_fE2OtherKO);
            if (lExternalKo & 0x8000) // LOG_fE2UseOtherKOMask)
            {
                sLogic->addKoLookup(lExternalKo & 0x3FFF, channelIndex(), IO_Input2);
            }
            // prepare input for cyclic read
            pInputProcessing.repeatInput2Delay = ParamLOG_fE2RepeatTimeMS;
            if (pInputProcessing.repeatInput2Delay)
            {
                pInputProcessing.repeatInput2Delay = millis();
                pCurrentPipeline |= PIP_REPEAT_INPUT2;
            }
            uint8_t lParInput = getByteParam(LOG_fE2Default);
            // should default be fetched from Flash
            if (lParInput & VAL_InputDefault_EEPROM)
            {
                // we expect, that the KO was loaded from Flash, if applicable
                GroupObject *lKo = getKo(IO_Input2);
                lInput2Flash = (lKo->commFlag() != ComFlag::Uninitialized);
                if (lInput2Flash)
                    lKo->objectWritten();
                else
                    lParInput &= ~VAL_InputDefault_EEPROM;
            }
            switch (lParInput)
            {
                case VAL_InputDefault_Read:
                    /* to read immediately we activate repeated read pipeline with 0 delay */
                    pInputProcessing.repeatInput2Delay = 0;
                    pCurrentPipeline |= PIP_REPEAT_INPUT2;
                    break;

                case VAL_InputDefault_False:
                    /* we clear bit for E2 and mark this value as valid */
                    startLogic(BIT_EXT_INPUT_2, false);
                    // we also add that this input was used and is now valid
                    pValidActiveIO |= BIT_EXT_INPUT_2;
                    break;

                case VAL_InputDefault_True:
                    /* we set bit for E2 and mark this value as valid */
                    startLogic(BIT_EXT_INPUT_2, true);
                    // we also add that this input was used and is now valid
                    pValidActiveIO |= BIT_EXT_INPUT_2;
                    break;

                default:
                    /* do nothing, value is invalid */
                    break;
            }
        }
        // internal input 1
        // first check, if input is active
        uint8_t lIsActive = ParamLOG_fI1;
        if (lIsActive > 0)
        {
            // input is active, we set according flag
            pValidActiveIO |= BIT_INT_INPUT_1 << 4;
        }
        // internal input 2
        // first check, if input is active
        lIsActive = ParamLOG_fI2;
        if (lIsActive > 0)
        {
            // input is active, we set according flag
            pValidActiveIO |= BIT_INT_INPUT_2 << 4;
        }
        // we set the startup delay
        startStartup();
        // we trigger input processing, if there are values from Flash
        if (lInput1Flash)
            processInput(IO_Input1);
        if (lInput2Flash)
            processInput(IO_Input2);
    }
    // return lResult;
}

void LogicChannel::loop()
{
    if (!knx.configured())
        return;

    if (pCurrentPipeline & PIP_STARTUP)
        processStartup();
    if (pCurrentPipeline & PIP_TIMER_RESTORE_STATE)
        processTimerRestoreState(sTimerRestore);

    // do no further processing until channel passed its startup time
    if (pCurrentPipeline & PIP_RUNNING)
    {
        if (pCurrentPipeline & PIP_TIMER_INPUT)
            processTimerInput();
        // repeat input pipeline
        if (pCurrentPipeline & PIP_REPEAT_INPUT1)
            processRepeatInput1();
        if (pCurrentPipeline & PIP_REPEAT_INPUT2)
            processRepeatInput2();
        // convert input pipeline
        if (pCurrentPipeline & PIP_CONVERT_INPUT1)
            processConvertInput(IO_Input1);
        if (pCurrentPipeline & PIP_CONVERT_INPUT2)
            processConvertInput(IO_Input2);
        // Logic execution pipeline
        if (pCurrentPipeline & PIP_LOGIC_EXECUTE)
            processLogic();
        // stairlight pipeline
        if (pCurrentPipeline & PIP_STAIRLIGHT)
            processStairlight();
        // blink pipeline (has to be "after" stairlight)
        if (pCurrentPipeline & PIP_BLINK)
            processBlink();
        // Off delay pipeline
        if (pCurrentPipeline & PIP_OFF_DELAY)
            processOffDelay();
        // On delay pipeline
        if (pCurrentPipeline & PIP_ON_DELAY)
            processOnDelay();
        // Output Filter pipeline
        if (pCurrentPipeline & (PIP_OUTPUT_FILTER_ON | PIP_OUTPUT_FILTER_OFF))
            processOutputFilter();
        // On/Off repeat pipeline
        if (pCurrentPipeline & (PIP_ON_REPEAT | PIP_OFF_REPEAT))
            processOnOffRepeat();
    }
}

// Start of Timer implementation
void LogicChannel::startTimerInput()
{
    uint8_t lLogicFunction = ParamLOG_fDisable ? 0 : ParamLOG_fLogic;
    if (lLogicFunction == VAL_Logic_Timer && sTimer.isTimerValid())
    {
        pCurrentPipeline |= PIP_TIMER_INPUT;
    }
}

// called every minute, finds the next timer to process and marks it
void LogicChannel::processTimerInput()
{
    bool lIsYearTimer = ParamLOG_fTYearDay;
    uint8_t lCountTimer = lIsYearTimer ? VAL_Tim_YearTimerCount : VAL_Tim_DayTimerCount; // there are 4 year timer or 8 day timer
    bool lToday;                                                                         // if it is a day timer lToday=true
    bool lResult = false;
    bool lValue;
    bool lEvaluate = true;
    // first we process settings valid for whole timer
    // vacation
    bool lIsVacation = KoLOG_Vacation.value(getDPT(VAL_DPT_1));
    uint8_t lVacationSetting = ParamLOG_fTVacation;
    if (lVacationSetting == VAL_Tim_Special_No && lIsVacation)
        lEvaluate = false;
    if (lVacationSetting == VAL_Tim_Special_Skip || lVacationSetting == VAL_Tim_Special_Sunday)
        lEvaluate = true;
    if (lVacationSetting == VAL_Tim_Special_Only)
        lEvaluate = lIsVacation;

    // holiday
    uint8_t lHolidaySetting = ParamLOG_fTHoliday;
    if (lEvaluate)
    {
        if (lHolidaySetting == VAL_Tim_Special_No && (sTimer.holidayToday() > 0))
            lEvaluate = false;
        if (lHolidaySetting == VAL_Tim_Special_Skip || lHolidaySetting == VAL_Tim_Special_Sunday)
            lEvaluate = true;
        if (lHolidaySetting == VAL_Tim_Special_Only)
            lEvaluate = (sTimer.holidayToday() > 0);
    }

    if (lEvaluate)
    {
        bool lHandleAsSunday = (lHolidaySetting == VAL_Tim_Special_Sunday && (sTimer.holidayToday() > 0)) ||
                               (lVacationSetting == VAL_Tim_Special_Sunday && lIsVacation);

        // loop through all timer
        uint32_t lTimerFunctions = getIntParam(LOG_fTd1DuskDawn);
        for (uint8_t lTimerIndex = 0; lTimerIndex < lCountTimer; lTimerIndex++)
        {
            // get timer function code
            uint8_t lTimerFunction = (lTimerFunctions >> (28 - lTimerIndex * 4)) & 0xF;
            if (lTimerFunction)
            {
                // timer function is active
                lToday = !lIsYearTimer || checkTimerToday(sTimer, lTimerIndex, lHandleAsSunday);
                if (lToday)
                {
                    uint16_t lBitfield = getWordParam(LOG_fTd1Value + 2 * lTimerIndex);
                    lValue = lBitfield & 0x8000;
                    switch (lTimerFunction)
                    {
                        case VAL_Tim_PointInTime:
                            lResult = checkPointInTime(sTimer, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday);
                            break;
                        case VAL_Tim_Sunrise_Plus:
                            lResult = checkSunAbs(sTimer, SUN_SUNRISE, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, false);
                            break;
                        case VAL_Tim_Sunrise_Minus:
                            lResult = checkSunAbs(sTimer, SUN_SUNRISE, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, true);
                            break;
                        case VAL_Tim_Sunset_Plus:
                            lResult = checkSunAbs(sTimer, SUN_SUNSET, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, false);
                            break;
                        case VAL_Tim_Sunset_Minus:
                            lResult = checkSunAbs(sTimer, SUN_SUNSET, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, true);
                            break;
                        case VAL_Tim_Sunrise_Earliest:
                            lResult = checkSunLimit(sTimer, SUN_SUNRISE, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, false);
                            break;
                        case VAL_Tim_Sunrise_Latest:
                            lResult = checkSunLimit(sTimer, SUN_SUNRISE, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, true);
                            break;
                        case VAL_Tim_Sunset_Earliest:
                            lResult = checkSunLimit(sTimer, SUN_SUNSET, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, false);
                            break;
                        case VAL_Tim_Sunset_Latest:
                            lResult = checkSunLimit(sTimer, SUN_SUNSET, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, true);
                            break;
                        case VAL_Tim_Sunrise_DegreeUp:
                            lResult = checkSunDegree(sTimer, SUN_SUNRISE, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, false);
                            break;
                        case VAL_Tim_Sunset_DegreeUp:
                            lResult = checkSunDegree(sTimer, SUN_SUNSET, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, false);
                            break;
                        case VAL_Tim_Sunrise_DegreeDown:
                            lResult = checkSunDegree(sTimer, SUN_SUNRISE, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, true);
                            break;
                        case VAL_Tim_Sunset_DegreeDown:
                            lResult = checkSunDegree(sTimer, SUN_SUNSET, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, true);
                            break;
                        default:
                            break;
                    }
                }
            }
            if (lResult)
                break;
        }
        if (lResult)
        {
#if LOGIC_TRACE
            if (debugFilter())
            {
                logChannel("startTimerInput: Value %i", lValue);
            }
#endif
            startLogic(BIT_EXT_INPUT_2, lValue);
            // we also add that this input was used and is now valid
            pValidActiveIO |= BIT_EXT_INPUT_2;
            // if a timer is executed, it has not to be restored anymore
            pCurrentPipeline &= ~PIP_TIMER_RESTORE_STATE;
        }
    }
    // we wait for next timer execution
    pCurrentPipeline &= ~PIP_TIMER_INPUT;
}

// checks if timer is valid today
// just called for year timer
bool LogicChannel::checkTimerToday(Timer &iTimer, uint8_t iTimerIndex, bool iHandleAsSunday)
{
    bool lResult = false;
    // check for valid index
    if (iTimerIndex < 4)
    {
        // now we check correct month
        uint8_t lMonth = (getByteParam(LOG_fTy1Month + 2 * iTimerIndex) >> 4) & 0xF;
        if (lMonth == 0 || lMonth == iTimer.getMonth())
        {
            // we have the correct month, check correct day
            uint8_t lDayWeekday = (getByteParam(LOG_fTy1Day + 2 * iTimerIndex));
            if (lDayWeekday & 1)
            {
                // Wochentag
                if (lDayWeekday == 0xFF)
                {
                    // shortcut for 'every day'
                    lResult = true;
                }
                else if (lDayWeekday > 1)
                {
                    for (uint8_t lWeekday = 1; lWeekday < 8; lWeekday++)
                    {
                        if (lDayWeekday & 0x80)
                        {
                            lResult = checkWeekday(iTimer, lWeekday, iHandleAsSunday);
                            if (lResult)
                                break;
                        };
                        lDayWeekday <<= 1;
                    }
                }
            }
            else
            {
                // Tag
                lDayWeekday >>= 1;
                lResult = (lDayWeekday == 0) || (lDayWeekday == iTimer.getDay());
            }
        }
    }
    return lResult;
}

// iWeekday is in our format (1=Monday, ..., 7 = Sunday, 0=any)
bool LogicChannel::checkWeekday(Timer &iTimer, uint8_t iWeekday, bool iHandleAsSunday)
{
    if (iWeekday > 7)
        return false;
    if (iWeekday == 0)
    {
        return true;
    }
    if (iWeekday == 7)
    {
        iWeekday = 0;
        if (iHandleAsSunday)
            return true;
    }
    return iWeekday == iTimer.getWeekday();
}

bool LogicChannel::checkTimerTime(Timer &iTimer, uint8_t iTimerIndex, uint16_t iBitfield, uint8_t iHour, uint8_t iMinute, bool iSkipWeekday, bool iHandleAsSunday)
{
    bool lResult = false;

    // check correct timer index
    if (iTimerIndex < 8)
    {
        if (iSkipWeekday || checkWeekday(iTimer, iBitfield & 0x7, iHandleAsSunday))
        {
            // check hour
            if (iHour == 31 || iHour == iTimer.getHour())
            {
                // check minute
                if (iMinute == 63 || iMinute == iTimer.getMinute())
                {
                    lResult = true;
                }
            }
        }
    }
    return lResult;
}

bool LogicChannel::checkPointInTime(Timer &iTimer, uint8_t iTimerIndex, uint16_t iBitfield, bool iSkipWeekday, bool iHandleAsSunday)
{
    uint8_t lHour = (iBitfield & 0x3E00) >> 9;
    uint8_t lMinute = (iBitfield & 0x01F8) >> 3;
    bool lResult = checkTimerTime(iTimer, iTimerIndex, iBitfield, lHour, lMinute, iSkipWeekday, iHandleAsSunday);
    return lResult;
}

bool LogicChannel::checkSunAbs(Timer &iTimer, uint8_t iSunInfo, uint8_t iTimerIndex, uint16_t iBitfield, bool iSkipWeekday, bool iHandleAsSunday, bool iMinus)
{
    int8_t lFactor = (iMinus) ? -1 : 1;
    uint8_t lHour = (iTimer.getSunInfo(iSunInfo)->hour + ((iBitfield & 0x3E00) >> 9) * lFactor) % 24;
    uint8_t lMinute = (iTimer.getSunInfo(iSunInfo)->minute + ((iBitfield & 0x01F8) >> 3) * lFactor) % 60;
    bool lResult = checkTimerTime(iTimer, iTimerIndex, iBitfield, lHour, lMinute, iSkipWeekday, iHandleAsSunday);
    return lResult;
}

bool LogicChannel::checkSunLimit(Timer &iTimer, uint8_t iSunInfo, uint8_t iTimerIndex, uint16_t iBitfield, bool iSkipWeekday, bool iHandleAsSunday, bool iLatest)
{
    uint8_t lHour = ((iBitfield & 0x3E00) >> 9);
    uint8_t lMinute = ((iBitfield & 0x01F8) >> 3);
    int8_t lCompare = iLatest ? -1 : 1; // else case means "Earliest"
    if ((iTimer.getSunInfo(iSunInfo)->hour - lHour) * lCompare > 0)
    {
        lHour = iTimer.getSunInfo(iSunInfo)->hour;
        lMinute = iTimer.getSunInfo(iSunInfo)->minute;
    }
    else if (iTimer.getSunInfo(iSunInfo)->hour == lHour && (iTimer.getSunInfo(iSunInfo)->minute - lMinute) * lCompare > 0)
    {
        lMinute = iTimer.getSunInfo(iSunInfo)->minute;
    }
    bool lResult = checkTimerTime(iTimer, iTimerIndex, iBitfield, lHour, lMinute, iSkipWeekday, iHandleAsSunday);
    return lResult;
}

bool LogicChannel::checkSunDegree(Timer &iTimer, uint8_t iSunInfo, uint8_t iTimerIndex, uint16_t iBitfield, bool iSkipWeekday, bool iHandleAsSunday, bool iDown)
{
    uint8_t lDegree = ((iBitfield & 0x7E00) >> 9); 
    uint8_t lMinute = ((iBitfield & 0x01F8) >> 3);
    sTime lTime;
    iTimer.getSunDegree(iSunInfo, (lDegree + lMinute / 60.0) * (iDown ? -1.0 : 1.0), &lTime);
    bool lResult = checkTimerTime(iTimer, iTimerIndex, iBitfield, lTime.hour, lTime.minute, iSkipWeekday, iHandleAsSunday);
    return lResult;
}

// implementing timer startup, especially rerun of missed timers (called timer restore state)
void LogicChannel::startTimerRestoreState()
{
    // check if current logic channel is a timer channel
    uint8_t lLogicFunction = ParamLOG_fDisable ? 0 : ParamLOG_fLogic;
    if (lLogicFunction == VAL_Logic_Timer)
    {
        bool lShouldRestoreState = ((getByteParam(LOG_fTRestoreState) >> LOG_fTRestoreState_Shift) & LOG_fTRestoreState_Mask) ;
        if (lShouldRestoreState == 1)
        {
            // Timers with vacation handling cannot be restored
            bool lIsUsingVacation = ((getByteParam(LOG_fTVacation) >> LOG_fTVacation_Shift) & LOG_fTVacation_Mask) <= VAL_Tim_Special_No;
            if (lIsUsingVacation)
            {
                pCurrentPipeline |= PIP_TIMER_RESTORE_STATE;
                pCurrentPipeline &= ~PIP_TIMER_RESTORE_STEP; // ensure first processing step is set to 1
            }
            logInfo("LogicChannel", "TimerRestore activated for channel %d", channelIndex() + 1);
        }
    }
}

// remove timer restore flag
void LogicChannel::stopTimerRestoreState()
{
    pCurrentPipeline &= ~(PIP_TIMER_RESTORE_STATE | PIP_TIMER_RESTORE_STEP);
}

// Restores the value for this timer, if the day fits
void LogicChannel::processTimerRestoreState(TimerRestore &iTimer)
{
    bool lIsYearTimer = ParamLOG_fTYearDay;
    uint8_t lCountTimer = lIsYearTimer ? 4 : 8; // there are 4 year timer or 8 day timer
    bool lToday;                                // if it is a day timer lToday=true
    int16_t lResult = -1;
    bool lValue = false;
    bool lEvaluate = false;

    if (iTimer.isTimerValid() != tmValid)
        return;

    // ensure, that this is just executed once per restore day
    // we flag the execution according to the last bit of iteration counter
    // as long as this is equal, the restore was already executed
    bool lStepMarker = (pCurrentPipeline & PIP_TIMER_RESTORE_STEP);
    bool lIterationIndicator = (iTimer.getDayIteration() & 1);
    if (lStepMarker == lIterationIndicator)
        return;

    // toggle restore step bit to indicate, that this timer was checked for this day
    pCurrentPipeline &= ~PIP_TIMER_RESTORE_STEP;
    if (lIterationIndicator)
        pCurrentPipeline |= PIP_TIMER_RESTORE_STEP;

    int16_t lDayTime = iTimer.getHour() * 100 + iTimer.getMinute();

    logInfo("LogicChannel", "Processing TimerRestore on Channel %d for Day %02d.%02d.%02d", channelIndex() + 1, iTimer.getDay(), iTimer.getMonth(), iTimer.getYear());
    // first we process settings valid for whole timer
    // vacation is not processed (always skipped)

    // holiday
    uint8_t lHolidaySetting = ParamLOG_fTHoliday;
    if (lHolidaySetting == VAL_Tim_Special_No && (iTimer.holidayToday() > 0))
        lEvaluate = false;
    if (lHolidaySetting == VAL_Tim_Special_Skip || lHolidaySetting == VAL_Tim_Special_Sunday)
        lEvaluate = true;
    if (lHolidaySetting == VAL_Tim_Special_Only)
        lEvaluate = (iTimer.holidayToday() > 0);
    if (!lEvaluate)
        return;

    bool lHandleAsSunday = (lHolidaySetting == VAL_Tim_Special_Sunday && (iTimer.holidayToday() > 0));

    // loop through all timer
    uint32_t lTimerFunctions = getIntParam(LOG_fTd1DuskDawn);
    for (uint8_t lTimerIndex = 0; lTimerIndex < lCountTimer; lTimerIndex++)
    {
        // get timer function code
        uint8_t lTimerFunction = (lTimerFunctions >> (28 - lTimerIndex * 4)) & 0xF;
        if (lTimerFunction)
        {
            // timer function is active
            lToday = !lIsYearTimer || checkTimerToday(iTimer, lTimerIndex, lHandleAsSunday);
            if (lToday)
            {
                uint16_t lBitfield = getWordParam(LOG_fTd1Value + 2 * lTimerIndex);
                bool lCurrentValue = lBitfield & 0x8000;
                int16_t lCurrentResult = -1;

                // at this point we know, that this timer is valid for this day
                // now we get the right switch time for that day

                switch (lTimerFunction)
                {
                    case VAL_Tim_PointInTime:
                        lCurrentResult = getPointInTime(iTimer, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday);
                        if (lCurrentResult > -1)
                            logInfo("LogicChannel", "TimerRestore: Found PointInTime %04d with value %d", lCurrentResult, lCurrentValue);
                        break;
                    case VAL_Tim_Sunrise_Plus:
                        lCurrentResult = getSunAbs(iTimer, SUN_SUNRISE, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, false);
                        if (lCurrentResult > -1)
                            logInfo("LogicChannel", "TimerRestore: Found SunrisePlus %04d with value %d", lCurrentResult, lCurrentValue);
                        break;
                    case VAL_Tim_Sunrise_Minus:
                        lCurrentResult = getSunAbs(iTimer, SUN_SUNRISE, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, true);
                        if (lCurrentResult > -1)
                            logInfo("LogicChannel", "TimerRestore: Found SunriseMinus %04d with value %d", lCurrentResult, lCurrentValue);
                        break;
                    case VAL_Tim_Sunset_Plus:
                        lCurrentResult = getSunAbs(iTimer, SUN_SUNSET, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, false);
                        if (lCurrentResult > -1)
                            logInfo("LogicChannel", "TimerRestore: Found SunsetPlus %04d with value %d", lCurrentResult, lCurrentValue);
                        break;
                    case VAL_Tim_Sunset_Minus:
                        lCurrentResult = getSunAbs(iTimer, SUN_SUNSET, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, true);
                        if (lCurrentResult > -1)
                            logInfo("LogicChannel", "TimerRestore: Found SunsetMinus %04d with value %d", lCurrentResult, lCurrentValue);
                        break;
                    case VAL_Tim_Sunrise_Earliest:
                        lCurrentResult = getSunLimit(iTimer, SUN_SUNRISE, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, false);
                        if (lCurrentResult > -1)
                            logInfo("LogicChannel", "TimerRestore: Found SunriseEarliest %04d with value %d", lCurrentResult, lCurrentValue);
                        break;
                    case VAL_Tim_Sunrise_Latest:
                        lCurrentResult = getSunLimit(iTimer, SUN_SUNRISE, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, true);
                        if (lCurrentResult > -1)
                            logInfo("LogicChannel", "TimerRestore: Found SunriseLatest %04d with value %d", lCurrentResult, lCurrentValue);
                        break;
                    case VAL_Tim_Sunset_Earliest:
                        lCurrentResult = getSunLimit(iTimer, SUN_SUNSET, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, false);
                        if (lCurrentResult > -1)
                            logInfo("LogicChannel", "TimerRestore: Found SunsetEarliest %04d with value %d", lCurrentResult, lCurrentValue);
                        break;
                    case VAL_Tim_Sunset_Latest:
                        lCurrentResult = getSunLimit(iTimer, SUN_SUNSET, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, true);
                        if (lCurrentResult > -1)
                            logInfo("LogicChannel", "TimerRestore: Found SunsetLatest %04d with value %d\n", lCurrentResult, lCurrentValue);
                        break;
                    case VAL_Tim_Sunrise_DegreeUp:
                        lCurrentResult = getSunDegree(sTimer, SUN_SUNRISE, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, false);
                        if (lCurrentResult > -1)
                            logInfo("LogicChannel", "TimerRestore: Found SunriseDegreeUp %04d with value %d\n", lCurrentResult, lCurrentValue);
                        break;
                    case VAL_Tim_Sunset_DegreeUp:
                        lCurrentResult = getSunDegree(sTimer, SUN_SUNSET, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, false);
                        if (lCurrentResult > -1)
                            logInfo("LogicChannel", "TimerRestore: Found SunsetDegreeUp %04d with value %d\n", lCurrentResult, lCurrentValue);
                        break;
                    case VAL_Tim_Sunrise_DegreeDown:
                        lCurrentResult = getSunDegree(sTimer, SUN_SUNRISE, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, true);
                        if (lCurrentResult > -1)
                            logInfo("LogicChannel", "TimerRestore: Found SunriseDegreeDown %04d with value %d\n", lCurrentResult, lCurrentValue);
                        break;
                    case VAL_Tim_Sunset_DegreeDown:
                        lCurrentResult = getSunDegree(sTimer, SUN_SUNSET, lTimerIndex, lBitfield, lIsYearTimer, lHandleAsSunday, true);
                        if (lCurrentResult > -1)
                            logInfo("LogicChannel", "TimerRestore: Found SunsetDegreeDown %04d with value %d\n", lCurrentResult, lCurrentValue);
                        break;
                    default:
                        break;
                }
                // the time found in timer is taken, if
                //   it is greater than the last found time
                //   and smaller that the time of the processed day
                // important: lDayTime is for today the current time, for any older day 2359 (End-Of-Day)
                if (lCurrentResult > lResult && lCurrentResult <= lDayTime)
                {
                    lResult = lCurrentResult;
                    lValue = lCurrentValue;
                }
            }
        }
    }
    if (lResult > -1)
    {
        logInfo("LogicChannel", "TimerRestore: Found timer %04d with value %d, starting processing", lResult, lValue);
        startLogic(BIT_EXT_INPUT_2, lValue);
        // we also add that this input was used and is now valid
        pValidActiveIO |= BIT_EXT_INPUT_2;
        stopTimerRestoreState();
    }
    else
    {
        logInfo("LogicChannel", "TimerRestore: There are no timers for this day");
    }
}

int16_t LogicChannel::getTimerTime(Timer &iTimer, uint8_t iTimerIndex, uint16_t iBitfield, uint8_t iHour, uint8_t iMinute, bool iSkipWeekday, bool iHandleAsSunday)
{
    int16_t lResult = -1;

    // check correct timer index
    if (iTimerIndex < 8)
    {
        if (iSkipWeekday || checkWeekday(iTimer, iBitfield & 0x7, iHandleAsSunday))
        {
            if (iHour == 31)
                iHour = iTimer.getHour();
            if (iMinute == 63)
                iMinute = iTimer.getMinute();
            lResult = iHour * 100 + iMinute;
        }
    }
    return lResult;
}

int16_t LogicChannel::getPointInTime(Timer &iTimer, uint8_t iTimerIndex, uint16_t iBitfield, bool iSkipWeekday, bool iHandleAsSunday)
{
    uint8_t lHour = (iBitfield & 0x3E00) >> 9;
    uint8_t lMinute = (iBitfield & 0x01F8) >> 3;
    int16_t lResult = getTimerTime(iTimer, iTimerIndex, iBitfield, lHour, lMinute, iSkipWeekday, iHandleAsSunday);
    return lResult;
}

int16_t LogicChannel::getSunAbs(Timer &iTimer, uint8_t iSunInfo, uint8_t iTimerIndex, uint16_t iBitfield, bool iSkipWeekday, bool iHandleAsSunday, bool iMinus)
{
    int8_t lFactor = (iMinus) ? -1 : 1;
    uint8_t lHour = (iTimer.getSunInfo(iSunInfo)->hour + ((iBitfield & 0x3E00) >> 9) * lFactor) % 24;
    uint8_t lMinute = (iTimer.getSunInfo(iSunInfo)->minute + ((iBitfield & 0x01F8) >> 3) * lFactor) % 60;
    int16_t lResult = getTimerTime(iTimer, iTimerIndex, iBitfield, lHour, lMinute, iSkipWeekday, iHandleAsSunday);
    return lResult;
}

int16_t LogicChannel::getSunLimit(Timer &iTimer, uint8_t iSunInfo, uint8_t iTimerIndex, uint16_t iBitfield, bool iSkipWeekday, bool iHandleAsSunday, bool iLatest)
{
    uint8_t lHour = ((iBitfield & 0x3E00) >> 9);
    uint8_t lMinute = ((iBitfield & 0x01F8) >> 3);
    int8_t lCompare = iLatest ? -1 : 1; // else case means "Earliest"
    if ((iTimer.getSunInfo(iSunInfo)->hour - lHour) * lCompare > 0)
    {
        lHour = iTimer.getSunInfo(iSunInfo)->hour;
        lMinute = iTimer.getSunInfo(iSunInfo)->minute;
    }
    else if (iTimer.getSunInfo(iSunInfo)->hour == lHour && (iTimer.getSunInfo(iSunInfo)->minute - lMinute) * lCompare > 0)
    {
        lMinute = iTimer.getSunInfo(iSunInfo)->minute;
    }
    int16_t lResult = getTimerTime(iTimer, iTimerIndex, iBitfield, lHour, lMinute, iSkipWeekday, iHandleAsSunday);
    return lResult;
}

int16_t LogicChannel::getSunDegree(Timer &iTimer, uint8_t iSunInfo, uint8_t iTimerIndex, uint16_t iBitfield, bool iSkipWeekday, bool iHandleAsSunday, bool iDown)
{
    uint8_t lDegree = ((iBitfield & 0x7E00) >> 9); 
    uint8_t lMinute = ((iBitfield & 0x01F8) >> 3);
    sTime lTime;
    iTimer.getSunDegree(iSunInfo, (lDegree + lMinute / 60.0) * (iDown ? -1.0 : 1.0), &lTime);
    int16_t lResult = getTimerTime(iTimer, iTimerIndex, iBitfield, lTime.hour, lTime.minute, iSkipWeekday, iHandleAsSunday);
    return lResult;
}

const std::string LogicChannel::name()
{
    return "Logic";
}