# STM32H5 CubeMX 코드 생성 버그 및 패치 가이드

이 문서는 STM32CubeMX로 코드를 재생성할 때 발생하는 알려진 버그들과 해결 방법을 정리합니다.

**⚠️ 중요**: CubeMX로 코드를 재생성한 후에는 **반드시 이 문서의 패치들을 적용**해야 합니다!

---

## 버그 #1: GPDMA2 DAC DMA 초기화 방식 불일치 (CRITICAL)

### 증상
- DAC DMA Circular 모드가 작동하지 않음
- DMA 전송이 한 번만 실행되고 멈춤
- `HAL_DAC_Start_DMA()` 호출 후 DMA가 활성화되지 않음

### 원인
CubeMX가 DAC용 GPDMA2를 설정할 때 **채널별로 다른 초기화 방식**을 생성:
- **Channel 0 (DAC1_CH1)**: Direct Init 방식 (❌ Circular 불가)
- **Channel 1 (DAC1_CH2)**: LinkedList Init 방식 (✅ Circular 가능)

**영향받는 파일**: `Core/Src/stm32h5xx_hal_msp.c`

### 패치 방법

#### 옵션 1: CubeMX 설정 변경 (권장)
1. STM32CubeMX 열기
2. **Connectivity** → **GPDMA2** 클릭
3. **Configuration** → **Channel 0** 설정
4. **Mode** 항목:
   - 현재: "Request" (Direct Init)
   - 변경: **"Linked-List Queue"** 선택
5. **Circular Mode**: Enable 확인
6. **Generate Code** → 재생성

#### 옵션 2: 수동 패치 (임시)

`Core/Src/stm32h5xx_hal_msp.c`의 `HAL_DAC_MspInit()` 함수에서 Channel 0 초기화 부분을 수정:

**변경 전** (약 Line 199~224):
```c
/* GPDMA2_REQUEST_DAC1_CH1 Init */
handle_GPDMA2_Channel0.Instance = GPDMA2_Channel0;
handle_GPDMA2_Channel0.Init.Request = GPDMA2_REQUEST_DAC1_CH1;
handle_GPDMA2_Channel0.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
// ... (Direct Init 방식)
if (HAL_DMA_Init(&handle_GPDMA2_Channel0) != HAL_OK)  // ❌
{
  Error_Handler();
}
```

**변경 후** (Channel 1과 동일하게 LinkedList 방식으로):
```c
/* GPDMA2_REQUEST_DAC1_CH1 Init */
NodeConfig.NodeType = DMA_GPDMA_LINEAR_NODE;
NodeConfig.Init.Request = GPDMA2_REQUEST_DAC1_CH1;
NodeConfig.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
NodeConfig.Init.Direction = DMA_MEMORY_TO_PERIPH;
NodeConfig.Init.SrcInc = DMA_SINC_INCREMENTED;
NodeConfig.Init.DestInc = DMA_DINC_FIXED;
NodeConfig.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_HALFWORD;
NodeConfig.Init.DestDataWidth = DMA_DEST_DATAWIDTH_HALFWORD;
NodeConfig.Init.SrcBurstLength = 1;
NodeConfig.Init.DestBurstLength = 1;
NodeConfig.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT0;
NodeConfig.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
NodeConfig.Init.Mode = DMA_NORMAL;
NodeConfig.TriggerConfig.TriggerPolarity = DMA_TRIG_POLARITY_MASKED;
NodeConfig.DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
NodeConfig.DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;

if (HAL_DMAEx_List_BuildNode(&NodeConfig, &Node_GPDMA2_Channel0) != HAL_OK)
{
  Error_Handler();
}

if (HAL_DMAEx_List_InsertNode(&List_GPDMA2_Channel0, NULL, &Node_GPDMA2_Channel0) != HAL_OK)
{
  Error_Handler();
}

if (HAL_DMAEx_List_SetCircularMode(&List_GPDMA2_Channel0) != HAL_OK)
{
  Error_Handler();
}

handle_GPDMA2_Channel0.Instance = GPDMA2_Channel0;
handle_GPDMA2_Channel0.InitLinkedList.Priority = DMA_HIGH_PRIORITY;
handle_GPDMA2_Channel0.InitLinkedList.LinkStepMode = DMA_LSM_FULL_EXECUTION;
handle_GPDMA2_Channel0.InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT0;
handle_GPDMA2_Channel0.InitLinkedList.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
handle_GPDMA2_Channel0.InitLinkedList.LinkedListMode = DMA_LINKEDLIST_CIRCULAR;

if (HAL_DMAEx_List_Init(&handle_GPDMA2_Channel0) != HAL_OK)
{
  Error_Handler();
}

if (HAL_DMAEx_List_LinkQ(&handle_GPDMA2_Channel0, &List_GPDMA2_Channel0) != HAL_OK)
{
  Error_Handler();
}
```

**또한 `Core/Src/main.c`에 변수 선언 추가 필요**:
```c
// 기존 (Line 49 근처)
DMA_HandleTypeDef handle_GPDMA2_Channel0;

// 추가 필요
DMA_NodeTypeDef Node_GPDMA2_Channel0;
DMA_QListTypeDef List_GPDMA2_Channel0;
```

**그리고 `Core/Src/stm32h5xx_hal_msp.c`와 `Core/Src/stm32h5xx_it.c`에 extern 선언 추가**:
```c
extern DMA_NodeTypeDef Node_GPDMA2_Channel0;
extern DMA_QListTypeDef List_GPDMA2_Channel0;
```

### 검증 방법
```c
// DAC DMA 시작 후 확인
HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, buffer, size, DAC_ALIGN_12B_R);

// DMA 레지스터 확인
uint32_t ccr = GPDMA2_Channel0->CCR;
printf("DMA CCR: 0x%08lX (EN=%d)\r\n", ccr, (ccr & DMA_CCR_EN) ? 1 : 0);
// EN 비트가 1이어야 정상
```

---

## 버그 #2: DMA Node Type이 2D로 설정되는 경우

### 증상
- DMA 전송이 전혀 시작되지 않음
- `HAL_DAC_Start_DMA()` 반환값이 HAL_ERROR

### 원인
CubeMX가 DAC DMA를 2D_NODE로 설정하는 경우가 있음. DAC는 1차원 버퍼 전송이므로 LINEAR_NODE를 사용해야 함.

### 패치 방법

**CubeMX 설정**:
1. GPDMA2 → Channel 0/1 설정
2. **Node Type**: "2D Addressing" → **"Linear Addressing"** 변경
3. Generate Code

**수동 패치** (`stm32h5xx_hal_msp.c`):
```c
// 변경 전
NodeConfig.NodeType = DMA_GPDMA_2D_NODE;
NodeConfig.RepeatBlockConfig.RepeatCount = 1;  // 2D 설정
// ...

// 변경 후
NodeConfig.NodeType = DMA_GPDMA_LINEAR_NODE;
// RepeatBlockConfig 관련 설정 제거
```

---

## 버그 #3: DCACHE 활성화 시 DMA 일관성 문제

### 증상
- DMA로 전송한 데이터가 주변장치에 반영되지 않음
- 랜덤하게 작동하거나 오래된 데이터가 전송됨

### 원인
STM32H5의 DCACHE가 활성화되면 DMA 전송 전/후에 캐시 동기화 필요

### 패치 방법

**DMA TX 전** (메모리 → 주변장치):
```c
// 캐시에서 메모리로 플러시
SCB_CleanDCache_by_Addr((uint32_t*)buffer, size * sizeof(uint16_t));

// 그 후 DMA 시작
HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, buffer, size, DAC_ALIGN_12B_R);
```

**DMA RX 후** (주변장치 → 메모리):
```c
// DMA 완료 콜백에서
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    // 캐시 무효화 (메모리에서 다시 읽기)
    SCB_InvalidateDCache_by_Addr((uint32_t*)rx_buffer, rx_size);

    // 이제 rx_buffer 사용 가능
}
```

**대안**: DMA 버퍼를 캐시 불가 영역에 배치
```c
// 링커 스크립트에 .dma_buffer 섹션 추가
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static uint8_t dma_buffer[4096];

// MPU로 해당 영역을 캐시 불가로 설정
// (현재 프로젝트에서는 이미 적용됨)
```

---

## 버그 #4: DAC Clock Source 설정 오류

### 증상
- DAC 출력 주파수가 예상과 다름
- 일부 설정에서 DAC가 작동하지 않음

### 원인
CubeMX가 DAC 클럭을 LSI (32kHz)로 설정하는 경우가 있음

### 패치 방법

**CubeMX 설정** (`audio_dac_v102.ioc` 또는 GUI):
1. **Clock Configuration** 탭
2. **DAC Clock Mux** 찾기
3. **HCLK** 또는 **PLL** 선택 (250MHz)
4. LSI (32kHz)는 저전력 애플리케이션 전용

**수동 확인** (`stm32h5xx_hal_msp.c`):
```c
void HAL_DAC_MspInit(DAC_HandleTypeDef* hdac)
{
  // ...
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_ADCDAC|RCC_PERIPHCLK_DAC;
  PeriphClkInitStruct.AdcDacClockSelection = RCC_ADCDACCLKSOURCE_HCLK;  // ✅ HCLK
  // PeriphClkInitStruct.AdcDacClockSelection = RCC_ADCDACCLKSOURCE_LSI;  // ❌ 32kHz만
}
```

---

## 버그 #5: User Code 섹션 보존 실패

### 증상
- CubeMX 재생성 후 사용자 코드가 사라짐

### 원인
`/* USER CODE BEGIN */` ~ `/* USER CODE END */` 외부에 코드 작성

### 패치 방법

**절대 수정하지 말 것**:
- `main.c`의 USER CODE 섹션 외부
- 함수 프로토타입 선언부
- 자동 생성된 초기화 함수 내부

**사용자 코드 위치**:
- `user_def.c` / `user_def.h` (권장)
- `main.c`의 `/* USER CODE BEGIN */` 섹션 내부만

---

## 체크리스트: 코드 재생성 후 확인 사항

코드를 재생성한 후 다음 항목들을 **반드시 확인**하세요:

- [ ] **GPDMA2 Channel 0**: LinkedList 방식으로 초기화됨?
- [ ] **GPDMA2 Channel 1**: LinkedList 방식으로 초기화됨?
- [ ] **DMA Node Type**: LINEAR_NODE로 설정됨?
- [ ] **DMA Circular Mode**: 활성화됨?
- [ ] **DAC Clock**: HCLK (250MHz)로 설정됨?
- [ ] **DCACHE**: 캐시 동기화 코드가 유지됨?
- [ ] **User Code**: `user_def.c`의 커스텀 코드가 보존됨?
- [ ] **빌드 성공**: 문법 오류 없이 컴파일됨?

---

## 추가 참고사항

### HAL 버전 확인
```bash
# Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal.h
grep "HAL_VERSION" Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal.h
```

최소 권장 버전: **STM32Cube_FW_H5_V1.2.0 이상**

### 관련 ST 문서
- [AN5557: GPDMA on STM32H5](https://www.st.com/resource/en/application_note/an5557-gpdma-on-stm32h5-series-stmicroelectronics.pdf)
- [RM0481: STM32H523 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0481-stm32h523-533-and-stm32h562-reference-manual-stmicroelectronics.pdf)
- [Errata Sheet STM32H523](https://www.st.com/resource/en/errata_sheet/es0552-stm32h523533-device-errata-stmicroelectronics.pdf)

---

**마지막 업데이트**: 2025-11-12
**STM32CubeMX 버전**: 6.x
**HAL 버전**: V1.5.0
