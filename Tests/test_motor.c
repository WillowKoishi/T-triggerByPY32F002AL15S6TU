#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "main.h"
#define main firmware_main
#include "../Src/main.c"
#undef main
#include "../Src/py32f0xx_it.c"

TIM_TypeDef testTim;
RCC_TypeDef testRcc;
uint32_t testPending,testPrimask;
static uint32_t clockMs,pclk=8000000U;
static GPIO_PinState key,pinOut;
static unsigned readCount,irqEnabled,pinMode;
static unsigned compareWrites;
static uint32_t lastNonzeroCompare;

void Test_SetCompare(TIM_HandleTypeDef *h,uint32_t channel,uint32_t value)
{
  uint32_t minimum=(GATE_STOP_MV*pwmPeriodCounts+GATE_HIGH_MV-1U)/GATE_HIGH_MV;
  assert(channel==TIM_CHANNEL_4);
  assert(value==0U || value>=minimum);
  if(value!=0U) lastNonzeroCompare=value;
  ++compareWrites;
  h->Instance->CCR4=value;
}

void SystemCoreClockUpdate(void) {}
int HAL_Init(void) { return HAL_OK; }
uint32_t HAL_GetTick(void) { return clockMs; }
void HAL_IncTick(void) { ++clockMs; }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return pclk; }
void HAL_GPIO_WritePin(void *p,uint16_t n,GPIO_PinState v)
{ assert(p==GPIOA && n==GPIO_PIN_1); pinOut=v; }
void HAL_GPIO_Init(void *p,GPIO_InitTypeDef *g)
{
  assert(p==GPIOA);
  if(g->Pin==GPIO_PIN_1) {
    pinMode=g->Mode;
    if(g->Mode==GPIO_MODE_AF_PP) assert(g->Alternate==13U);
  } else {
    assert(g->Pin==GPIO_PIN_3 && g->Mode==GPIO_MODE_IT_RISING_FALLING);
    assert(g->Pull==GPIO_PULLUP);
  }
}
GPIO_PinState HAL_GPIO_ReadPin(void *p,uint16_t n)
{ assert(p==GPIOA && n==GPIO_PIN_3); ++readCount; return key; }
void HAL_GPIO_EXTI_IRQHandler(uint16_t n)
{
  if(testPending & n) { testPending &= ~n; HAL_GPIO_EXTI_Callback(n); }
}
int HAL_TIM_PWM_Init(TIM_HandleTypeDef *h)
{
  testTim.ARR=h->Init.Period; testTim.PSC=h->Init.Prescaler;
  assert(h->Init.AutoReloadPreload==TIM_AUTORELOAD_PRELOAD_ENABLE);
  return HAL_OK;
}
int HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *h,TIM_OC_InitTypeDef *o,uint32_t c)
{
  assert(h->Instance==TIM1 && c==TIM_CHANNEL_4);
  assert(o->OCMode==TIM_OCMODE_PWM1 && o->Pulse==0U);
  assert(o->OCPolarity==MOTOR_PWM_POLARITY);
  testTim.CCR4=o->Pulse;
  return HAL_OK;
}
int HAL_TIM_PWM_Start(TIM_HandleTypeDef *h,uint32_t c)
{ assert(h->Instance==TIM1 && c==TIM_CHANNEL_4 && testTim.CCR4==0U); return HAL_OK; }
void HAL_NVIC_DisableIRQ(int n) { assert(n==EXTI2_3_IRQn); irqEnabled=0; }
void HAL_NVIC_ClearPendingIRQ(int n) { assert(n==EXTI2_3_IRQn); }
void HAL_NVIC_SetPriority(int n,uint32_t a,uint32_t b)
{ assert(n==EXTI2_3_IRQn && a==1U && b==0U); }
void HAL_NVIC_EnableIRQ(int n) { assert(n==EXTI2_3_IRQn); irqEnabled=1; }

static void reset_at(uint32_t ms,GPIO_PinState initial)
{
  /* Model the zero-initialized state produced by the C startup. */
  memset(&htim1,0,sizeof(htim1)); memset(&testTim,0,sizeof(testTim));
  dutyNow=gateVoltageMv=rampFromMv=rampToMv=rampStartedMs=0U;
  rampActive=motorRequestedOn=0U;
  testPrimask=testPending=0U; readCount=0; compareWrites=0; lastNonzeroCompare=0;
  clockMs=ms; key=initial;
  APP_MotorForceOff();
  assert(pinOut==MOTOR_OFF_LEVEL && pinMode==GPIO_MODE_OUTPUT_PP);
  APP_PWM_Init(); APP_KeyInit();
  assert(testTim.ARR==399U && testTim.CCR4==0U && irqEnabled);
}
static void service(uint32_t ms)
{
  clockMs=ms; APP_KeyTask(); APP_MotorTask(clockMs);
  assert(testTim.CCR4<=400U && dutyNow<=DUTY_FULL && testPrimask==0U);
  assert(dutyNow==0U || dutyNow>=GATE_STOP_DUTY);
  assert(gateVoltageMv==0U || gateVoltageMv>=GATE_STOP_MV);
}
static void edge(uint32_t ms,GPIO_PinState value)
{
  clockMs=ms; key=value; testPending|=GPIO_PIN_3;
  EXTI2_3_IRQHandler();
}
static void released(uint32_t ms)
{ edge(ms,GPIO_PIN_SET); service(ms+20U); }
static void pressed(uint32_t ms)
{ edge(ms,GPIO_PIN_RESET); service(ms+20U); }

static double output_average(void)
{
  return ((double)GATE_HIGH_MV/1000.0)*(double)testTim.CCR4/400.0;
}

/* Independent floating-point check of the INTEGER firmware output envelope.
   The 12 mV bound includes the timer's 0.25% duty quantization. */
static void check_average_segment(uint32_t started,double from,double to,uint32_t duration)
{
  uint32_t previous=testTim.CCR4;
  for(uint32_t t=0;t<duration;++t)
  {
    double expected=from+(to-from)*(double)t/5000.0;
    service(started+t);
    assert(fabs(output_average()-expected)<0.012);
    if(to>from) assert(testTim.CCR4>=previous);
    else assert(testTim.CCR4<=previous);
    previous=testTim.CCR4;
  }
}

int main(void)
{
  unsigned reads;
  reset_at(0,GPIO_PIN_SET); service(20);
  edge(100,GPIO_PIN_RESET); service(119); assert(!motorRequestedOn);
  testTim.EGR=0U;
  service(120); assert(motorRequestedOn && testTim.CCR4==316U);
  assert(gateVoltageMv==2600U && rampFromMv==2600U && testTim.EGR==TIM_EGR_UG);
  assert(compareWrites==3U); /* init zero, immediate minimum, ramp at t=0 */
  service(2620); assert(testTim.CCR4==358U);
  service(5120); assert(testTim.CCR4==400U && !rampActive);
  reads=readCount; service(10000); assert(readCount==reads && motorRequestedOn);
  released(10300); pressed(10400);
  service(12920); assert(testTim.CCR4==334U);
  service(15419); assert(testTim.CCR4==267U && rampActive);
  assert(lastNonzeroCompare==267U); testTim.EGR=0U;
  service(15420); assert(testTim.CCR4==0U && !rampActive);
  assert(gateVoltageMv==0U && testTim.EGR==TIM_EGR_UG);
  puts("PASS immediate 2.6V gate start, 5-second ramp, forced zero at 2.2V stop");

  released(16000); pressed(16100); service(18120);
  assert(testTim.CCR4==350U);
  released(18130); pressed(18200);
  assert(gateVoltageMv==2894U && testTim.CCR4==351U && !motorRequestedOn);
  service(20720); assert(testTim.CCR4==309U);
  released(20730); pressed(20800);
  assert(gateVoltageMv==2534U && testTim.CCR4==308U && motorRequestedOn);
  service(23320); assert(testTim.CCR4==354U);
  service(25820); assert(testTim.CCR4==400U && !rampActive);
  puts("PASS mid-start stop and mid-stop start, continuous duty, fresh 5-second ramp");

  reset_at(0,GPIO_PIN_SET); service(20);
  edge(100,GPIO_PIN_RESET); edge(105,GPIO_PIN_SET); edge(110,GPIO_PIN_RESET);
  edge(115,GPIO_PIN_SET); edge(120,GPIO_PIN_RESET);
  service(139); assert(!motorRequestedOn);
  service(140); assert(motorRequestedOn);
  edge(200,GPIO_PIN_SET); edge(205,GPIO_PIN_RESET); service(250);
  assert(motorRequestedOn);
  released(300);
  edge(400,GPIO_PIN_RESET); edge(410,GPIO_PIN_SET); service(440);
  assert(motorRequestedOn);
  pressed(500); assert(!motorRequestedOn);
  puts("PASS bounce rejection, stable-release re-arm, short-press rejection");

  reset_at(0,GPIO_PIN_RESET); service(1000); assert(!motorRequestedOn);
  released(1100); pressed(1200); assert(motorRequestedOn);
  puts("PASS held key at startup is ignored");

  reset_at(0,GPIO_PIN_SET); service(20);
  edge(100,GPIO_PIN_RESET);
  clockMs=120; key=GPIO_PIN_SET; testPending=GPIO_PIN_3;
  APP_KeyTask(); assert(!motorRequestedOn && keyEventPending);
  EXTI2_3_IRQHandler(); service(140);
  assert(!motorRequestedOn);
  puts("PASS hardware edge pending during debounce does not create a false press");

  reset_at(UINT32_C(0xFFFFFFB0),GPIO_PIN_SET);
  service(UINT32_C(0xFFFFFFC4));
  edge(UINT32_C(0xFFFFFFF8),GPIO_PIN_RESET);
  service(11); assert(!motorRequestedOn);
  service(12); assert(motorRequestedOn);
  service(2512); assert(testTim.CCR4==358U);
  service(5012); assert(testTim.CCR4==400U);
  reset_at(UINT32_C(0xFFFFF000),GPIO_PIN_SET); service(UINT32_C(0xFFFFF014));
  pressed(UINT32_C(0xFFFFF020));
  service((uint32_t)(UINT32_C(0xFFFFF034)+2500U)); assert(testTim.CCR4==358U);
  service((uint32_t)(UINT32_C(0xFFFFF034)+5000U)); assert(testTim.CCR4==400U);
  puts("PASS debounce and ramp across 32-bit millisecond wraparound");

  clockMs=6000; HAL_GPIO_EXTI_Callback(GPIO_PIN_1);
  assert(!keyEventPending);
  APP_MotorForceOff(); assert(pinOut==MOTOR_OFF_LEVEL && pinMode==GPIO_MODE_OUTPUT_PP);
  puts("PASS other EXTI lines ignored and GPIO forced-off path");
  pclk=4000000U; testRcc.CFGR=0x1000U;
  reset_at(0,GPIO_PIN_SET);
  assert(testTim.ARR==399U);
  testPrimask=1U; clockMs=20; APP_KeyTask(); assert(testPrimask==1U);
  testPrimask=0U;
  puts("PASS APB timer-clock doubling and saved interrupt-mask restoration");
  reset_at(0,GPIO_PIN_SET); service(20);
  APP_MotorToggle(100U);
  assert(dutyNow==7879U && testTim.CCR4==316U);
  APP_MotorToggle(100U);
  assert(rampActive && !motorRequestedOn && gateVoltageMv==2600U && rampToMv==2200U);
  service(5099U); assert(testTim.CCR4==267U);
  service(5100U); assert(!rampActive && testTim.CCR4==0U);
  puts("PASS immediate stop during start uses the separate 2.2V stop threshold");
  pclk=8000000U; testRcc.CFGR=0U;
  reset_at(0,GPIO_PIN_SET); service(20); APP_MotorToggle(100U);
  double start=output_average();
  double stop=3.3*267.0/400.0;
  check_average_segment(100U,start,3.3,5000U);
  service(5100U); assert(testTim.CCR4==400U);
  APP_MotorToggle(6000U);
  check_average_segment(6000U,3.3,stop,5000U);
  service(11000U); assert(testTim.CCR4==0U);
  puts("PASS linear gate DC target at every millisecond (<12mV error)");
  reset_at(0,GPIO_PIN_SET); service(20); APP_MotorToggle(100U);
  service(2100U);
  uint32_t before=testTim.CCR4;
  testTim.EGR=0U;
  APP_MotorToggle(2100U); assert(testTim.CCR4==before && testTim.EGR==0U);
  check_average_segment(2100U,output_average(),stop,2000U);
  service(4100U); before=testTim.CCR4;
  testTim.EGR=0U;
  APP_MotorToggle(4100U); assert(testTim.CCR4==before && testTim.EGR==0U);
  check_average_segment(4100U,output_average(),3.3,5000U);
  service(9100U); assert(testTim.CCR4==400U);
  puts("PASS DC-linear reversal segments and exact PWM continuity");
  return 0;
}
