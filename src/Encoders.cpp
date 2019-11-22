#include "Encoders.h"
#include "ch.h"
#include "hal.h"

__extension__ static QEIConfig qeicfg1 = {
		.mode = QEI_MODE_QUADRATURE,
		.resolution = QEI_BOTH_EDGES,
		.dirinv = QEI_DIRINV_FALSE,
		.overflow = QEI_OVERFLOW_WRAP,
		.min = 0,
		.max = 0,
		.notify_cb = nullptr,
		.overflow_cb = nullptr
};

static QEIConfig qeicfg2 = {
		.mode = QEI_MODE_QUADRATURE,
		.resolution = QEI_BOTH_EDGES,
		.dirinv = QEI_DIRINV_FALSE,
		.overflow = QEI_OVERFLOW_WRAP,
		.min = 0,
		.max = 0,
		.notify_cb = nullptr,
		.overflow_cb = nullptr
};


Encoders::Encoders(bool invertEncoder1, bool invertEncoder2, float encoder1Ratio, float encoder2Ratio)
{
	m_invertEncoder1 = invertEncoder1;
	m_invertEncoder2 = invertEncoder2;
	m_encoder1Ratio = encoder1Ratio;
	m_encoder2Ratio = encoder2Ratio;
}

Encoders::~Encoders()
{
}

void Encoders::init()
{
	// Encoder 1
	palSetPadMode(GPIOA, 7, PAL_MODE_ALTERNATE(2)); //TIM3_chan2
	palSetPadMode(GPIOC, 6, PAL_MODE_ALTERNATE(2)); //TIM3_chan1
	qeiStart(&QEID3, &qeicfg1);

	// Encoder 2
	palSetPadMode(GPIOA, 1, PAL_MODE_ALTERNATE(1)); //TIM2_chan1
	palSetPadMode(GPIOA, 0, PAL_MODE_ALTERNATE(1)); //TIM2_chan2
	qeiStart(&QEID2, &qeicfg2);
}

void Encoders::start()
{
	qeiEnable (&QEID3);
	qeiEnable (&QEID2);
}

void Encoders::stop()
{
	qeiDisable(&QEID3);
	qeiDisable(&QEID2);
}

void Encoders::getValuesAndReset(int16_t *encoder1, int16_t *encoder2)
{
	*encoder1 = qeiGetCount(&QEID2);
	*encoder2 = qeiGetCount(&QEID3);

	qei_lld_set_count(&QEID2, 0);
	qei_lld_set_count(&QEID3, 0);

	*encoder1 = int16_t((float)(*encoder1)*m_encoder1Ratio);
	*encoder2 = int16_t((float)(*encoder2)*m_encoder2Ratio);


	if(m_invertEncoder1)
		*encoder1 = -*encoder1;
	if(m_invertEncoder2)
		*encoder2 = -*encoder2;
}