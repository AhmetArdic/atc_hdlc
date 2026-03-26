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

/* ---- TX Ring Buffer (DMA) ---- */
#define TX_RING_SIZE       8192u    /* must be power of 2 */
#define TX_RING_MASK       (TX_RING_SIZE - 1u)

/* ---- HDLC Buffer Sizes ---- */
#define HDLC_MAX_FRAME_SIZE  512u                               /**< Max payload per I-frame (bytes). */
#define HDLC_RX_BUF_SIZE     (HDLC_MAX_FRAME_SIZE + 4u)        /**< Addr(1)+Ctrl(1)+Payload+FCS(2). */
#define HDLC_WINDOW_SIZE     7u

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_rx;
DMA_HandleTypeDef hdma_usart2_tx;

/* USER CODE BEGIN PV */

/* ---------- RX Ring Buffer (written by DMA, read by main loop) ---------- */
static uint8_t           rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_head = 0;  /* written by ISR  */
static uint16_t          rx_tail = 0;  /* read by main    */

/* ---------- TX Ring Buffer (written by HDLC, read by DMA) ---------- */
static uint8_t           tx_ring[TX_RING_SIZE];
static volatile uint16_t tx_head    = 0;
static volatile uint16_t tx_tail    = 0;
static volatile uint8_t  tx_dma_busy = 0;

/* ---------- HDLC station ---------- */
static atc_hdlc_context_t  hdlc_ctx;
static atc_hdlc_config_t   hdlc_cfg;
static atc_hdlc_platform_t hdlc_plat;
static atc_hdlc_tx_window_t hdlc_tw;
static atc_hdlc_rx_buffer_t hdlc_rx;

/* Static storage for HDLC buffers */
static uint8_t  hdlc_rx_buf[HDLC_RX_BUF_SIZE];
static uint8_t  hdlc_tx_slots[HDLC_WINDOW_SIZE * HDLC_MAX_FRAME_SIZE];
static uint32_t hdlc_tx_lens[HDLC_WINDOW_SIZE];
static uint8_t  hdlc_tx_seq[8];  /* seq_to_slot map: indexed by V(S) 0..7 (mod-8) */

/* Timer state flags (set/cleared by platform callbacks) */
static volatile uint8_t  t1_active     = 0;
static volatile uint32_t t1_started_ms = 0;  /* HAL_GetTick() snapshot */
static volatile uint8_t  t2_active     = 0;
static volatile uint32_t t2_started_ms = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
static int  hdlc_on_send_cb(atc_hdlc_u8 byte, bool flush, void *user_data);
static void hdlc_on_data_cb(const atc_hdlc_u8 *data, atc_hdlc_u16 len, void *user_data);
static void hdlc_on_event_cb(atc_hdlc_event_t event, void *user_data);
static void hdlc_t1_start_cb(atc_hdlc_u32 ms, void *user_data);
static void hdlc_t1_stop_cb(void *user_data);
static void hdlc_t2_start_cb(atc_hdlc_u32 ms, void *user_data);
static void hdlc_t2_stop_cb(void *user_data);
static void tx_flush_dma(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief Flush the TX ring buffer via DMA (non-blocking, ISR-safe).
 */
static void tx_flush_dma(void)
{
    __disable_irq();
    if (tx_dma_busy || (tx_head == tx_tail)) {
        __enable_irq();
        return;
    }

    uint16_t head = tx_head;
    uint16_t tail = tx_tail;
    uint16_t len;

    if (head > tail) {
        len = head - tail;
    } else {
        /* Wrap-around: send from tail to end of buffer first */
        len = TX_RING_SIZE - tail;
    }

    tx_dma_busy = 1;
    __enable_irq();

    if (HAL_UART_Transmit_DMA(&huart2, &tx_ring[tail], len) != HAL_OK) {
        __disable_irq();
        tx_dma_busy = 0;
        __enable_irq();
    }
}

/* ================================================================
 *  HDLC Platform Callbacks
 * ================================================================ */

/**
 * @brief Physical byte-output callback.
 *        Appends byte to TX ring buffer and triggers DMA on frame end.
 */
static int hdlc_on_send_cb(atc_hdlc_u8 byte, bool flush, void *user_data)
{
    (void)user_data;

    /* Block if TX ring is full — DMA will drain it */
    uint16_t next_head = (tx_head + 1u) & TX_RING_MASK;
    while (next_head == tx_tail) {
        tx_flush_dma();
    }

    tx_ring[tx_head] = byte;
    tx_head = next_head;

    if (flush) {
        tx_flush_dma();
    }

    return 0;
}

/**
 * @brief Upper-layer data delivery callback.
 *        Called once per complete, valid I-frame or UI-frame received.
 *        Echoes payload back as a UI frame for demonstration.
 */
static void hdlc_on_data_cb(const atc_hdlc_u8 *data, atc_hdlc_u16 len, void *user_data)
{
    (void)user_data;

    /* Echo payload back as UI to the peer */
    atc_hdlc_transmit_ui(&hdlc_ctx, hdlc_ctx.peer_address, data, len);
}

/**
 * @brief Event notification callback.
 *        Could toggle an LED or set a flag for the application layer.
 */
static void hdlc_on_event_cb(atc_hdlc_event_t event, void *user_data)
{
    (void)user_data;
    (void)event;
    /* Example: HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin); */
}

/**
 * @brief T1 retransmission timer start callback.
 *        Records the start time; main loop checks expiry.
 */
static void hdlc_t1_start_cb(atc_hdlc_u32 ms, void *user_data)
{
    (void)ms;
    (void)user_data;
    t1_started_ms = HAL_GetTick();
    t1_active     = 1;
}

/**
 * @brief T1 retransmission timer stop callback.
 */
static void hdlc_t1_stop_cb(void *user_data)
{
    (void)user_data;
    t1_active = 0;
}

/**
 * @brief T2 delayed-ACK timer start callback.
 *
 * On a bare-metal main loop driven by HAL_GetTick (1ms resolution),
 * waiting a full t2_ms before ACKing significantly reduces throughput
 * at high baud rates (each 512-byte frame takes ~5.5ms at 921600).
 * Fire T2 immediately so the RR goes out on the same DMA burst as the
 * last received byte, maximising pipelining with the sender.
 */
static void hdlc_t2_start_cb(atc_hdlc_u32 ms, void *user_data)
{
    (void)ms;
    (void)user_data;
    /* Immediate expiry — set flag for main loop to fire on next iteration */
    t2_started_ms = 0;   /* epoch trick: always elapsed */
    t2_active     = 1;
}

/**
 * @brief T2 delayed-ACK timer stop callback.
 */
static void hdlc_t2_stop_cb(void *user_data)
{
    (void)user_data;
    t2_active = 0;
}

/* ================================================================
 *  HAL Callbacks (called from ISR context)
 * ================================================================ */

/**
 * @brief UART TX DMA complete — advance tail pointer and chain next transfer.
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        uint16_t len = huart->TxXferSize;
        tx_tail    = (tx_tail + len) & TX_RING_MASK;
        tx_dma_busy = 0;
        tx_flush_dma(); /* chain next chunk if ring not empty */
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

  /* Enable Flash caches for performance */
  __HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
  __HAL_FLASH_DATA_CACHE_ENABLE();
  __HAL_FLASH_PREFETCH_BUFFER_ENABLE();

  /* ---- Build HDLC config ---- */
  hdlc_cfg.mode           = ATC_HDLC_MODE_ABM;
  hdlc_cfg.address        = 0x02;          /* This station (STM32) */
  hdlc_cfg.window_size    = HDLC_WINDOW_SIZE;
  hdlc_cfg.max_frame_size = HDLC_MAX_FRAME_SIZE;
  hdlc_cfg.max_retries    = 10;
  hdlc_cfg.t1_ms          = 500;           /* Retransmission timeout */
  hdlc_cfg.t2_ms          = 1;             /* Delayed-ACK timeout (1ms = 1 HAL tick) */
  hdlc_cfg.t3_ms          = 0;             /* Keep-alive disabled */
  hdlc_cfg.use_extended   = false;

  /* ---- Build platform callbacks ---- */
  hdlc_plat.on_send   = hdlc_on_send_cb;
  hdlc_plat.on_data   = hdlc_on_data_cb;
  hdlc_plat.on_event  = hdlc_on_event_cb;
  hdlc_plat.user_ctx  = NULL;
  hdlc_plat.t1_start  = hdlc_t1_start_cb;
  hdlc_plat.t1_stop   = hdlc_t1_stop_cb;
  hdlc_plat.t2_start  = hdlc_t2_start_cb;
  hdlc_plat.t2_stop   = hdlc_t2_stop_cb;
  hdlc_plat.t3_start  = NULL;              /* T3 not used */
  hdlc_plat.t3_stop   = NULL;

  /* ---- Build TX window descriptor ---- */
  hdlc_tw.slots        = hdlc_tx_slots;
  hdlc_tw.slot_lens    = hdlc_tx_lens;
  hdlc_tw.seq_to_slot  = hdlc_tx_seq;
  hdlc_tw.slot_count   = HDLC_WINDOW_SIZE;
  hdlc_tw.slot_capacity = HDLC_MAX_FRAME_SIZE;

  /* ---- Build RX buffer descriptor ---- */
  hdlc_rx.buffer   = hdlc_rx_buf;
  hdlc_rx.capacity = sizeof(hdlc_rx_buf);  /* HDLC_RX_BUF_SIZE = payload + 4 bytes overhead */

  /* ---- Initialize HDLC station ---- */
  atc_hdlc_params_t hdlc_params = { .config = &hdlc_cfg, .platform = &hdlc_plat,
                                     .tx_window = &hdlc_tw, .rx_buf = &hdlc_rx };
  atc_hdlc_init(&hdlc_ctx, hdlc_params);

  /* ---- Initiate connection to peer (PC = address 0x01) ---- */
  atc_hdlc_link_setup(&hdlc_ctx, 0x01);

  /* ---- Start RX DMA (Circular mode) ---- */
  HAL_UART_Receive_DMA(&huart2, rx_ring, RX_RING_SIZE);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* ---- Drain RX ring buffer into HDLC parser ---- */
    uint32_t dma_head = RX_RING_SIZE - __HAL_DMA_GET_COUNTER(huart2.hdmarx);
    if (dma_head == RX_RING_SIZE) dma_head = 0;

    if (rx_tail != (uint16_t)dma_head) {
        if (dma_head > rx_tail) {
            atc_hdlc_data_in(&hdlc_ctx, &rx_ring[rx_tail], dma_head - rx_tail);
            rx_tail = (uint16_t)dma_head;
        } else {
            /* Wrap-around: linear tail→end, then 0→head */
            atc_hdlc_data_in(&hdlc_ctx, &rx_ring[rx_tail], RX_RING_SIZE - rx_tail);
            rx_tail = 0;
            if (dma_head > 0) {
                atc_hdlc_data_in(&hdlc_ctx, rx_ring, dma_head);
                rx_tail = (uint16_t)dma_head;
            }
        }
    }

    /* ---- Fire T1 expiry if timeout has elapsed ---- */
    if (t1_active) {
        uint32_t now = HAL_GetTick();
        if ((now - t1_started_ms) >= hdlc_cfg.t1_ms) {
            t1_active = 0;
            atc_hdlc_t1_expired(&hdlc_ctx);
        }
    }

    /* ---- Fire T2 expiry if timeout has elapsed ---- */
    if (t2_active) {
        uint32_t now = HAL_GetTick();
        if ((now - t2_started_ms) >= hdlc_cfg.t2_ms) {
            t2_active = 0;
            atc_hdlc_t2_expired(&hdlc_ctx);
        }
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
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_RXOVERRUNDISABLE_INIT;
  huart2.AdvancedInit.OverrunDisable = UART_ADVFEATURE_OVERRUN_DISABLE;
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
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
