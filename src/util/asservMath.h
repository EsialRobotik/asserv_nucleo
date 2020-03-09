#ifndef SRC_UTIL_ASSERVMATH_H_
#define SRC_UTIL_ASSERVMATH_H_


#define M_PI (3.14159265358979323846264338327950288)
#define M_2PI (2.0*M_PI)


/*
 * Remap value contenue dans [inMin;inMax] dans [outMin;outMax]
 * à la façon de ce qui existe dans la lib arduino : https://www.arduino.cc/reference/en/language/functions/math/map/
 */
inline float fmap(float value, float inMin, float inMax, float outMin, float outMax) {
    return (value - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

#endif /* SRC_UTIL_ASSERVMATH_H_ */