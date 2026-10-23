#ifndef MOTOR_TEST_MAIN_H
#define MOTOR_TEST_MAIN_H
#include <stdint.h>
typedef enum { GPIO_PIN_RESET=0, GPIO_PIN_SET=1 } GPIO_PinState;
typedef struct { uint32_t Pin,Mode,Pull,Speed,Alternate; } GPIO_InitTypeDef;
typedef struct { uint32_t EGR,CCR4,ARR,PSC; } TIM_TypeDef;
typedef struct { uint32_t CFGR; } RCC_TypeDef;
typedef struct { uint32_t Prescaler,CounterMode,Period,ClockDivision,RepetitionCounter,AutoReloadPreload; } TIM_Base_InitTypeDef;
typedef struct { TIM_TypeDef *Instance; TIM_Base_InitTypeDef Init; } TIM_HandleTypeDef;
typedef struct { uint32_t OCMode,Pulse,OCPolarity,OCNPolarity,OCFastMode,OCIdleState,OCNIdleState; } TIM_OC_InitTypeDef;
extern TIM_TypeDef testTim;
extern RCC_TypeDef testRcc;
extern uint32_t testPending,testPrimask;
#define TIM1 (&testTim)
#define RCC (&testRcc)
#define GPIOA ((void *)1)
#define GPIO_PIN_1 2U
#define GPIO_PIN_3 8U
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_MODE_AF_PP 2U
#define GPIO_MODE_IT_RISING_FALLING 3U
#define GPIO_NOPULL 0U
#define GPIO_PULLUP 1U
#define GPIO_SPEED_FREQ_HIGH 2U
#define GPIO_AF13_TIM1 13U
#define TIM_COUNTERMODE_UP 0U
#define TIM_CLOCKDIVISION_DIV1 0U
#define TIM_AUTORELOAD_PRELOAD_ENABLE 1U
#define TIM_OCMODE_PWM1 6U
#define TIM_OCPOLARITY_HIGH 0U
#define TIM_OCPOLARITY_LOW 1U
#define TIM_OCNPOLARITY_HIGH 0U
#define TIM_OCFAST_DISABLE 0U
#define TIM_OCIDLESTATE_RESET 0U
#define TIM_OCIDLESTATE_SET 1U
#define TIM_OCNIDLESTATE_RESET 0U
#define TIM_CHANNEL_4 12U
#define TIM_EGR_UG 1U
#define RCC_CFGR_PPRE 0x7000U
#define EXTI2_3_IRQn 6
#define HAL_OK 0
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM1_FORCE_RESET() ((void)0)
#define __HAL_RCC_TIM1_RELEASE_RESET() ((void)0)
#define __HAL_TIM_SET_COMPARE(h,c,v) Test_SetCompare((h),(c),(v))
#define __HAL_GPIO_EXTI_CLEAR_IT(p) (testPending &= ~(p))
#define __HAL_GPIO_EXTI_GET_IT(p) (testPending & (p))
#define __get_PRIMASK() (testPrimask)
#define __disable_irq() (testPrimask=1U)
#define __set_PRIMASK(p) (testPrimask=(p))
#define __WFI() ((void)0)
void SystemCoreClockUpdate(void);
int HAL_Init(void);
uint32_t HAL_GetTick(void);
void HAL_IncTick(void);
uint32_t HAL_RCC_GetPCLK1Freq(void);
void HAL_GPIO_WritePin(void *,uint16_t,GPIO_PinState);
void HAL_GPIO_Init(void *,GPIO_InitTypeDef *);
GPIO_PinState HAL_GPIO_ReadPin(void *,uint16_t);
void HAL_GPIO_EXTI_IRQHandler(uint16_t);
void HAL_GPIO_EXTI_Callback(uint16_t);
int HAL_TIM_PWM_Init(TIM_HandleTypeDef *);
int HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *,TIM_OC_InitTypeDef *,uint32_t);
int HAL_TIM_PWM_Start(TIM_HandleTypeDef *,uint32_t);
void HAL_NVIC_DisableIRQ(int);
void HAL_NVIC_ClearPendingIRQ(int);
void HAL_NVIC_SetPriority(int,uint32_t,uint32_t);
void HAL_NVIC_EnableIRQ(int);
void APP_ErrorHandler(void);
void Test_SetCompare(TIM_HandleTypeDef *,uint32_t,uint32_t);
#endif
