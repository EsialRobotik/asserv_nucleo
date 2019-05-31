/*
 * AsservMain.cpp
 *
 *  Created on: 4 mai 2019
 *      Author: jeff
 */

#include "AsservMain.h"
#include "ch.h"
#include "hal.h"

#define M_PI (3.14159265358979323846)
#define M_2PI (2.0*M_PI)


#define ASSERV_THREAD_PERIOD_MS (5)
#define ASSERV_THREAD_PERIOD_S (float(ASSERV_THREAD_PERIOD_MS)/1000.0)
#define ASSERV_POSITION_DIVISOR (5)


AsservMain::AsservMain(float wheelRadius_mm, float encoderWheelsDistance_mm):
m_motorController(false,true),
m_encoders(true,true, 1 , 1),
m_odometrie(encoderWheelsDistance_mm),
m_speedControllerRight(0.25, 0.45, 100, 1000, 30, 1.0/ASSERV_THREAD_PERIOD_S),
m_speedControllerLeft(0.25, 0.45 , 100, 1000, 30, 1.0/ASSERV_THREAD_PERIOD_S),
m_angleRegulator(1100),
m_distanceRegulator(10)
{
	m_encoderWheelsDistance_mm = encoderWheelsDistance_mm;
	m_distanceByEncoderTurn_mm = M_2PI*wheelRadius_mm;
	m_encodermmByTicks = m_distanceByEncoderTurn_mm/4096.0;
	m_encoderWheelsDistance_ticks = m_encoderWheelsDistance_mm / m_encodermmByTicks;
	m_angleGoal=0;
	m_distanceGoal = 0;
	m_asservCounter = 0;
	m_enableMotors = false;
	m_enablePolar = true;
}

AsservMain::~AsservMain()
{
}


void AsservMain::init()
{
	m_motorController.init();
	m_encoders.init();
	m_encoders.start();
	USBStream::init();
}


float AsservMain::estimateSpeed(int16_t deltaCount)
{
	const float ticksByTurn = 1024*4;
	const float dt = ASSERV_THREAD_PERIOD_S;

	float deltaAngle_nbTurn = ((float)deltaCount/(float)ticksByTurn);
	float speed_nbTurnPerSec = deltaAngle_nbTurn / dt;

	// Speed returned in mm/sec
	return speed_nbTurnPerSec*m_distanceByEncoderTurn_mm;
}

float AsservMain::estimateDeltaAngle(int16_t deltaCountRight, int16_t deltaCountLeft )
{
	// in rad
    return float(deltaCountRight-deltaCountLeft)  / m_encoderWheelsDistance_ticks ;
}

float AsservMain::estimateDeltaDistance(int16_t deltaCountRight, int16_t deltaCountLeft )
{
	// in mm
    return float(deltaCountRight+deltaCountLeft) * (1.0/2.0) * m_encodermmByTicks;
}

void AsservMain::mainLoop()
{
	systime_t time = chVTGetSystemTime();
	time += TIME_MS2I(ASSERV_THREAD_PERIOD_MS);
	while (true)
	{
		int16_t m_encoderDeltaRight;
		int16_t m_encoderDeltaLeft;
		m_encoders.getValuesAndReset(&m_encoderDeltaRight, &m_encoderDeltaLeft);

		// angle regulation
		float deltaAngle_radian = estimateDeltaAngle(m_encoderDeltaLeft, m_encoderDeltaRight);
		float angleConsign = m_angleRegulator.update(m_angleGoal, deltaAngle_radian);

		// distance regulation
		float deltaDistance_mm = estimateDeltaDistance(m_encoderDeltaLeft, m_encoderDeltaRight);
		float distanceConsign = m_distanceRegulator.update(m_distanceGoal, deltaDistance_mm);

		// Compute speed consign every ASSERV_POSITION_DIVISOR
		if( m_asservCounter == ASSERV_POSITION_DIVISOR && m_enablePolar)
		{
			setMotorsSpeed(
					distanceConsign+angleConsign,
					distanceConsign-angleConsign);
			m_asservCounter=0;
		}

		m_odometrie.refresh(
				m_encoderDeltaRight*m_encodermmByTicks,
				m_encoderDeltaLeft*m_encodermmByTicks);


		// Speed regulation
		float estimatedSpeedRight = estimateSpeed(m_encoderDeltaRight);
		float estimatedSpeedLeft = estimateSpeed(m_encoderDeltaLeft);

		float outputSpeedRight = m_speedControllerRight.update(estimatedSpeedRight);
		float outputSpeedLeft = m_speedControllerLeft.update(estimatedSpeedLeft);

		if(m_enableMotors)
		{
			m_motorController.setMotor2Speed(outputSpeedRight);
			m_motorController.setMotor1Speed(outputSpeedLeft);
		}
		else
		{
			m_motorController.setMotor2Speed(0);
			m_motorController.setMotor1Speed(0);
		}


		USBStream::instance()->setSpeedEstimatedRight(estimatedSpeedRight);
		USBStream::instance()->setSpeedEstimatedLeft(estimatedSpeedLeft);
		USBStream::instance()->setSpeedGoalRight(m_speedControllerRight.getSpeedGoal());
		USBStream::instance()->setSpeedGoalLeft(m_speedControllerLeft.getSpeedGoal());
		USBStream::instance()->setSpeedOutputRight(outputSpeedRight);
		USBStream::instance()->setSpeedOutputLeft(outputSpeedLeft);
		USBStream::instance()->setSpeedIntegratedOutputRight(m_speedControllerRight.getIntegratedOutput());
		USBStream::instance()->setSpeedIntegratedOutputLeft(m_speedControllerLeft.getIntegratedOutput());
		USBStream::instance()->setLimitedSpeedGoalRight(m_speedControllerRight.getLimitedSpeedGoal());
		USBStream::instance()->setLimitedSpeedGoalLeft(m_speedControllerLeft.getLimitedSpeedGoal());

		USBStream::instance()->setAngleGoal(m_angleGoal);
		USBStream::instance()->setAngleAccumulator(m_angleRegulator.getAccumulator());
		USBStream::instance()->setAngleOutput(angleConsign);


		USBStream::instance()->setDistGoal(m_distanceGoal);
		USBStream::instance()->setDistAccumulator(m_distanceRegulator.getAccumulator());
		USBStream::instance()->setDistOutput(distanceConsign);

		USBStream::instance()->setOdoX(m_odometrie.getX());
		USBStream::instance()->setOdoY(m_odometrie.getY());
		USBStream::instance()->setOdoTheta(m_odometrie.getTheta());

		USBStream::instance()->SendCurrentStream();

		m_asservCounter++;
		chThdSleepUntil(time);
		time += TIME_MS2I(ASSERV_THREAD_PERIOD_MS);
	}
}

void AsservMain::setMotorsSpeed(float motorLeft, float motorRight)
{
	m_speedControllerRight.setSpeedGoal(motorRight);
	m_speedControllerLeft.setSpeedGoal(motorLeft);
}

void AsservMain::enableMotors(bool enable)
{
	m_enableMotors = enable;
	if(enable)
	{
		// In this case, reseting integrators are useful ...
		m_speedControllerLeft.resetIntegral();
		m_speedControllerRight.resetIntegral();
	}
}

void AsservMain::enablePolar(bool enable)
{
	m_enablePolar = enable;
}


