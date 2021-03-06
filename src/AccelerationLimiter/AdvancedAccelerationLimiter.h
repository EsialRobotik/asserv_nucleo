#ifndef SRC_ACCELERATIONLIMITER_ADVANCEDACCELERATIONLIMITER_H_
#define SRC_ACCELERATIONLIMITER_ADVANCEDACCELERATIONLIMITER_H_

#include "AbstractAccelerationLimiter.h"

class AdvancedAccelerationLimiter : public AbstractAccelerationLimiter
{

public:
    explicit AdvancedAccelerationLimiter(float maxAcceleration, float minAcceleration, float highSpeedThreshold);
    virtual ~AdvancedAccelerationLimiter(){};

    void setMaxAcceleration(float maxAcceleration);
    void setMinAcceleration(float minAcceleration);
    void setHighSpeedThreshold(float highSpeedThreshold);

    inline float getMaxAcceleration() const { return m_maxAcceleration; };
    inline float getMinAcceleration() const { return m_minAcceleration; };
    inline float getHighSpeedThreshold() const{ return m_HighSpeedThreshold; };

private:
    virtual float limitOutput(float dt, float targetSpeed, float previousOutput, float currentSpeed);

    float m_maxAcceleration;
    float m_minAcceleration;
    float m_HighSpeedThreshold;
};

#endif /* SRC_ACCELERATIONLIMITER_ADVANCEDACCELERATIONLIMITER_H_ */
