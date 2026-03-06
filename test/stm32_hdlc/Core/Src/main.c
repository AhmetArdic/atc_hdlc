/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include "hdlc.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* ---- Ring Buffer (RX) ---- */
#define RX_RING_SIZE       8192u    /* must be power of 2 */
#define RX_RING_MASK       (RX_RING_SIZE - 1u)

/* ---- TX Buffer ---- */
#define TX_BUF_SIZE        8192u

/* ---- HDLC Buffers ---- */
#define HDLC_INPUT_BUF_SIZE   (8192u)
#define HDLC_RETX_BUF_SIZE   (8192u)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_rx;
DMA_HandleTypeDef hdma_usart2_tx;

/* USER CODE BEGIN PV */

/* ---------- RX Ring Buffer ---------- */
static uint8_t  rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_head = 0;   /* written by ISR  */
static uint16_t rx_tail = 0;            /* read by main    */

/* ---------- TX Buffer (DMA) ---------- */
#define TX_RING_SIZE       8192u    /* must be power of 2 */
#define TX_RING_MASK       (TX_RING_SIZE - 1u)
static uint8_t  tx_ring[TX_RING_SIZE];
static volatile uint16_t tx_head = 0;   /* written by hdlc   */
static volatile uint16_t tx_tail = 0;   /* read by DMA       */
static volatile uint8_t  tx_dma_busy = 0;

/* ---------- HDLC ---------- */
static atc_hdlc_context_t hdlc_ctx;
static uint8_t hdlc_input_buf[HDLC_INPUT_BUF_SIZE];
static uint8_t hdlc_retx_buf[HDLC_RETX_BUF_SIZE];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
static void hdlc_output_cb(atc_hdlc_u8 byte, atc_hdlc_bool flush, void *user_data);
static void hdlc_on_frame_cb(const atc_hdlc_frame_t *frame, void *user_data);
static void hdlc_state_cb(atc_hdlc_protocol_state_t state, void *user_data);
static void tx_flush_dma(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief Flush the TX ring buffer via DMA.
 */
static void tx_flush_dma(void)
{
  __disable_irq(); // Protect state check
  if (tx_dma_busy || (tx_head == tx_tail)) {
    __enable_irq();
    return;
  }
  
  uint16_t head = tx_head;
  uint16_t tail = tx_tail;
  uint16_t len = 0;

  if (head > tail) {
    len = head - tail;
  } else {
    /* Send up to the end of the ring buffer first */
    len = TX_RING_SIZE - tail;
  }
  
  tx_dma_busy = 1;
  __enable_irq();
  
  HAL_UART_Transmit_DMA(&huart2, &tx_ring[tail], len);
}

/* ================================================================
 *  HDLC Callbacks
 * ================================================================ */

/**
 * @brief Output byte callback — appends to TX ring buffer and flushes on demand.
 */
static void hdlc_output_cb(atc_hdlc_u8 byte, atc_hdlc_bool flush, void *user_data)
{
  (void)user_data;

  /* Block if TX buffer is full to prevent byte dropping during high-load tests */
  uint16_t next_head = (tx_head + 1u) & TX_RING_MASK;
  while (next_head == tx_tail) {
    /* If we are full, we MUST flush any pending DMA and wait.
       This is critical for physical throughput tests at high baud rates. */
    tx_flush_dma();
    /* Small delay or yield could go here if using RTOS, otherwise just wait */
  }

  tx_ring[tx_head] = byte;
  tx_head = next_head;

  if (flush) {
    tx_flush_dma();
  }
}

/**
 * @brief Frame received callback.
 *        - I-frame data is echoed back as a UI frame.
 *        - ACK (RR) generation for I-frames is handled automatically
 *          by the HDLC library.
 */
static void hdlc_on_frame_cb(const atc_hdlc_frame_t *frame, void *user_data)
{
  (void)user_data;

  if (frame->type == ATC_HDLC_FRAME_I) {
    /* Echo payload back as UI */
    atc_hdlc_output_frame_ui(&hdlc_ctx, frame->address, frame->information, frame->information_len);
  }
  /* U-frames (SABM, DISC, etc.) and S-frames are handled by the library
     internally — no user action needed. */
}

/**
 * @brief Connection state change callback (informational).
 */
static void hdlc_state_cb(atc_hdlc_protocol_state_t state, void *user_data)
{
  (void)user_data;
  (void)state;
  /* Could toggle an LED here for visual feedback */
}

/* ================================================================
 *  HAL Callbacks (called from ISR context)
 * ================================================================ */

/**
 * @brief UART RX Complete (using DMA now, so we poll NDTR).
 *        Unused callback, handled in main polling loop.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  (void)huart;
}

/**
 * @brief UART TX complete.
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2) {
    uint16_t len = huart->TxXferSize;
    tx_tail = (tx_tail + len) & TX_RING_MASK;
    tx_dma_busy = 0;
    
    /* Start next transfer if queue is not empty */
    tx_flush_dma();
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

  /* Enable I-Cache, D-Cache and Prefetch Buffer for performance */
  __HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
  __HAL_FLASH_DATA_CACHE_ENABLE();
  __HAL_FLASH_PREFETCH_BUFFER_ENABLE();

  /* ---- Initialize HDLC ---- */
  atc_hdlc_init(&hdlc_ctx,
            hdlc_input_buf, sizeof(hdlc_input_buf),
            hdlc_retx_buf,  sizeof(hdlc_retx_buf),
            500,   	/* retransmit timeout (ticks) */
			1,   	/* ack timtout (ticks)*/
            7,     	/* window size */
            10,    	/* max retry count */
            hdlc_output_cb,
            hdlc_on_frame_cb,
            hdlc_state_cb,
            NULL);

  /* Target is address 0x02, peer (PC) is 0x01 */
  atc_hdlc_configure_addresses(&hdlc_ctx, 0x02, 0x01);

  /* Start receiving via DMA (Circular mode) */
  HAL_UART_Receive_DMA(&huart2, rx_ring, RX_RING_SIZE);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t last_tick = HAL_GetTick();

  while (1)
  {
    /* ---- Drain RX ring buffer into HDLC parser (DMA Polling) ---- */
    uint32_t rx_head = RX_RING_SIZE - __HAL_DMA_GET_COUNTER(huart2.hdmarx);
    if (rx_head == RX_RING_SIZE) {
        rx_head = 0; // Handle edge case if NDTR flips to reload value
    }

    while (rx_tail != rx_head) {
      atc_hdlc_input_byte(&hdlc_ctx, rx_ring[rx_tail]);
      rx_tail = (rx_tail + 1u) & RX_RING_MASK;
    }

    /* ---- Drive HDLC timers at ~1 ms resolution ---- */
    uint32_t now = HAL_GetTick();
    if (now != last_tick) {
      uint32_t elapsed = now - last_tick;
      for (uint32_t t = 0; t < elapsed; t++) {
        atc_hdlc_tick(&hdlc_ctx);
      }
      last_tick = now;
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_11;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the SYSCLKSource, HCLK, PCLK1 and PCLK2 clocks dividers
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK3|RCC_CLOCKTYPE_HCLK
                              |RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1
                              |RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.AHBCLK3Divider = RCC_SYSCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 921600;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMAMUX1_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
  /* DMA1_Channel2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);
  /* DMAMUX1_OVR_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMAMUX1_OVR_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMAMUX1_OVR_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
