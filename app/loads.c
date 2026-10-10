#include "board.h"
#include "hal.h"

/* The load switches (hal.h): two drive outputs, and an analog current-sense
 * input each, read by the ADC against VDDANA. A drive that is PIN_NONE is
 * left alone; the commanded state is still kept and reported. */

static const uint8_t drives[2] = { PIN_DRIVE_1, PIN_DRIVE_2 };
static const uint8_t senses[2] = { PIN_SENSE_1, PIN_SENSE_2 };
static const uint8_t inputs[2] = { AIN_SENSE_1, AIN_SENSE_2 };
static uint8_t commanded;

static void adc_sync(void)
{
    while (ADC->STATUS.reg & ADC_STATUS_SYNCBUSY) {
    }
}

static uint16_t adc_convert(void)
{
    ADC->INTFLAG.reg = ADC_INTFLAG_RESRDY;
    ADC->SWTRIG.reg = ADC_SWTRIG_START;
    while (!(ADC->INTFLAG.reg & ADC_INTFLAG_RESRDY)) {
    }
    return (uint16_t)ADC->RESULT.reg;
}

static void adc_init(void)
{
    PM->APBCMASK.reg |= PM_APBCMASK_ADC;
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_ADC | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_CLKEN;
    while (GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY) {
    }
    /* Factory calibration from the NVM software calibration row. */
    const uint32_t bias = (*(const uint32_t *)ADC_FUSES_BIASCAL_ADDR & ADC_FUSES_BIASCAL_Msk)
                        >> ADC_FUSES_BIASCAL_Pos;
    const uint32_t linearity =
        ((*(const uint32_t *)ADC_FUSES_LINEARITY_0_ADDR & ADC_FUSES_LINEARITY_0_Msk)
         >> ADC_FUSES_LINEARITY_0_Pos)
        | (((*(const uint32_t *)ADC_FUSES_LINEARITY_1_ADDR & ADC_FUSES_LINEARITY_1_Msk)
            >> ADC_FUSES_LINEARITY_1_Pos) << 5);
    ADC->CALIB.reg = (uint16_t)(ADC_CALIB_BIAS_CAL(bias) | ADC_CALIB_LINEARITY_CAL(linearity));

    /* Reference VDDANA/2 with gain 1/2: 0 V to VDDANA. 48 MHz / 32 = 1.5 MHz.
     * Sixteen samples averaged back to 12 bits (averaging needs RESSEL 16). */
    ADC->REFCTRL.reg = ADC_REFCTRL_REFSEL_INTVCC1;
    ADC->AVGCTRL.reg = ADC_AVGCTRL_SAMPLENUM_16 | ADC_AVGCTRL_ADJRES(4);
    ADC->SAMPCTRL.reg = ADC_SAMPCTRL_SAMPLEN(8);
    ADC->CTRLB.reg = ADC_CTRLB_PRESCALER_DIV32 | ADC_CTRLB_RESSEL_16BIT;
    adc_sync();
    ADC->INPUTCTRL.reg = ADC_INPUTCTRL_GAIN_DIV2 | ADC_INPUTCTRL_MUXNEG_GND
                       | ADC_INPUTCTRL_MUXPOS(inputs[0]);
    adc_sync();
    ADC->CTRLA.reg = ADC_CTRLA_ENABLE;
    adc_sync();
    (void)adc_convert();   /* the first result after enabling is not to be used */
}

void loads_init(void)
{
    /* Off from the first moment we drive them: a link loss or crash that
     * resets the unit leaves the loads unpowered. */
    commanded = 0u;
    for (uint8_t index = 0u; index < 2u; index++) {
        if (drives[index] != PIN_NONE) {
            pin_write(drives[index], false);
            pin_output(drives[index]);
        }
        pin_analog(senses[index]);
    }
    adc_init();
}

void loads_set(uint8_t outputs)
{
    commanded = (uint8_t)(outputs & 3u);
    for (uint8_t index = 0u; index < 2u; index++) {
        if (drives[index] != PIN_NONE) {
            pin_write(drives[index], ((commanded >> index) & 1u) != 0u);
        }
    }
}

uint8_t loads_outputs(void)
{
    return commanded;
}

uint16_t loads_sense_mv(uint8_t load)
{
    ADC->INPUTCTRL.bit.MUXPOS = inputs[load];
    adc_sync();
    return (uint16_t)(((uint32_t)adc_convert() * VDDANA_MV + 2047u) / 4095u);
}
