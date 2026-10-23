/* PY32F002AL15S6TU: PA3/pin 3 = EXTI key, PA1/pin 7 = TIM1_CH4.
 * Internal 8 MHz HSI. The MOSFET must support the available gate voltage.
 */
#include "main.h"

#define KEY_PORT            GPIOA
#define KEY_PIN             GPIO_PIN_3
#define MOTOR_PORT          GPIOA
#define MOTOR_PIN           GPIO_PIN_1
#define PWM_FREQUENCY_HZ    20000U
#define RAMP_TIME_MS        5000U
#define DEBOUNCE_MS         20U
#define DUTY_FULL           10000U

/* Calibrated DC meter readings AT THE GATE, driven directly by PA1.
 * Use PA1's high level (3.3 V), not the motor's battery supply voltage.
 */
#ifndef GATE_HIGH_MV
#define GATE_HIGH_MV        3300U
#endif
#ifndef GATE_START_MV
#define GATE_START_MV       2600U
#endif
#ifndef GATE_STOP_MV
#define GATE_STOP_MV        2200U
#endif

#if (GATE_STOP_MV == 0U) || (GATE_START_MV < GATE_STOP_MV) || \
    (GATE_HIGH_MV <= GATE_START_MV) || (GATE_HIGH_MV > 60000U)
#error "Require 0 < GATE_STOP_MV <= GATE_START_MV < GATE_HIGH_MV"
#endif

/* On-duty basis points, for direct active-high N-channel gate drive. */
#define GATE_START_DUTY \
  ((GATE_START_MV * DUTY_FULL + GATE_HIGH_MV - 1U) / GATE_HIGH_MV)
#define GATE_STOP_DUTY \
  ((GATE_STOP_MV * DUTY_FULL + GATE_HIGH_MV - 1U) / GATE_HIGH_MV)

#define MOTOR_OFF_LEVEL     GPIO_PIN_RESET
#define MOTOR_PWM_POLARITY  TIM_OCPOLARITY_HIGH
#define MOTOR_IDLE_STATE    TIM_OCIDLESTATE_RESET

static TIM_HandleTypeDef htim1;
static uint32_t pwmPeriodCounts;
static uint32_t dutyNow;
/* All ramp state is explicitly in millivolts. OFF is a separate state. */
static uint32_t gateVoltageMv;
static uint32_t rampFromMv;
static uint32_t rampToMv;
static uint32_t rampStartedMs;
static uint8_t rampActive;
static uint8_t motorRequestedOn;

static volatile uint32_t keyLastEdgeMs;
static volatile uint8_t keyEventPending;
static GPIO_PinState keyStable;

/* On a fatal error, override the timer with an inactive GPIO. */
static void APP_MotorForceOff(void)
{
  GPIO_InitTypeDef gpio = {0};
  __HAL_RCC_GPIOA_CLK_ENABLE();
  HAL_GPIO_WritePin(MOTOR_PORT, MOTOR_PIN, MOTOR_OFF_LEVEL);
  gpio.Pin = MOTOR_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(MOTOR_PORT, &gpio);
}

static void APP_PWM_Init(void)
{
  TIM_OC_InitTypeDef oc = {0};
  GPIO_InitTypeDef gpio = {0};
  uint32_t timerClock = HAL_RCC_GetPCLK1Freq();

  if ((RCC->CFGR & RCC_CFGR_PPRE) != 0U)
  {
    timerClock *= 2U;
  }
  pwmPeriodCounts = timerClock / PWM_FREQUENCY_HZ;
  /* CCR=ARR+1 must fit in 16 bits to obtain a true 100% output. */
  if ((pwmPeriodCounts < 2U) || (pwmPeriodCounts > 65535U) ||
      ((timerClock % PWM_FREQUENCY_HZ) != 0U))
  {
    APP_ErrorHandler();
  }

  __HAL_RCC_TIM1_CLK_ENABLE();
  __HAL_RCC_TIM1_FORCE_RESET();
  __HAL_RCC_TIM1_RELEASE_RESET();
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0U;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = pwmPeriodCounts - 1U;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0U;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    APP_ErrorHandler();
  }

  oc.OCMode = TIM_OCMODE_PWM1;
  oc.Pulse = 0U;
  oc.OCPolarity = MOTOR_PWM_POLARITY;
  oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  oc.OCFastMode = TIM_OCFAST_DISABLE;
  oc.OCIdleState = MOTOR_IDLE_STATE;
  oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_4) != HAL_OK)
  {
    APP_ErrorHandler();
  }

  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, 0U);
  /* Load buffered ARR/CCR before connecting the timer to PA1. */
  htim1.Instance->EGR = TIM_EGR_UG;
  if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4) != HAL_OK)
  {
    APP_ErrorHandler();
  }

  gpio.Pin = MOTOR_PIN;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = GPIO_AF13_TIM1;
  HAL_GPIO_Init(MOTOR_PORT, &gpio);
}

/* Gate DC average: Vgate_avg ~= PA1_high * duty.
 * forceUpdate is used for the OFF->start and stop->OFF jumps.
 */
static void APP_MotorSetGateVoltage(uint32_t millivolts, uint8_t forceUpdate)
{
  uint32_t compare;
  if (millivolts > GATE_HIGH_MV)
  {
    millivolts = GATE_HIGH_MV;
  }
  if ((millivolts != 0U) && (millivolts < GATE_STOP_MV))
  {
    millivolts = GATE_STOP_MV;
  }
  gateVoltageMv = millivolts;
  dutyNow = (millivolts * DUTY_FULL + GATE_HIGH_MV - 1U) / GATE_HIGH_MV;
  /* Direct millivolt-to-CCR conversion, rounded up to preserve the minimum. */
  compare = (millivolts * pwmPeriodCounts + GATE_HIGH_MV - 1U) / GATE_HIGH_MV;
  /* PWM1: CCR=0 is off; CCR=ARR+1 is on continuously.
     CCR preload applies changes at the next PWM period boundary. */
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, compare);
  if (forceUpdate != 0U)
  {
    /* Latch CCR now, rather than waiting for the next PWM update event. */
    htim1.Instance->EGR = TIM_EGR_UG;
  }
}

static void APP_MotorTask(uint32_t now)
{
  uint32_t elapsed;
  int32_t delta;
  if (rampActive == 0U)
  {
    return;
  }

  elapsed = (uint32_t)(now - rampStartedMs);
  if (elapsed >= RAMP_TIME_MS)
  {
    rampActive = 0U;
    if (motorRequestedOn == 0U)
    {
      /* The ramp has reached the measured 2.2 V stop point: switch off. */
      APP_MotorSetGateVoltage(0U, 1U);
      return;
    }
    APP_MotorSetGateVoltage(rampToMv, 0U);
  }
  else
  {
    uint32_t voltage;
    delta = (int32_t)rampToMv - (int32_t)rampFromMv;
    voltage = (uint32_t)((int32_t)rampFromMv +
                        delta * (int32_t)elapsed / (int32_t)RAMP_TIME_MS);
    APP_MotorSetGateVoltage(voltage, 0U);
  }
}

static void APP_MotorToggle(uint32_t now)
{
  /* Evaluate the old ramp first so changing direction never jumps duty. */
  APP_MotorTask(now);
  motorRequestedOn ^= 1U;
  if ((motorRequestedOn != 0U) && (gateVoltageMv == 0U))
  {
    /* Only a start from OFF jumps to 2.6 V. A live stop->start reversal
       below 2.6 V keeps its current value to preserve continuity. */
    APP_MotorSetGateVoltage(GATE_START_MV, 1U);
  }
  rampFromMv = gateVoltageMv;
  rampToMv = (motorRequestedOn != 0U) ? GATE_HIGH_MV : GATE_STOP_MV;
  rampStartedMs = now;
  rampActive = (rampFromMv != rampToMv) ? 1U : 0U;
  if ((rampActive == 0U) && (motorRequestedOn == 0U))
  {
    /* A stop requested exactly at the lower bound has no ramp left to run. */
    APP_MotorSetGateVoltage(0U, 1U);
  }
}

static void APP_KeyInit(void)
{
  GPIO_InitTypeDef gpio = {0};
  HAL_NVIC_DisableIRQ(EXTI2_3_IRQn);
  gpio.Pin = KEY_PIN;
  gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
  gpio.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(KEY_PORT, &gpio);

  /* Require a stable release first; ignore a key held at power-up. */
  keyStable = GPIO_PIN_RESET;
  keyLastEdgeMs = HAL_GetTick();
  keyEventPending = 1U;
  __HAL_GPIO_EXTI_CLEAR_IT(KEY_PIN);
  HAL_NVIC_ClearPendingIRQ(EXTI2_3_IRQn);
  HAL_NVIC_SetPriority(EXTI2_3_IRQn, 1U, 0U);
  HAL_NVIC_EnableIRQ(EXTI2_3_IRQn);
}

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
  if (pin == KEY_PIN)
  {
    /* Only record the interrupt; never delay inside the ISR. */
    keyLastEdgeMs = HAL_GetTick();
    keyEventPending = 1U;
  }
}

static void APP_KeyTask(void)
{
  uint32_t savedPrimask = __get_PRIMASK();
  uint32_t now;
  GPIO_PinState raw;
  uint8_t pressed = 0U;

  __disable_irq();
  /* Time must not precede the latest edge timestamp. */
  now = HAL_GetTick();
  if ((keyEventPending != 0U) &&
      ((uint32_t)(now - keyLastEdgeMs) >= DEBOUNCE_MS))
  {
    raw = HAL_GPIO_ReadPin(KEY_PORT, KEY_PIN);
    /* If an edge latched while IRQs were masked, let its ISR run first. */
    if (__HAL_GPIO_EXTI_GET_IT(KEY_PIN) == 0U)
    {
      keyEventPending = 0U;
      if (raw != keyStable)
      {
        keyStable = raw;
        pressed = (raw == GPIO_PIN_RESET) ? 1U : 0U;
      }
    }
  }
  __set_PRIMASK(savedPrimask);

  if (pressed != 0U)
  {
    APP_MotorToggle(now);
  }
}

int main(void)
{
  SystemCoreClockUpdate();
  if (HAL_Init() != HAL_OK)
  {
    APP_ErrorHandler();
  }
  APP_MotorForceOff();
  APP_PWM_Init();
  APP_KeyInit();

  while (1)
  {
    APP_KeyTask();
    APP_MotorTask(HAL_GetTick());
    /* SysTick wakes each millisecond; EXTI also wakes the CPU.
       Hardware PWM continues while the core sleeps. */
    __WFI();
  }
}

void APP_ErrorHandler(void)
{
  __disable_irq();
  APP_MotorForceOff();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
  APP_ErrorHandler();
}
#endif
