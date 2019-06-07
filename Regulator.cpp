/*
 * Regulator.cpp
 *
 *  Created on: 8 mai 2019
 *      Author: jeff
 */

#include "Regulator.h"

Regulator::Regulator(float Kp, float max_output)
{
	m_accumulator = 0;
	m_Kp = Kp;
	m_error = 0;
	m_output = 0;
	m_maxOutput = max_output;
}

void Regulator::updateFeedback(float feedback)
{
	m_accumulator += feedback;
}


float Regulator::updateOutput(float goal)
{
	m_error = goal-m_accumulator;
	m_output =  m_error*m_Kp;

	if(m_output < -m_maxOutput)
		m_output = -m_maxOutput;
	else if(m_output > m_maxOutput)
		m_output = m_maxOutput;

	return m_output;
}
