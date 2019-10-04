/***********************************************************************************************************************
 * Copyright [2019] Renesas Electronics Corporation and/or its affiliates.  All Rights Reserved.
 *
 * This software is supplied by Renesas Electronics America Inc. and may only be used with products of Renesas
 * Electronics Corp. and its affiliates ("Renesas").  No other uses are authorized.  This software is protected under
 * all applicable laws, including copyright laws. Renesas reserves the right to change or discontinue this software.
 * THE SOFTWARE IS DELIVERED TO YOU "AS IS," AND RENESAS MAKES NO REPRESENTATIONS OR WARRANTIES, AND TO THE FULLEST
 * EXTENT PERMISSIBLE UNDER APPLICABLE LAW,DISCLAIMS ALL WARRANTIES, WHETHER EXPLICITLY OR IMPLICITLY, INCLUDING
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT, WITH RESPECT TO THE SOFTWARE.
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, IN NO EVENT WILL RENESAS BE LIABLE TO YOU IN CONNECTION WITH THE SOFTWARE
 * (OR ANY PERSON OR ENTITY CLAIMING RIGHTS DERIVED FROM YOU) FOR ANY LOSS, DAMAGES, OR CLAIMS WHATSOEVER, INCLUDING,
 * WITHOUT LIMITATION, ANY DIRECT, CONSEQUENTIAL, SPECIAL, INDIRECT, PUNITIVE, OR INCIDENTAL DAMAGES; ANY LOST PROFITS,
 * OTHER ECONOMIC DAMAGE, PROPERTY DAMAGE, OR PERSONAL INJURY; AND EVEN IF RENESAS HAS BEEN ADVISED OF THE POSSIBILITY
 * OF SUCH LOSS, DAMAGES, CLAIMS OR COSTS.
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Includes
 **********************************************************************************************************************/
#include <string.h>
#include "r_dac.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/* D/A Control Register Mask */
/** Driver ID (DTC in ASCII), used to identify Digital to Analog Converter (DAC) configuration  */
#define DAC_OPEN                                     (0x44414300)
#define DAC_VREF_AVCC0_AVSS0                         (0x01U)
#define DAC_DAADSCR_REG_DAADST_BIT_POS               (0x07U)
#define DAC_DAADUSR_REG_MASK                         (0x02U)
#define DAC_DADPR_REG_DPSEL_BIT_POS                  (0x07U)
#define DAC_DAAMPCR_AMP_CTRL_BITS                    (0x06U) /* 6th bit for channel 0; 7th bit for channel 1 */
#define DAC_DACR_DAOE_BITS                           (0x06U) /* 6th bit for channel 0; 7th bit for channel 1 */
#define DAC_DAASWCR_DAASW0_MASK                      (0x40)
#define DAC_DAASWCR_DAASW1_MASK                      (0x80)
#define DAC_ADC_UNIT_1                               (0x01)

/* Conversion time with Output Amplifier. See hardware manual (see Table 60.44
 *'D/A conversion characteristics' of the RA6M3 manual R01UH0886EJ0100). */
#define DAC_CONVERSION_TIME_WITH_OUTPUT_AMPLIFIER    (0x04U) /* Unit: Microseconds. */

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Global variables
 **********************************************************************************************************************/

const dac_api_t g_dac_on_dac =
{
    .open       = R_DAC_Open,
    .write      = R_DAC_Write,
    .start      = R_DAC_Start,
    .stop       = R_DAC_Stop,
    .close      = R_DAC_Close,
    .versionGet = R_DAC_VersionGet
};

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

/** Version data structure used by error logger macro. */
static const fsp_version_t g_dac_version =
{
    .api_version_minor  = DAC_API_VERSION_MINOR,
    .api_version_major  = DAC_API_VERSION_MAJOR,
    .code_version_major = DAC_CODE_VERSION_MAJOR,
    .code_version_minor = DAC_CODE_VERSION_MINOR
};

/*******************************************************************************************************************//**
 * @addtogroup DAC
 * @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Functions
 **********************************************************************************************************************/

/******************************************************************************************************************//**
 * Perform required initialization described in hardware manual.  Implements dac_api_t::open.
 * Configures a single DAC channel, starts the channel, and provides a handle for use with the
 * DAC API Write and Close functions.  Must be called once prior to calling any other DAC API
 * functions.  After a channel is opened, Open should not be called again for the same channel
 * without calling Close first.
 *
 * @retval FSP_SUCCESS                     The channel was successfully opened.
 * @retval FSP_ERR_ASSERTION               Parameter check failure due to one or more reasons below:
 *                                         1. One or both of the following parameters may be NULL: p_api_ctrl or p_cfg
 *                                         2. data_format value in p_cfg is out of range.
 *                                         3. Extended configuration structure is set to NULL for
 *                                            MCU supporting charge pump.
 * @retval FSP_ERR_IP_CHANNEL_NOT_PRESENT  Channel ID requested in p_cfg may not available on the devices.
 * @retval FSP_ERR_ALREADY_OPEN            The control structure is already opened.
 *
 **********************************************************************************************************************/
fsp_err_t R_DAC_Open (dac_ctrl_t * p_api_ctrl, dac_cfg_t const * const p_cfg)
{
    dac_instance_ctrl_t * p_ctrl = (dac_instance_ctrl_t *) p_api_ctrl;

    /* Validate the input parameter. */
#if DAC_CFG_PARAM_CHECKING_ENABLE
    FSP_ASSERT(NULL != p_cfg);
    FSP_ASSERT(NULL != p_ctrl);
    FSP_ERROR_RETURN(p_cfg->channel < (uint8_t) BSP_FEATURE_DAC_MAX_CHANNELS, FSP_ERR_IP_CHANNEL_NOT_PRESENT);
    FSP_ERROR_RETURN(false == p_ctrl->channel_opened, FSP_ERR_ALREADY_OPEN);
    if (1U == BSP_FEATURE_DAC_HAS_CHARGEPUMP)
    {
        FSP_ASSERT(NULL != p_cfg->p_extend)
    }
#endif

    /* Power on the DAC device. */
    R_BSP_MODULE_START(FSP_IP_DAC, p_cfg->channel);

    FSP_CRITICAL_SECTION_DEFINE;
    FSP_CRITICAL_SECTION_ENTER;

    /* Stop the channel. */
    (0U == p_cfg->channel) ? (R_DAC->DACR_b.DAOE0 = 0U) : (R_DAC->DACR_b.DAOE1 = 0U);

    FSP_CRITICAL_SECTION_EXIT;

    /* Configure data format: left or right justified. */
    R_DAC->DADPR = (uint8_t) ((uint8_t) p_cfg->data_format << (uint8_t) DAC_DADPR_REG_DPSEL_BIT_POS);

#if BSP_FEATURE_ADC_UNIT_1_CHANNELS

    /* DA/AD Synchronization. Described in hardware manual (see Section 48.2.7
     * 'D/A A/D Synchronous Unit Select Register (DAADUSR)' and Section 48.2.4
     * 'D/A A/D Synchronous Start Control Register (DAADSCR)'of the RA6M3 manual R01UH0886EJ0100). */

    /* D/A A/D Synchronous Unit Select Register: Select ADC Unit 1 for synchronization with this DAC channel */
    if ((0U == R_DAC->DAADSCR) && (p_cfg->ad_da_synchronized))
    {
        /* For correctly writing to this register:
         * 1. ADC (unit 1) module stop bit must be cleared.
         * 2. DAADSCR.DAADST must be cleared.
         *
         * If ADC module is started, this will have no effect.
         *
         * If ADC module is not started yet, this will start it for enabling write to DAADUSR.
         * Since the ad_da_synchronized is set to true in the configuration structure
         * the ADC module is believed to be started at a later point in the application.
         */
        R_BSP_MODULE_START(FSP_IP_ADC, (uint16_t) DAC_ADC_UNIT_1);

        R_DAC->DAADUSR = (uint8_t) DAC_DAADUSR_REG_MASK;

        /* Configure D/A-A/D Synchronous Start Control Register(DAADSCR). */
        R_DAC->DAADSCR = (uint8_t) (1U << (uint8_t) DAC_DAADSCR_REG_DAADST_BIT_POS);
    }
#else

    /* Configure D/A-A/D Synchronous Start Control Register(DAADSCR). */
    R_DAC->DAADSCR = (uint8_t) (p_cfg->ad_da_synchronized << (uint8_t) DAC_DAADSCR_REG_DAADST_BIT_POS);
#endif

#if BSP_FEATURE_DAC_HAS_OUTPUT_AMPLIFIER
    p_ctrl->output_amplifier_enabled = p_cfg->output_amplifier_enabled;
#endif

    /* Set the reference voltage. */
#if BSP_FEATURE_DAC_HAS_DAVREFCR
    R_DAC->DAVREFCR = (uint8_t) DAC_VREF_AVCC0_AVSS0;
#endif

#if (1U == BSP_FEATURE_DAC_HAS_CHARGEPUMP)
    R_DAC->DAPC = (uint8_t) ((dac_extended_cfg_t *) p_cfg->p_extend)->enable_charge_pump;
#endif

    /* Initialize the channel state information. */
    p_ctrl->channel        = p_cfg->channel;
    p_ctrl->channel_opened = DAC_OPEN;

    return FSP_SUCCESS;
}

/******************************************************************************************************************//**
 * Write data to the D/A converter and enable the output if it has not been enabled.
 * Write function automatically starts the D/A conversion after data is successfully written to the channel.
 *
 * @retval   FSP_SUCCESS           Data is successfully written to the D/A Converter.
 * @retval   FSP_ERR_ASSERTION     p_api_ctrl is NULL.
 * @retval   FSP_ERR_NOT_OPEN      Channel associated with p_ctrl has not been opened.
 **********************************************************************************************************************/
fsp_err_t R_DAC_Write (dac_ctrl_t * p_api_ctrl, uint16_t value)
{
    dac_instance_ctrl_t * p_ctrl = (dac_instance_ctrl_t *) p_api_ctrl;

#if DAC_CFG_PARAM_CHECKING_ENABLE

    /* Validate the handle parameter */
    FSP_ASSERT(NULL != p_ctrl);

    /* Validate that the channel is opened. */
    FSP_ERROR_RETURN(p_ctrl->channel_opened, FSP_ERR_NOT_OPEN);
#endif

    /* Write the value to D/A converter. */
    R_DAC->DADR[p_ctrl->channel] = value;

    return FSP_SUCCESS;
}

/******************************************************************************************************************//**
 * Start the D/A conversion output if it has not been started.
 *
 * @retval   FSP_SUCCESS           The channel is started successfully.
 * @retval   FSP_ERR_ASSERTION     p_api_ctrl is NULL.
 * @retval   FSP_ERR_IN_USE        Attempt to re-start a channel.
 * @retval   FSP_ERR_NOT_OPEN      Channel associated with p_ctrl has not been opened.
 **********************************************************************************************************************/
fsp_err_t R_DAC_Start (dac_ctrl_t * p_api_ctrl)
{
    dac_instance_ctrl_t * p_ctrl = (dac_instance_ctrl_t *) p_api_ctrl;

#if DAC_CFG_PARAM_CHECKING_ENABLE

    /* Validate the handle parameter */
    FSP_ASSERT(NULL != p_ctrl);

    /* Validate that the channel is opened. */
    FSP_ERROR_RETURN(p_ctrl->channel_opened, FSP_ERR_NOT_OPEN);

    /* Check if the channel is not already started */
    bool channel_started = false;

    channel_started = ((0U == p_ctrl->channel) ? ((bool) R_DAC->DACR_b.DAOE0) : (bool) (R_DAC->DACR_b.DAOE1));

    FSP_ERROR_RETURN(!channel_started, FSP_ERR_IN_USE);
#endif

#if BSP_FEATURE_DAC_HAS_OUTPUT_AMPLIFIER

    /* Initialize output amplifier. Described in hardware manual (see Section 48.6.5
     * 'Initialization Procedure with the Output Amplifier' of the RA6M3 manual R01UH0878EJ0100). */
    if (p_ctrl->output_amplifier_enabled)
    {
        /* Store value intended to be amplified during DAC output */
        uint16_t value = R_DAC->DADR[p_ctrl->channel];

        /* Clear the D/A Data Register for the requested channel. */
        R_DAC->DADR[p_ctrl->channel] = 0x00U;

        FSP_CRITICAL_SECTION_DEFINE;
        FSP_CRITICAL_SECTION_ENTER;

        if (0U == p_ctrl->channel)
        {
            R_DAC->DACR_b.DAOE0     = 0U; /* Disable channel 0 */
            R_DAC->DAASWCR_b.DAASW0 = 1U; /* Enable D/A Amplifier Stabilization Wait for channel 0 */
            R_DAC->DAAMPCR_b.DAAMP0 = 1U; /* Enable amplifier control for channel 0 */
            R_DAC->DACR_b.DAOE0     = 1U; /* Enable channel 0 to start D/A conversion of 0x00 */
        }
        else
        {
            R_DAC->DACR_b.DAOE1     = 0U; /* Disable channel 1 */
            R_DAC->DAASWCR_b.DAASW1 = 1U; /* Enable D/A Amplifier Stabilization Wait for channel 1 */
            R_DAC->DAAMPCR_b.DAAMP1 = 1U; /* Enable amplifier control for channel 1 */
            R_DAC->DACR_b.DAOE1     = 1U; /* Enable channel 1 to start D/A conversion of 0x00 */
        }

        FSP_CRITICAL_SECTION_EXIT;

        /* The System clock will be running at this point. It is safe to use this function. */
        R_BSP_SoftwareDelay((uint32_t) DAC_CONVERSION_TIME_WITH_OUTPUT_AMPLIFIER, BSP_DELAY_UNITS_MICROSECONDS);

        FSP_CRITICAL_SECTION_ENTER;

        /* Disable D/A Amplifier Stabilization Wait for channel 0 or 1 */
        (0U == p_ctrl->channel) ? (R_DAC->DAASWCR_b.DAASW0 = 0U) : (R_DAC->DAASWCR_b.DAASW1 = 0U);

        FSP_CRITICAL_SECTION_EXIT;

        /* Revert value intended to be amplified during DAC output. */
        R_DAC->DADR[p_ctrl->channel] = value;
    }
    else
#endif
    {
        FSP_CRITICAL_SECTION_DEFINE;
        FSP_CRITICAL_SECTION_ENTER;

        /* Start the channel */
        (0U == p_ctrl->channel) ? (R_DAC->DACR_b.DAOE0 = 1U) : (R_DAC->DACR_b.DAOE1 = 1U);

        FSP_CRITICAL_SECTION_EXIT;
    }

    return FSP_SUCCESS;
}

/******************************************************************************************************************//**
 * Stop the D/A conversion and disable the output signal.
 *
 * @retval  FSP_SUCCESS           The control is successfully stopped.
 * @retval  FSP_ERR_ASSERTION     p_api_ctrl is NULL.
 * @retval  FSP_ERR_NOT_OPEN      Channel associated with p_ctrl has not been opened.
 **********************************************************************************************************************/
fsp_err_t R_DAC_Stop (dac_ctrl_t * p_api_ctrl)
{
    dac_instance_ctrl_t * p_ctrl = (dac_instance_ctrl_t *) p_api_ctrl;

#if DAC_CFG_PARAM_CHECKING_ENABLE

    /* Validate the handle parameter */
    FSP_ASSERT(NULL != p_ctrl);

    /* Validate that the channel is opened. */
    FSP_ERROR_RETURN(p_ctrl->channel_opened, FSP_ERR_NOT_OPEN);
#endif

    FSP_CRITICAL_SECTION_DEFINE;
    FSP_CRITICAL_SECTION_ENTER;

    /* Stop the channel */
    (0U == p_ctrl->channel) ? (R_DAC->DACR_b.DAOE0 = 0U) : (R_DAC->DACR_b.DAOE1 = 0U);

    FSP_CRITICAL_SECTION_EXIT;

    return FSP_SUCCESS;
}

/******************************************************************************************************************//**
 * Stop the D/A conversion, stop output, and close the DAC channel.
 *
 * @retval   FSP_SUCCESS           The channel is successfully closed.
 * @retval   FSP_ERR_ASSERTION     p_api_ctrl is NULL.
 * @retval   FSP_ERR_NOT_OPEN      Channel associated with p_ctrl has not been opened.
 **********************************************************************************************************************/
fsp_err_t R_DAC_Close (dac_ctrl_t * p_api_ctrl)
{
    dac_instance_ctrl_t * p_ctrl = (dac_instance_ctrl_t *) p_api_ctrl;

#if DAC_CFG_PARAM_CHECKING_ENABLE

    /* Validate the handle parameter */
    FSP_ASSERT(NULL != p_ctrl);

    /* Validate that the channel is opened. */
    FSP_ERROR_RETURN(p_ctrl->channel_opened, FSP_ERR_NOT_OPEN);
#endif

    /* Module Stop is not needed here as this module does not have channel specific Start/Stop control.
     * For more than 1 channels used (on selected MCUs), a module stop will disable both the channels. */

    FSP_CRITICAL_SECTION_DEFINE;
    FSP_CRITICAL_SECTION_ENTER;

    /* Stop the channel, clear the amplifier stabilization wait bit and
     * clear the output amplifier control register for the associated channel. */
    if (0U == p_ctrl->channel)
    {
        R_DAC->DACR_b.DAOE0     = 0U;  /* Disable channel 0 */
        R_DAC->DAAMPCR_b.DAAMP0 = 0U;  /* Disable amplifier control for channel 0 */
    }
    else
    {
        R_DAC->DACR_b.DAOE1     = 0U;  /* Disable channel 1 */
        R_DAC->DAAMPCR_b.DAAMP1 = 0U;  /* Disable amplifier control for channel 1 */
    }

    FSP_CRITICAL_SECTION_EXIT;

    /* Update the channel state information. */
    p_ctrl->channel_opened = 0U;

    return FSP_SUCCESS;
}

/******************************************************************************************************************//**
 * Get version and store it in provided pointer p_version.
 *
 * @retval  FSP_SUCCESS           Successfully retrieved version information.
 * @retval  FSP_ERR_ASSERTION     p_version is NULL.
 **********************************************************************************************************************/
fsp_err_t R_DAC_VersionGet (fsp_version_t * p_version)
{
#if DAC_CFG_PARAM_CHECKING_ENABLE
    FSP_ASSERT(NULL != p_version);
#endif

    p_version->version_id = g_dac_version.version_id;

    return FSP_SUCCESS;
}

/*******************************************************************************************************************//**
 * @} (end addtogroup DAC)
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private Functions
 **********************************************************************************************************************/