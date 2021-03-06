/**************************************************************************/
/*!
 @file     ams_as5048b.cpp
 @author   SOSAndroid (E. Ha.)
 @license  BSD

 Library to interface the AS5048B magnetic rotary encoder from AMS over the I2C bus

 @section  HISTORY

 v1.0.0 - First release
 v1.0.1 - Typo to allow compiling on Codebender.cc (Math.h vs math.h)
 v1.0.2 - setZeroReg() issue raised by @MechatronicsWorkman
 v1.0.3 - Small bug fix and improvement by @DavidHowlett
 +Modifications PMX to use ChibiOS I2c features
 */
/**************************************************************************/

#include <stdlib.h>
#include <math.h>
#include "../util/asservMath.h"
#include "ams_as5048b.h"
#include "string.h"
#include "ch.h"
#include "hal.h"
#include <chprintf.h>

extern BaseSequentialStream *outputStream;

/*========================================================================*/
/*                            CONSTRUCTORS                                */
/*========================================================================*/

/**************************************************************************/
/*!
 Constructor
 */
/**************************************************************************/

AMS_AS5048B::AMS_AS5048B(uint8_t chipAddress)
{
    connected_ = 0;
    _chipAddress = chipAddress;
    _debugFlag = false;
    _zeroRegVal = 0;
    _addressRegVal = 0;
    _clockWise = false;
    _movingAvgCountLoop = 0;
    _movingAvgExpCos = 0.0;
    _movingAvgExpSin = 0.0;
    _movingAvgExpAngle = 0.0;
    _movingAvgExpAlpha = 0.0;
    _lastAngleRaw = 0.0;
}

/*========================================================================*/
/*                           PUBLIC FUNCTIONS                             */
/*========================================================================*/

/**************************************************************************/
/*!
 @brief  ping

 @params
 none
 @returns
 none
 */
/**************************************************************************/
int AMS_AS5048B::ping()
{
    msg_t msg = MSG_OK;
    uint8_t cmd[] = { 0x00 };
    i2cAcquireBus (&I2CD2);
    msg = i2cMasterTransmitTimeout(&I2CD2, _chipAddress, cmd, 1, NULL, 0, TIME_MS2I(1));
    //chprintf(outputStream, "msg=%d\r\n", msg);
    i2cReleaseBus(&I2CD2);

    if (msg == MSG_OK)
        return 0;
    else
        return -1;
}

/**************************************************************************/
/*!
 @brief  init values and overall behaviors for AS5948B use

 @params
 none
 @returns
 none
 */
/**************************************************************************/

int AMS_AS5048B::begin(void)
{
    if (ping()<0)
        return -1;
    connected_ = 1;
    _clockWise = false;
    _lastAngleRaw = 0.0;
    reset();
    return 0;
}

/**************************************************************************/
/*!
 @brief  reset values and overall behaviors for AS5948B use

 @params
 none
 @returns
 none
 */
/**************************************************************************/
void AMS_AS5048B::reset(void)
{
    _lastAngleRaw = 0.0;
    _zeroRegVal = AMS_AS5048B::zeroRegR();
    _addressRegVal = AMS_AS5048B::addressRegR();
    AMS_AS5048B::resetMovingAvgExp();
}
/**************************************************************************/
/*!
 *
 *
 @brief  Toggle debug output to serial

 @params
 none
 @returns
 none
 */
/**************************************************************************/
void AMS_AS5048B::toggleDebug(void)
{
    _debugFlag = !_debugFlag;
    return;
}

/**************************************************************************/
/*!
 @brief  Set / unset clock wise counting - sensor counts CCW natively

 @params[in]
 bool cw - true: CW, false: CCW
 @returns
 none
 */
/**************************************************************************/
void AMS_AS5048B::setClockWise(bool cw)
{
    _clockWise = cw;
    _lastAngleRaw = 0.0;
    AMS_AS5048B::resetMovingAvgExp();
    return;
}

/**************************************************************************/
/*!
 @brief  writes OTP control register

 @params[in]
 unit8_t register value
 @returns
 none
 */
/**************************************************************************/
void AMS_AS5048B::progRegister(uint8_t regVal)
{
    AMS_AS5048B::writeReg(AS5048B_PROG_REG, regVal);
    return;
}

/**************************************************************************/
/*!
 @brief  Burn values to the slave address OTP register

 @params[in]
 none
 @returns
 none
 */
/**************************************************************************/
void AMS_AS5048B::doProg(void)
{
    //enable special programming mode
    AMS_AS5048B::progRegister(0xFD);
    chThdSleepMilliseconds(10);

    //set the burn bit: enables automatic programming procedure
    AMS_AS5048B::progRegister(0x08);
    chThdSleepMilliseconds(10);

    //disable special programming mode
    AMS_AS5048B::progRegister(0x00);
    chThdSleepMilliseconds(10);

    return;
}

/**************************************************************************/
/*!
 @brief  Burn values to the zero position OTP register

 @params[in]
 none
 @returns
 none
 */
/**************************************************************************/
void AMS_AS5048B::doProgZero(void)
{
    //this will burn the zero position OTP register like described in the datasheet
    //enable programming mode
    AMS_AS5048B::progRegister(0x01);
    chThdSleepMilliseconds(10);

    //set the burn bit: enables automatic programming procedure
    AMS_AS5048B::progRegister(0x08);
    chThdSleepMilliseconds(10);

    //read angle information (equals to 0)
    AMS_AS5048B::readReg16(AS5048B_ANGLMSB_REG);
    chThdSleepMilliseconds(10);

    //enable verification
    AMS_AS5048B::progRegister(0x40);
    chThdSleepMilliseconds(10);

    //read angle information (equals to 0)
    AMS_AS5048B::readReg16(AS5048B_ANGLMSB_REG);
    chThdSleepMilliseconds(10);

    return;
}

/**************************************************************************/
/*!
 @brief  write I2C address value (5 bits) into the address register

 @params[in]
 unit8_t register value
 @returns
 none
 */
/**************************************************************************/
void AMS_AS5048B::addressRegW(uint8_t regVal)
{
    // write the new chip address to the register
    AMS_AS5048B::writeReg(AS5048B_ADDR_REG, regVal);

    // update our chip address with our 5 programmable bits
    // the MSB is internally inverted, so we flip the leftmost bit
    _chipAddress = ((regVal << 2) | (_chipAddress & 0b11)) ^ (1 << 6);
    return;
}

/**************************************************************************/
/*!
 @brief  reads I2C address register value

 @params[in]
 none
 @returns
 uint8_t register value
 */
/**************************************************************************/
uint8_t AMS_AS5048B::addressRegR(void)
{
    return AMS_AS5048B::readReg8(AS5048B_ADDR_REG);
}

/**************************************************************************/
/*!
 @brief  sets current angle as the zero position

 @params[in]
 none
 @returns
 none
 */
/**************************************************************************/
void AMS_AS5048B::setZeroReg(void)
{
    AMS_AS5048B::zeroRegW((uint16_t) 0x00); //Issue closed by @MechatronicsWorkman and @oilXander. The last sequence avoids any offset for the new Zero position
    uint16_t newZero = AMS_AS5048B::readReg16(AS5048B_ANGLMSB_REG);
    AMS_AS5048B::zeroRegW(newZero);
    return;
}

/**************************************************************************/
/*!
 @brief  writes the 2 bytes Zero position register value

 @params[in]
 unit16_t register value
 @returns
 none
 */
/**************************************************************************/
void AMS_AS5048B::zeroRegW(uint16_t regVal)
{
    AMS_AS5048B::writeReg(AS5048B_ZEROMSB_REG, (uint8_t) (regVal >> 6));
    AMS_AS5048B::writeReg(AS5048B_ZEROLSB_REG, (uint8_t) (regVal & 0x3F));
    return;
}

/**************************************************************************/
/*!
 @brief  reads the 2 bytes Zero position register value

 @params[in]
 none
 @returns
 uint16_t register value trimmed on 14 bits
 */
/**************************************************************************/
uint16_t AMS_AS5048B::zeroRegR(void)
{
    return AMS_AS5048B::readReg16(AS5048B_ZEROMSB_REG);
}

/**************************************************************************/
/*!
 @brief  reads the 2 bytes magnitude register value

 @params[in]
 none
 @returns
 uint16_t register value trimmed on 14 bits
 */
/**************************************************************************/
uint16_t AMS_AS5048B::magnitudeR(void)
{
    return AMS_AS5048B::readReg16(AS5048B_MAGNMSB_REG);
}

uint16_t AMS_AS5048B::angleRegR(void)
{
    return AMS_AS5048B::readReg16(AS5048B_ANGLMSB_REG);
}

/**************************************************************************/
/*!
 @brief  reads the 1 bytes auto gain register value

 @params[in]
 none
 @returns
 uint8_t register value
 */
/**************************************************************************/
uint8_t AMS_AS5048B::getAutoGain(void)
{
    return AMS_AS5048B::readReg8(AS5048B_GAIN_REG);
}

/**************************************************************************/
/*!
 @brief  reads the 1 bytes diagnostic register value

 @params[in]
 none
 @returns
 uint8_t register value
 */
/**************************************************************************/
uint8_t AMS_AS5048B::getDiagReg(void)
{
    return AMS_AS5048B::readReg8(AS5048B_DIAG_REG);
}

/**************************************************************************/
/*!
 @brief  reads current angle value and converts it into the desired unit

 @params[in]
 String unit : string expressing the unit of the angle. Sensor raw value as default
 @params[in]
 Bool newVal : have a new measurement or use the last read one. True as default
 @returns
 float angle value converted into the desired unit
 */
/**************************************************************************/
float AMS_AS5048B::angleR(int unit, bool newVal)
{
    float angleRaw;

    if (newVal) {
        if (_clockWise) {
            angleRaw = (float) (0b11111111111111 - AMS_AS5048B::readReg16(AS5048B_ANGLMSB_REG));
        } else {
            angleRaw = (float) AMS_AS5048B::readReg16(AS5048B_ANGLMSB_REG);
        }
        _lastAngleRaw = angleRaw;
    } else {
        angleRaw = _lastAngleRaw;
    }

    return AMS_AS5048B::convertAngle(unit, angleRaw);
}

/**************************************************************************/
/*!
 @brief  Performs an exponential moving average on the angle.
 Works on Sine and Cosine of the angle to avoid issues 0°/360° discontinuity

 @params[in]
 none
 @returns
 none
 */
/**************************************************************************/
void AMS_AS5048B::updateMovingAvgExp(void)
{
    //sine and cosine calculation on angles in radian
    float angle = AMS_AS5048B::angleR(U_RAD, true);

    if (_movingAvgCountLoop < EXP_MOVAVG_LOOP) {
        _movingAvgExpSin += sin(angle);
        _movingAvgExpCos += cos(angle);
        if (_movingAvgCountLoop == (EXP_MOVAVG_LOOP - 1)) {
            _movingAvgExpSin = _movingAvgExpSin / EXP_MOVAVG_LOOP;
            _movingAvgExpCos = _movingAvgExpCos / EXP_MOVAVG_LOOP;
        }
        _movingAvgCountLoop++;
    } else {
        float movavgexpsin = _movingAvgExpSin + _movingAvgExpAlpha * (sin(angle) - _movingAvgExpSin);
        float movavgexpcos = _movingAvgExpCos + _movingAvgExpAlpha * (cos(angle) - _movingAvgExpCos);
        _movingAvgExpSin = movavgexpsin;
        _movingAvgExpCos = movavgexpcos;
        _movingAvgExpAngle = getExpAvgRawAngle();
    }

    return;
}

/**************************************************************************/
/*!
 @brief  sent back the exponential moving averaged angle in the desired unit

 @params[in]
 String unit : string expressing the unit of the angle. Sensor raw value as default
 @returns
 float exponential moving averaged angle value
 */
/**************************************************************************/
float AMS_AS5048B::getMovingAvgExp(int unit)
{
    return AMS_AS5048B::convertAngle(unit, _movingAvgExpAngle);
}

void AMS_AS5048B::resetMovingAvgExp(void)
{
    _movingAvgExpAngle = 0.0;
    _movingAvgCountLoop = 0;
    _movingAvgExpAlpha = 2.0 / (EXP_MOVAVG_N + 1.0);
    return;
}

/*========================================================================*/
/*                           PRIVATE FUNCTIONS                            */
/*========================================================================*/

uint8_t AMS_AS5048B::readReg8(uint8_t address)
{
    uint8_t readValue = 0;
    readRegs(address, 1, &readValue);
    return readValue;
}

uint16_t AMS_AS5048B::readReg16(uint8_t address)
{
    //16 bit value got from 2 8bits registers (7..0 MSB + 5..0 LSB) => 14 bits value
    uint8_t readArray[2] = { 0 };
    uint16_t readValue = 0;
    readRegs(address, 2, readArray);
    readValue = (((uint16_t) readArray[0]) << 6);
    readValue += (readArray[1] & 0x3F);
    return readValue;
}

msg_t AMS_AS5048B::i2cMasterTransmitTimeoutTimes(I2CDriver *i2cp,
                               i2caddr_t addr,
                               const uint8_t *txbuf,
                               size_t txbytes,
                               uint8_t *rxbuf,
                               size_t rxbytes,
                               sysinterval_t timeout, int times)
{
    msg_t r;
   for(int i = 0; i <= times ; i++)
   {
       if (i2cp->state != I2C_READY)
       {
           i2cStart(i2cp, i2cp->config);
       }
       r = i2cMasterTransmitTimeout(i2cp, addr, txbuf, txbytes, rxbuf, rxbytes, timeout);
       if (r == MSG_OK)
           return r;
       else
           chprintf(outputStream,"...AMS_AS5048B::i2cMasterTransmitTimeoutTimes try... %d  \r\n", i);
       chThdSleepMilliseconds(1);
   }

   return r;
}

int AMS_AS5048B::readRegs(uint8_t address, uint8_t len, uint8_t* data)
{
    uint8_t cmd[] = { address };
    msg_t msg = MSG_OK;
    for (int i = 0; i < 3; i++) {
        i2cAcquireBus (&I2CD2);
        msg = i2cMasterTransmitTimeoutTimes(&I2CD2, _chipAddress, cmd, sizeof(cmd), data, len, TIME_MS2I(1),10);
        i2cReleaseBus(&I2CD2);
        //chprintf(outputStream, "i=%d\r\n", i);
        if (msg == MSG_OK)
            return 0;

    }
    return -1;
}

uint8_t AMS_AS5048B::getAllData(uint8_t *agc, uint8_t *diag, uint16_t *mag, uint16_t *raw)
{
    uint8_t data[6] = { 0, 0, 0, 0, 0, 0 };
    uint8_t r = readRegs(AS5048B_GAIN_REG, 6, data);

    *agc = data[0];
    *diag = data[1];
    *mag = ((uint16_t) (data[2]) << 6) + (data[3] & 0x3F);
    *raw = ((uint16_t) (data[4]) << 6) + (data[5] & 0x3F);
    return r;
}

int AMS_AS5048B::writeReg(uint8_t address, uint8_t value)
{

    uint8_t cmd[] = { address, value };
    int i = 0;
    msg_t msg = MSG_OK;
    do {
        i++;
        i2cAcquireBus (&I2CD2);
        msg = i2cMasterTransmitTimeout(&I2CD2, _chipAddress, cmd, sizeof(cmd), NULL, 0, TIME_MS2I(1));
        i2cReleaseBus(&I2CD2);
        //chprintf(outputStream, "i=%d\r\n", i);
        if (i > 10)
            return -1;
    } while (msg != MSG_OK );

    //chDbgAssert(msg == MSG_OK, "AMS_AS5048B - i2cMasterTransmitTimeout writeReg after 5 requests ERROR NOK\r\n");

    return 0;
}

float AMS_AS5048B::convertAngle(int unit, float angle)
{
    // convert raw sensor reading into angle unit
    float angleConv;

    switch (unit) {
    case U_RAW:
        //Sensor raw measurement
        angleConv = angle;
        break;
    case U_TRN:
        //full turn ratio
        angleConv = (angle / AS5048B_RESOLUTION);
        break;
    case U_DEG:
        //degree
        angleConv = (angle / AS5048B_RESOLUTION) * 360.0;
        break;
    case U_RAD:
        //Radian
        angleConv = (angle / AS5048B_RESOLUTION) * 2 * M_PI;
        break;
    case U_MOA:
        //minute of arc
        angleConv = (angle / AS5048B_RESOLUTION) * 60.0 * 360.0;
        break;
    case U_SOA:
        //second of arc
        angleConv = (angle / AS5048B_RESOLUTION) * 60.0 * 60.0 * 360.0;
        break;
    case U_GRAD:
        //grade
        angleConv = (angle / AS5048B_RESOLUTION) * 400.0;
        break;
    case U_MILNATO:
        //NATO MIL
        angleConv = (angle / AS5048B_RESOLUTION) * 6400.0;
        break;
    case U_MILSE:
        //Swedish MIL
        angleConv = (angle / AS5048B_RESOLUTION) * 6300.0;
        break;
    case U_MILRU:
        //Russian MIL
        angleConv = (angle / AS5048B_RESOLUTION) * 6000.0;
        break;
    default:
        //no conversion => raw angle
        angleConv = angle;
        break;
    }
    return angleConv;
}

float AMS_AS5048B::getExpAvgRawAngle(void)
{
    float angle;
    float twopi = 2 * M_PI;

    if (_movingAvgExpSin < 0.0) {
        angle = twopi - acos(_movingAvgExpCos);
    } else {
        angle = acos(_movingAvgExpCos);
    }

    angle = (angle / twopi) * AS5048B_RESOLUTION;

    return angle;
}

void AMS_AS5048B::printDebug(void)
{
    return;
}
