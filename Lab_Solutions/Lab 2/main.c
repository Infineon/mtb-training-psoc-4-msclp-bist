/******************************************************************************
 * File Name:   main.c
 *
 * Description: This is the source code for the PSoC 4: MSCLP low-power self-capacitance button
 *              Example for ModusToolbox.
 *
 * Related Document: See README.md
 *
 *
 *******************************************************************************
 * $ Copyright 2021-2023 Cypress Semiconductor $
 *******************************************************************************/

/*******************************************************************************
 * Include header files
 ******************************************************************************/

#include "cy_pdl.h"
#include "cy_scb_uart.h"
#include "cybsp.h"
#include "cycfg.h"
#include "cycfg_capsense.h"
#include <stdint.h>
#include <stdio.h>

/*******************************************************************************
 * User configurable Macros
 ********************************************************************************/
/*Enables the Runtime measurement functionality used to for processing time measurement */
#define ENABLE_RUN_TIME_MEASUREMENT     (0u)

/* Enable this, if Tuner needs to be enabled */
#define ENABLE_TUNER                    (1u)

/*Enable PWM controlled LEDs*/
#define ENABLE_PWM_LED                  (1u)

/* 128Hz Refresh rate in Active mode */
#define ACTIVE_MODE_REFRESH_RATE        (128u)

/* 32Hz Refresh rate in Active-Low Refresh rate(ALR) mode */
#define ALR_MODE_REFRESH_RATE           (32u)

/* Timeout to move from ACTIVE mode to ALR mode if there is no user activity */
#define ACTIVE_MODE_TIMEOUT_SEC         (10u)

/* Timeout to move from ALR mode to WOT mode if there is no user activity */
#define ALR_MODE_TIMEOUT_SEC            (5u)

/* Active mode Scan time calculated in us ~= 16us */
#define ACTIVE_MODE_FRAME_SCAN_TIME     (16u)

/* Active mode Processing time in us ~= 23us with PWM LED and Tuner disabled*/
#define ACTIVE_MODE_PROCESS_TIME        (23u)

/* ALR mode Scan time calculated in us ~= 16us */
#define ALR_MODE_FRAME_SCAN_TIME        (16u)

/* ALR mode Processing time in us ~= 23us with PWM LED and Tuner disabled*/
#define ALR_MODE_PROCESS_TIME           (23u)

/*******************************************************************************
 * Macros
 ********************************************************************************/
#define CAPSENSE_MSC0_INTR_PRIORITY     (3u)
#define CY_ASSERT_FAILED                (0u)

/* EZI2C interrupt priority must be higher than CAPSENSE&trade; interrupt. */
#define EZI2C_INTR_PRIORITY             (2u)

/* Define the conditions to check sensor status */
#define CAPSENSE_WIDGET_INACTIVE        (0u)


#define ILO_FREQ                        (40000u)
#define TIME_IN_US                      (1000000u)

#define MINIMUM_TIMER                   (TIME_IN_US / ILO_FREQ)
#if ((TIME_IN_US / ACTIVE_MODE_REFRESH_RATE) > (ACTIVE_MODE_FRAME_SCAN_TIME + ACTIVE_MODE_PROCESS_TIME))
#define ACTIVE_MODE_TIMER           (TIME_IN_US / ACTIVE_MODE_REFRESH_RATE - \
        (ACTIVE_MODE_FRAME_SCAN_TIME + ACTIVE_MODE_PROCESS_TIME))
#elif
#define ACTIVE_MODE_TIMER           (MINIMUM_TIMER)
#endif

#if ((TIME_IN_US / ALR_MODE_REFRESH_RATE) > (ALR_MODE_FRAME_SCAN_TIME + ALR_MODE_PROCESS_TIME))
#define ALR_MODE_TIMER              (TIME_IN_US / ALR_MODE_REFRESH_RATE - \
        (ALR_MODE_FRAME_SCAN_TIME + ALR_MODE_PROCESS_TIME))
#elif
#define ALR_MODE_TIMER              (MINIMUM_TIMER)
#endif

#define ACTIVE_MODE_TIMEOUT             (ACTIVE_MODE_REFRESH_RATE * ACTIVE_MODE_TIMEOUT_SEC)

#define ALR_MODE_TIMEOUT                (ALR_MODE_REFRESH_RATE * ALR_MODE_TIMEOUT_SEC)

#define TIMEOUT_RESET                   (0u)

#if ENABLE_PWM_LED
#define ACTIVE_BLINK_RATE_IN_HZ         (10u)
#define ALR_BLINK_RATE_IN_HZ            (2u)
#define WOT_BLINK_RATE_IN_HZ            (1u)
#define PWM_FREQ                        (7812u)
#define ACTIVE_MODE_LED_BLINK_PERIOD    (PWM_FREQ/ACTIVE_BLINK_RATE_IN_HZ)
#define ALR_MODE_LED_BLINK_PERIOD       ((uint32_t)(PWM_FREQ/ALR_BLINK_RATE_IN_HZ))
#define WOT_MODE_LED_BLINK_PERIOD       (PWM_FREQ/WOT_BLINK_RATE_IN_HZ)
#define ACTIVE_MODE_LED_BLINK_DUTY_CYCLE ((ACTIVE_MODE_LED_BLINK_PERIOD)>>1u)
#define ALR_MODE_LED_BLINK_DUTY_CYCLE   ((ALR_MODE_LED_BLINK_PERIOD)>>1u)
#define WOT_MODE_LED_BLINK_DUTY_CYCLE   ((WOT_MODE_LED_BLINK_PERIOD)>>2u)
#define CYBSP_LED_OFF                   (0u)
#define CYBSP_LED_ON                    (1u)
#endif


#if ENABLE_RUN_TIME_MEASUREMENT
#define SYS_TICK_INTERVAL           (0x00FFFFFF)
#define TIME_PER_TICK_IN_US         ((float)1/CY_CAPSENSE_CPU_CLK)*TIME_IN_US
#endif

/*****************************************************************************
 * Finite state machine states for device operating states
 *****************************************************************************/
typedef enum
{
    ACTIVE_MODE = 0x01u,    /* Active mode - All the sensors are scanned in this state
     * with highest refresh rate */
    ALR_MODE = 0x02u,       /* Active-Low Refresh Rate (ALR) mode - All the sensors are
     * scanned in this state with low refresh rate */
    WOT_MODE = 0x03u        /* Wake on Touch (WoT) mode - Low Power sensors are scanned
     * in this state with lowest refresh rate */
} APPLICATION_STATE;

/*******************************************************************************
 * Function Prototypes
 ********************************************************************************/
static void initialize_capsense(void);
static void capsense_msc0_isr(void);

static void ezi2c_isr(void);
static void initialize_capsense_tuner(void);

#if ENABLE_RUN_TIME_MEASUREMENT
static void init_sys_tick();
static void start_runtime_measurement();
static uint32_t stop_runtime_measurement();
#endif

#if ENABLE_PWM_LED
void led_control();
#endif

/* Deep Sleep Callback function */
void register_callback(void);
cy_en_syspm_status_t deep_sleep_callback(cy_stc_syspm_callback_params_t *callbackParams,
        cy_en_syspm_callback_mode_t mode);

/*******************************************************************************
 * Global Definitions
 *******************************************************************************/

/* Variables holds the current low power state [ACTIVE, ALR or WOT] */
APPLICATION_STATE capsense_state;
APPLICATION_STATE prev_capsense_state;

static bool execute_bist = false;

cy_stc_scb_ezi2c_context_t ezi2c_context;

/* Callback parameters for custom, EzI2C */

/* Callback parameters for EzI2C */
cy_stc_syspm_callback_params_t ezi2cCallbackParams =
{
        .base       = SCB1,
        .context    = &ezi2c_context
};

/* Callback parameters for custom callback */
cy_stc_syspm_callback_params_t deepSleepCallBackParams = {
        .base       =  NULL,
        .context    =  NULL
};

/* Callback declaration for EzI2C Deep Sleep callback */
cy_stc_syspm_callback_t ezi2cCallback =
{
        .callback       = (Cy_SysPmCallback)&Cy_SCB_EZI2C_DeepSleepCallback,
        .type           = CY_SYSPM_DEEPSLEEP,
        .skipMode       = 0UL,
        .callbackParams = &ezi2cCallbackParams,
        .prevItm        = NULL,
        .nextItm        = NULL,
        .order          = 0
};

/* Callback declaration for Custom Deep Sleep callback */
cy_stc_syspm_callback_t deepSleepCb =
{
        .callback       = &deep_sleep_callback,
        .type           = CY_SYSPM_DEEPSLEEP,
        .skipMode       = 0UL,
        .callbackParams = &deepSleepCallBackParams,
        .prevItm        = NULL,
        .nextItm        = NULL,
        .order          = 2
};


void print_bist_test_result(char * test_name, cy_en_capsense_bist_status_t result)
{
	char test_result_string[50];
	char test_name_string[50];

	switch(result)
	{
		case CY_CAPSENSE_BIST_SUCCESS_E:
			sprintf(test_result_string,"Succesful \r\n");
			break;
	    case CY_CAPSENSE_BIST_BAD_PARAM_E:
			sprintf(test_result_string,"Bad Parameter \r\n");
			break;
		case CY_CAPSENSE_BIST_HW_BUSY_E:
			sprintf(test_result_string,"CAPSENSE HW Busy \r\n");
			break;
	    case CY_CAPSENSE_BIST_LOW_LIMIT_E:
			sprintf(test_result_string,"Low Limit Error \r\n");
			break;
	    case CY_CAPSENSE_BIST_HIGH_LIMIT_E:
			sprintf(test_result_string,"High Limit Error \r\n");
			break;
	    case CY_CAPSENSE_BIST_ERROR_E:
			sprintf(test_result_string,"Error \r\n");
			break;
	    case CY_CAPSENSE_BIST_FEATURE_DISABLED_E:
			sprintf(test_result_string,"BIST Feature Disabled \r\n");
			break;
	    case CY_CAPSENSE_BIST_TIMEOUT_E:
			sprintf(test_result_string,"Timeout \r\n");
			break;
	    case CY_CAPSENSE_BIST_BAD_CONFIG_E:
			sprintf(test_result_string,"Bad Config \r\n");
			break;
	    case CY_CAPSENSE_BIST_FAIL_E:
			sprintf(test_result_string,"Fail \r\n");
			break;
	    default:
			break;	
	}

	
	sprintf(test_name_string, "\n%s BIST Status: ", test_name);
	Cy_SCB_UART_PutString(DEBUG_UART_HW, test_name_string);
	Cy_SCB_UART_PutString(DEBUG_UART_HW, test_result_string);
	
}

void execute_capsense_bist(void)
{
	cy_en_capsense_bist_status_t run_self_test_result;
	run_self_test_result = Cy_CapSense_RunSelfTest(CY_CAPSENSE_BIST_RUN_AVAILABLE_SELF_TEST_MASK , &cy_capsense_context);
	print_bist_test_result("Run Self Test", run_self_test_result);


	char print_string[50];
	uint32_t shield_cap_measurement = *cy_capsense_context.ptrBistContext->ptrChShieldCap;
	uint32_t cmod_1_cap_measurement = cy_capsense_context.ptrBistContext->cMod01Cap;
	uint32_t cmod_2_cap_measurement = cy_capsense_context.ptrBistContext->cMod02Cap;
	uint32_t lp_sensor_cap_measurement = cy_capsense_context.ptrWdConfig[CY_CAPSENSE_LOWPOWER0_WDGT_ID].ptrSnsCapacitance[CY_CAPSENSE_LOWPOWER0_SNS0_ID];
	uint32_t button_sensor_cap_measurement = cy_capsense_context.ptrWdConfig[CY_CAPSENSE_BUTTON0_WDGT_ID].ptrSnsCapacitance[CY_CAPSENSE_BUTTON0_SNS0_ID];
	uint32_t lp_electrode_cap_measurement = cy_capsense_context.ptrWdConfig[CY_CAPSENSE_LOWPOWER0_WDGT_ID].ptrEltdCapacitance[CY_CAPSENSE_LOWPOWER0_SNS0_ID];
	uint32_t button_electrode_cap_measurement = cy_capsense_context.ptrWdConfig[CY_CAPSENSE_BUTTON0_WDGT_ID].ptrEltdCapacitance[CY_CAPSENSE_BUTTON0_SNS0_ID];
	uint16_t vdda = cy_capsense_context.ptrBistContext->vddaVoltage;
	uint32_t lp_sensor_crc = cy_capsense_context.ptrBistContext->ptrWdgtCrc[CY_CAPSENSE_LOWPOWER0_WDGT_ID];
	uint32_t buttun_crc = cy_capsense_context.ptrBistContext->ptrWdgtCrc[CY_CAPSENSE_BUTTON0_WDGT_ID];

	sprintf(print_string, "Shield Capacitance: %u fF\r\n", shield_cap_measurement);
	Cy_SCB_UART_PutString(DEBUG_UART_HW, print_string);

	sprintf(print_string, "COMD 1 Capacitance: %u pF\r\n", cmod_1_cap_measurement);
	Cy_SCB_UART_PutString(DEBUG_UART_HW, print_string);

	sprintf(print_string, "COMD 2 Capacitance: %u pF\r\n", cmod_2_cap_measurement);
	Cy_SCB_UART_PutString(DEBUG_UART_HW, print_string);

	sprintf(print_string, "Low Power Sensor Capacitance: %u fF\r\n", lp_sensor_cap_measurement);
	Cy_SCB_UART_PutString(DEBUG_UART_HW, print_string);

	sprintf(print_string, "Button Sensor Capacitance: %u fF\r\n", button_sensor_cap_measurement);
	Cy_SCB_UART_PutString(DEBUG_UART_HW, print_string);

	sprintf(print_string, "Low Power Electrode Capacitance: %u fF\r\n", lp_electrode_cap_measurement);
	Cy_SCB_UART_PutString(DEBUG_UART_HW, print_string);

	sprintf(print_string, "Button Electrode Capacitance: %u fF\r\n", button_electrode_cap_measurement);
	Cy_SCB_UART_PutString(DEBUG_UART_HW, print_string);

	sprintf(print_string, "VDDA Voltage: %u mV\r\n", vdda);
	Cy_SCB_UART_PutString(DEBUG_UART_HW, print_string);

	sprintf(print_string, "Low Power Sensor CRC: 0x%x\r\n", lp_sensor_crc);
	Cy_SCB_UART_PutString(DEBUG_UART_HW, print_string);

	sprintf(print_string, "Button Sensor CRC: 0x%x\r\n", buttun_crc);
	Cy_SCB_UART_PutString(DEBUG_UART_HW, print_string);

	uint16_t lp_sensor_low_baseline = 1190;
	uint16_t lp_sensor_high_baseline = 1790; 
	cy_en_capsense_bist_status_t lp_sensor_raw_count_bist;
	
	lp_sensor_raw_count_bist = Cy_CapSense_CheckIntegritySensorRawcount(
											 CY_CAPSENSE_LOWPOWER0_WDGT_ID, CY_CAPSENSE_LOWPOWER0_SNS0_ID, 
											 lp_sensor_high_baseline,lp_sensor_low_baseline, 
											 &cy_capsense_context);

	print_bist_test_result("LP Sensor Raw Count", lp_sensor_raw_count_bist);

	uint16_t button0_low_baseline = 1190;
	uint16_t button0_high_baseline = 1790; 
	cy_en_capsense_bist_status_t button0_raw_count_bist;
	
	button0_raw_count_bist = Cy_CapSense_CheckIntegritySensorRawcount(
											 CY_CAPSENSE_BUTTON0_WDGT_ID, CY_CAPSENSE_BUTTON0_SNS0_ID, 
											 button0_high_baseline,button0_low_baseline, 
											 &cy_capsense_context);

	print_bist_test_result("Button 0 Raw Count", button0_raw_count_bist);
}

void button_isr(void)
{
    execute_bist = true;
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN);
}

/*******************************************************************************
 * Function Name: main
 ********************************************************************************
 * Summary:
 *  System entrance point. This function performs
 *  - initial setup of device
 *  - initialize CAPSENSE&trade;
 *  - initialize tuner communication
 *  - scan touch input continuously at 3 different power modes
 *  - user LED for touch indication
 *
 * Return:
 *  int
 *
 *******************************************************************************/
int main(void)
{
    cy_rslt_t result;
    uint32_t capsense_state_timeout;
    uint32_t interruptStatus;
	cy_stc_sysint_t intrCfg = 
    {
        /*.intrSrc =*/ CYBSP_USER_BTN1_IRQ,
        /*.intrPriority =*/ 3UL 
    };


    #if ENABLE_RUN_TIME_MEASUREMENT
    static uint32_t active_processing_time;
    static uint32_t alr_processing_time;
    #endif

    /* Initialize the device and board peripherals */
    result = cybsp_init() ;

    #if ENABLE_RUN_TIME_MEASUREMENT
    init_sys_tick();
    #endif

    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(CY_ASSERT_FAILED);
    }

	/* Configure UART to operate */
    (void) Cy_SCB_UART_Init(DEBUG_UART_HW, &DEBUG_UART_config, NULL);

    /* Enable UART to operate */
    Cy_SCB_UART_Enable(DEBUG_UART_HW);

	
    Cy_SysInt_Init(&intrCfg, button_isr);
    
    /* Enable the interrupt */
    NVIC_EnableIRQ(intrCfg.intrSrc);

    /* Enable global interrupts */
    __enable_irq();

	Cy_SCB_UART_PutString(DEBUG_UART_HW, "\x1b[2J\x1b[;H");

	Cy_SCB_UART_PutString(DEBUG_UART_HW, 
				"****************** "
               "PSOC 4100T Plus MCU: CAPSENSE BIST "
               "****************** \r\n\n");


    /* Initialize EZI2C */
    initialize_capsense_tuner();

    #if ENABLE_PWM_LED
    /* Initialize PWM block */
    (void)Cy_TCPWM_PWM_Init(CYBSP_PWM_HW, CYBSP_PWM_NUM, &CYBSP_PWM_config);
    /* Enable the initialized PWM */
    Cy_TCPWM_Enable_Multiple(CYBSP_PWM_HW, CYBSP_PWM_MASK);
    /* Then start the PWM */
    Cy_TCPWM_TriggerReloadOrIndex(CYBSP_PWM_HW, CYBSP_PWM_MASK);

    Cy_TCPWM_PWM_SetPeriod0(CYBSP_PWM_HW, CYBSP_PWM_NUM,390);
    Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM_HW, CYBSP_PWM_NUM,195);
    #endif

    /* Register callbacks */
    register_callback();

    /* Define initial state of the device and the corresponding refresh rate*/
    capsense_state = ACTIVE_MODE;
    capsense_state_timeout = ACTIVE_MODE_TIMEOUT;

    /* Initialize MSC CAPSENSE&trade; */
    initialize_capsense();

    /* Measures the actual ILO frequency and compensate MSCLP wake up timers */
    Cy_CapSense_IloCompensate(&cy_capsense_context);

	execute_capsense_bist();

    /* Configure the MSCLP wake up timer as per the ACTIVE mode refresh rate */
    Cy_CapSense_ConfigureMsclpTimer(ACTIVE_MODE_TIMER, &cy_capsense_context);

    for (;;)
    {
        switch(capsense_state)
        {
            case ACTIVE_MODE:

                Cy_CapSense_ScanAllSlots(&cy_capsense_context);

                interruptStatus = Cy_SysLib_EnterCriticalSection();

                while (Cy_CapSense_IsBusy(&cy_capsense_context))
                {
                    #if ENABLE_PWM_LED
                    Cy_SysPm_CpuEnterSleep();
                    #else
                    Cy_SysPm_CpuEnterDeepSleep();
                    #endif

                    Cy_SysLib_ExitCriticalSection(interruptStatus);

                    /* This is a place where all interrupt handlers will be executed */
                    interruptStatus = Cy_SysLib_EnterCriticalSection();
                }

                Cy_SysLib_ExitCriticalSection(interruptStatus);

                #if ENABLE_RUN_TIME_MEASUREMENT
                active_processing_time=0;
                start_runtime_measurement();
                #endif

                Cy_CapSense_ProcessAllWidgets(&cy_capsense_context);

                /* Scan, process and check the status of the all Active mode sensors */
                if(Cy_CapSense_IsAnyWidgetActive(&cy_capsense_context))
                {
                    capsense_state_timeout = ACTIVE_MODE_TIMEOUT;
                }
                else
                {
                    capsense_state_timeout--;

                    if(TIMEOUT_RESET == capsense_state_timeout)
                    {
                        capsense_state = ALR_MODE;
                        capsense_state_timeout = ALR_MODE_TIMEOUT;

                        /* Configure the MSCLP wake up timer as per the ALR mode refresh rate */
                        Cy_CapSense_ConfigureMsclpTimer(ALR_MODE_TIMER, &cy_capsense_context);
                    }
                }

                #if ENABLE_RUN_TIME_MEASUREMENT
                active_processing_time=stop_runtime_measurement();
                #endif

                break;
                /* End of ACTIVE_MODE */

                /* Active Low Refresh-rate Mode */
            case ALR_MODE :

                Cy_CapSense_ScanAllSlots(&cy_capsense_context);

                interruptStatus = Cy_SysLib_EnterCriticalSection();

                while (Cy_CapSense_IsBusy(&cy_capsense_context))
                {
                    #if ENABLE_PWM_LED
                    Cy_SysPm_CpuEnterSleep();
                    #else
                    Cy_SysPm_CpuEnterDeepSleep();
                    #endif

                    Cy_SysLib_ExitCriticalSection(interruptStatus);

                    /* This is a place where all interrupt handlers will be executed */
                    interruptStatus = Cy_SysLib_EnterCriticalSection();
                }

                Cy_SysLib_ExitCriticalSection(interruptStatus);

                #if ENABLE_RUN_TIME_MEASUREMENT
                alr_processing_time=0;
                start_runtime_measurement();
                #endif

                Cy_CapSense_ProcessAllWidgets(&cy_capsense_context);

                /* Scan, process and check the status of the all Active mode sensors */
                if(Cy_CapSense_IsAnyWidgetActive(&cy_capsense_context))
                {
                    capsense_state = ACTIVE_MODE;
                    capsense_state_timeout = ACTIVE_MODE_TIMEOUT;

                    /* Configure the MSCLP wake up timer as per the ACTIVE mode refresh rate */
                    Cy_CapSense_ConfigureMsclpTimer(ACTIVE_MODE_TIMER, &cy_capsense_context);
                }
                else
                {
                    capsense_state_timeout--;

                    if(TIMEOUT_RESET == capsense_state_timeout)
                    {
                        capsense_state = WOT_MODE;

                    }
                }

                #if ENABLE_RUN_TIME_MEASUREMENT
                alr_processing_time=stop_runtime_measurement();
                #endif

                break;
                /* End of Active-Low Refresh Rate(ALR) mode */

                /* Wake On Touch Mode */
            case WOT_MODE :

                Cy_CapSense_ScanAllLpSlots(&cy_capsense_context);

                interruptStatus = Cy_SysLib_EnterCriticalSection();

                while (Cy_CapSense_IsBusy(&cy_capsense_context))
                {
                    #if ENABLE_PWM_LED
                    Cy_SysPm_CpuEnterSleep();
                    #else
                    Cy_SysPm_CpuEnterDeepSleep();
                    #endif

                    Cy_SysLib_ExitCriticalSection(interruptStatus);

                    /* This is a place where all interrupt handlers will be executed */
                    interruptStatus = Cy_SysLib_EnterCriticalSection();
                }

                Cy_SysLib_ExitCriticalSection(interruptStatus);

                if (Cy_CapSense_IsAnyLpWidgetActive(&cy_capsense_context))
                {
                    capsense_state = ACTIVE_MODE;
                    capsense_state_timeout = ACTIVE_MODE_TIMEOUT;

                    /* Configure the MSCLP wake up timer as per the ACTIVE mode refresh rate */
                    Cy_CapSense_ConfigureMsclpTimer(ACTIVE_MODE_TIMER, &cy_capsense_context);
                }
                else
                {
                    capsense_state = ALR_MODE;
                    capsense_state_timeout = ALR_MODE_TIMEOUT;

                    /* Configure the MSCLP wake up timer as per the ALR mode refresh rate */
                    Cy_CapSense_ConfigureMsclpTimer(ALR_MODE_TIMER, &cy_capsense_context);
                }

                break;
                /* End of "WAKE_ON_TOUCH_MODE" */

            default:
                /**  Unknown power mode state. Unexpected situation.  **/
                CY_ASSERT(CY_ASSERT_FAILED);
                break;
        }
        #if ENABLE_PWM_LED
        led_control();
        #endif

		if (execute_bist)
		{
			/* Simple debounce */
			Cy_SysLib_Delay(100);
			execute_bist = false;
			execute_capsense_bist();
			
		}

        #if ENABLE_TUNER
        /* Establishes synchronized communication with the CAPSENSE&trade; Tuner tool */
        Cy_CapSense_RunTuner(&cy_capsense_context);
        #endif
    }
}

/*******************************************************************************
 * Function Name: initialize_capsense
 ********************************************************************************
 * Summary:
 *  This function initializes the CAPSENSE&trade; and configures the CAPSENSE&trade;
 *  interrupt.
 *
 *******************************************************************************/
static void initialize_capsense(void)
{
    cy_capsense_status_t status = CY_CAPSENSE_STATUS_SUCCESS;

    /* CAPSENSE&trade; interrupt configuration MSCLP 0 */
    const cy_stc_sysint_t capsense_msc0_interrupt_config =
    {
            .intrSrc = CY_MSCLP0_LP_IRQ,
            .intrPriority = CAPSENSE_MSC0_INTR_PRIORITY,
    };

    /* Capture the MSC HW block and initialize it to the default state. */
    status = Cy_CapSense_Init(&cy_capsense_context);

    if (CY_CAPSENSE_STATUS_SUCCESS == status)
    {
        /* Initialize CAPSENSE&trade; interrupt for MSC 0 */
        Cy_SysInt_Init(&capsense_msc0_interrupt_config, capsense_msc0_isr);
        NVIC_ClearPendingIRQ(capsense_msc0_interrupt_config.intrSrc);
        NVIC_EnableIRQ(capsense_msc0_interrupt_config.intrSrc);

        status = Cy_CapSense_Enable(&cy_capsense_context);
    }

    if(status != CY_CAPSENSE_STATUS_SUCCESS)
    {
        /* This status could fail before tuning the sensors correctly.
         * Ensure that this function passes after the CAPSENSE&trade; sensors are tuned
         * as per procedure give in the Readme.md file */
    }
}

/*******************************************************************************
 * Function Name: capsense_msc0_isr
 ********************************************************************************
 * Summary:
 *  Wrapper function for handling interrupts from CAPSENSE&trade; MSC0 block.
 *
 *******************************************************************************/
static void capsense_msc0_isr(void)
{
    Cy_CapSense_InterruptHandler(CY_MSCLP0_HW, &cy_capsense_context);
}

/*******************************************************************************
 * Function Name: initialize_capsense_tuner
 ********************************************************************************
 * Summary:
 *  EZI2C module to communicate with the CAPSENSE&trade; Tuner tool.
 *
 *******************************************************************************/
static void initialize_capsense_tuner(void)
{
    cy_en_scb_ezi2c_status_t status = CY_SCB_EZI2C_SUCCESS;

    /* EZI2C interrupt configuration structure */
    const cy_stc_sysint_t ezi2c_intr_config =
    {
            .intrSrc = CYBSP_EZI2C_IRQ,
            .intrPriority = EZI2C_INTR_PRIORITY,
    };

    /* Initialize the EzI2C firmware module */
    status = Cy_SCB_EZI2C_Init(CYBSP_EZI2C_HW, &CYBSP_EZI2C_config, &ezi2c_context);

    if(status != CY_SCB_EZI2C_SUCCESS)
    {
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    Cy_SysInt_Init(&ezi2c_intr_config, ezi2c_isr);
    NVIC_EnableIRQ(ezi2c_intr_config.intrSrc);

    /* Set the CAPSENSE&trade; data structure as the I2C buffer to be exposed to the
     * master on primary slave address interface. Any I2C host tools such as
     * the Tuner or the Bridge Control Panel can read this buffer but you can
     * connect only one tool at a time.
     */
    #if ENABLE_TUNER
    Cy_SCB_EZI2C_SetBuffer1(CYBSP_EZI2C_HW, (uint8_t *)&cy_capsense_tuner,
            sizeof(cy_capsense_tuner), sizeof(cy_capsense_tuner),
            &ezi2c_context);
    #endif

    Cy_SCB_EZI2C_Enable(CYBSP_EZI2C_HW);

}

/*******************************************************************************
 * Function Name: ezi2c_isr
 ********************************************************************************
 * Summary:
 *  Wrapper function for handling interrupts from EZI2C block.
 *
 *******************************************************************************/
static void ezi2c_isr(void)
{
    Cy_SCB_EZI2C_Interrupt(CYBSP_EZI2C_HW, &ezi2c_context);
}

#if ENABLE_RUN_TIME_MEASUREMENT
/*******************************************************************************
 * Function Name: init_sys_tick
 ********************************************************************************
 * Summary:
 *  initializes the system tick with highest possible value to start counting down.
 *
 *******************************************************************************/
static void init_sys_tick()
{
    Cy_SysTick_Init (CY_SYSTICK_CLOCK_SOURCE_CLK_CPU ,0x00FFFFFF);
}
#endif

#if ENABLE_RUN_TIME_MEASUREMENT
/*******************************************************************************
 * Function Name: start_runtime_measurement
 ********************************************************************************
 * Summary:
 *  Initializes the system tick counter by calling Cy_SysTick_Clear() API.
 *******************************************************************************/
static void start_runtime_measurement()
{
    Cy_SysTick_Clear();
}

/*******************************************************************************
 * Function Name: stop_runtime_measurement
 ********************************************************************************
 * Summary:
 *  Reads the system tick and converts to time in microseconds(us).
 *
 *  Returns:
 *  runtime - in microseconds(us)
 *******************************************************************************/

static uint32_t stop_runtime_measurement()
{
    uint32_t ticks;
    uint32_t runtime;
    ticks=Cy_SysTick_GetValue();
    ticks= SYS_TICK_INTERVAL - Cy_SysTick_GetValue();
    runtime=ticks*TIME_PER_TICK_IN_US;
    return runtime;
}
#endif

#if ENABLE_PWM_LED
/*******************************************************************************
 * Function Name: led_control
 ********************************************************************************
 * Summary:
 *  Control USER LED2 in the kit to show the button status:
 *    No touch - LED2 == OFF
 *    Touch - LED2 == ON
 *  Control USER LED3 in the kit to show different power modes:
 *    Active Mode :LED3 Blinks at a fast rate
 *    ALR Mode :LED3 Blinks at a medium rate
 *    WOT Mode :LED3 Blinks at a slow rate
 *******************************************************************************/
void led_control()
{

    if (CAPSENSE_WIDGET_INACTIVE != Cy_CapSense_IsWidgetActive(CY_CAPSENSE_BUTTON0_WDGT_ID, &cy_capsense_context))
    {
        Cy_GPIO_Write(CYBSP_USER_LED2_PORT, CYBSP_USER_LED2_NUM, CYBSP_LED_ON);
    }
    else
    {
        Cy_GPIO_Write(CYBSP_USER_LED2_PORT, CYBSP_USER_LED2_NUM, CYBSP_LED_OFF);
    }

    /* LED3 Control: Based on application power mode */
    if( capsense_state!=prev_capsense_state)
    {
        switch(capsense_state)
        {
            case ACTIVE_MODE:
                /* LED3 blinks at a fast rate */
                Cy_TCPWM_TriggerReloadOrIndex(CYBSP_PWM_HW, CYBSP_PWM_MASK);
                Cy_TCPWM_PWM_SetPeriod0(CYBSP_PWM_HW, CYBSP_PWM_NUM,ACTIVE_MODE_LED_BLINK_PERIOD);
                Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM_HW, CYBSP_PWM_NUM,(ACTIVE_MODE_LED_BLINK_DUTY_CYCLE));
                break;

            case ALR_MODE:
                /* LED3 blinks at a medium rate */
                Cy_TCPWM_TriggerReloadOrIndex(CYBSP_PWM_HW, CYBSP_PWM_MASK);
                Cy_TCPWM_PWM_SetPeriod0(CYBSP_PWM_HW, CYBSP_PWM_NUM,ALR_MODE_LED_BLINK_PERIOD);
                Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM_HW, CYBSP_PWM_NUM, (ALR_MODE_LED_BLINK_DUTY_CYCLE));
                break;

            case WOT_MODE:
                /* LED3 blinks at a slow rate */
                Cy_TCPWM_TriggerReloadOrIndex(CYBSP_PWM_HW, CYBSP_PWM_MASK);
                Cy_TCPWM_PWM_SetPeriod0(CYBSP_PWM_HW, CYBSP_PWM_NUM,WOT_MODE_LED_BLINK_PERIOD);
                Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM_HW, CYBSP_PWM_NUM, (WOT_MODE_LED_BLINK_DUTY_CYCLE));
                break;

            default:
                /* Unknown power mode state. Unexpected situation. */
                CY_ASSERT(CY_ASSERT_FAILED);
                break;
        }
        prev_capsense_state=capsense_state;
    }
}
#endif

/*******************************************************************************
 * Function Name: register_callback
 ********************************************************************************
 *
 * Summary:
 *  Register Deep Sleep callbacks for EzI2C, SPI components
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void register_callback(void)
{
    /* Register EzI2C Deep Sleep callback */
    Cy_SysPm_RegisterCallback(&ezi2cCallback);

    /* Register Deep Sleep callback */
    Cy_SysPm_RegisterCallback(&deepSleepCb);
}

/*******************************************************************************
 * Function Name: deep_sleep_callback
 ********************************************************************************
 *
 * Summary:
 * Deep Sleep callback implementation. Waits for the completion of SPI transaction.
 * And change the SPI GPIOs to highZ while transition to deep-sleep and vice-versa
 *
 * Parameters:
 *  callbackParams: The pointer to the callback parameters structure cy_stc_syspm_callback_params_t.
 *  mode: Callback mode, see cy_en_syspm_callback_mode_t
 *
 * Return:
 *  Entered status, see cy_en_syspm_status_t.
 *
 *******************************************************************************/
cy_en_syspm_status_t deep_sleep_callback(
        cy_stc_syspm_callback_params_t *callbackParams, cy_en_syspm_callback_mode_t mode)
{
    cy_en_syspm_status_t ret_val = CY_SYSPM_FAIL;

    switch (mode)
    {
        case CY_SYSPM_CHECK_READY:

            ret_val = CY_SYSPM_SUCCESS;
            break;

        case CY_SYSPM_CHECK_FAIL:

            ret_val = CY_SYSPM_SUCCESS;
            break;

        case CY_SYSPM_BEFORE_TRANSITION:

            ret_val = CY_SYSPM_SUCCESS;
            break;

        case CY_SYSPM_AFTER_TRANSITION:

            ret_val = CY_SYSPM_SUCCESS;
            break;

        default:
            /* Don't do anything in the other modes */
            ret_val = CY_SYSPM_SUCCESS;
            break;
    }
    return ret_val;
}

/* [] END OF FILE */
