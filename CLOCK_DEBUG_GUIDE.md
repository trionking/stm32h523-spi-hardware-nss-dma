# STM32H523 클럭 설정 디버깅 가이드

## 개요

STM32H523 프로젝트에서 외부 클럭(HSE)에서 내부 클럭(HSI)으로 변경하는 과정에서 발생한 문제와 해결 방법을 정리한 문서입니다.

---

## 문제 발생 순서

### 1단계: 외부 클럭(HSE) 설정 실패

**증상:**
```c
if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
{
    Error_Handler();  // ← 여기서 에러 발생
}
```

**원인:**
- 보드에 25MHz 외부 오실레이터가 없음
- `RCC_HSE_BYPASS` 모드는 외부 클럭 소스 필수
- HSE 초기화 타임아웃 발생

**실패한 설정:**
```c
RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;  // 외부 25MHz 필요
RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_HSE;
RCC_OscInitStruct.PLL.PLLM = 2;
RCC_OscInitStruct.PLL.PLLN = 40;
RCC_OscInitStruct.PLL.PLLP = 2;
```

**클럭 계산 (실패):**
- HSE: 25MHz (없음!)
- VCI: 25MHz / 2 = 12.5MHz
- VCO: 12.5MHz × 40 = 500MHz
- SYSCLK: 500MHz / 2 = 250MHz

---

### 2단계: CSI 내부 클럭으로 변경 실패

**시도한 설정:**
```c
RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_CSI;
RCC_OscInitStruct.CSIState = RCC_CSI_ON;
RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_CSI;
RCC_OscInitStruct.PLL.PLLM = 2;
RCC_OscInitStruct.PLL.PLLN = 125;
RCC_OscInitStruct.PLL.PLLP = 1;  // ← 문제!
RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_1;
```

**원인:**
- `PLLP = 1` 설정이 일부 조건에서 거부됨
- CSI VCI 범위 설정 문제 가능성
- HAL 라이브러리에서 유효성 검증 실패

**클럭 계산 (실패):**
- CSI: 4MHz
- VCI: 4MHz / 2 = 2MHz (VCIRANGE_1)
- VCO: 2MHz × 125 = 250MHz
- SYSCLK: 250MHz / 1 = 250MHz (PLLP=1 거부됨)

---

### 3단계: HSI 내부 클럭으로 성공 (시스템 클럭)

**성공한 설정:**
```c
RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSI;
RCC_OscInitStruct.HSIState = RCC_HSI_ON;
RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
RCC_OscInitStruct.LSIState = RCC_LSI_ON;
RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_HSI;
RCC_OscInitStruct.PLL.PLLM = 4;
RCC_OscInitStruct.PLL.PLLN = 31;
RCC_OscInitStruct.PLL.PLLP = 2;  // ← PLLP=2 사용
RCC_OscInitStruct.PLL.PLLQ = 2;
RCC_OscInitStruct.PLL.PLLR = 2;
RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_3;
RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
```

**클럭 계산 (성공):**
- HSI: 64MHz (내장 RC 오실레이터)
- VCI: 64MHz / 4 = 16MHz (VCIRANGE_3: 8-16MHz) ✓
- VCO: 16MHz × 31 = 496MHz (VCORANGE_WIDE) ✓
- SYSCLK: 496MHz / 2 = **248MHz** ✓

**Flash 설정:**
```c
if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
```

---

### 4단계: SPI 초기화 실패

**증상:**
```c
if (HAL_SPI_Init(&hspi1) != HAL_OK)
{
    Error_Handler();  // ← 새로운 에러 발생
}
```

**원인:**
- SPI1 클럭 소스가 PLL3로 설정되어 있음
- PLL3는 HSE(외부 클럭)를 소스로 사용
- HSE가 비활성화되어 PLL3 초기화 실패

**실패한 SPI 클럭 설정:**
```c
// stm32h5xx_hal_msp.c - HAL_SPI_MspInit()
PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI1;
PeriphClkInitStruct.PLL3.PLL3Source = RCC_PLL3_SOURCE_HSE;  // ← 문제!
PeriphClkInitStruct.PLL3.PLL3M = 2;
PeriphClkInitStruct.PLL3.PLL3N = 40;
PeriphClkInitStruct.PLL3.PLL3P = 4;
PeriphClkInitStruct.Spi1ClockSelection = RCC_SPI1CLKSOURCE_PLL3P;
```

**PLL3 클럭 계산 (실패):**
- PLL3 소스: HSE 25MHz (비활성화됨!)
- PLL3 VCI: 25MHz / 2 = 12.5MHz
- PLL3 VCO: 12.5MHz × 40 = 500MHz
- PLL3P: 500MHz / 4 = 125MHz (SPI1 클럭)
- **결과**: PLL3 초기화 실패 → SPI 초기화 실패

---

### 5단계: SPI 클럭 소스 변경으로 최종 해결

**해결 방법:**
SPI1 클럭 소스를 PLL3P → PLL1Q로 변경

**성공한 SPI 클럭 설정:**
```c
// stm32h5xx_hal_msp.c - HAL_SPI_MspInit()
PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI1;
PeriphClkInitStruct.Spi1ClockSelection = RCC_SPI1CLKSOURCE_PLL1Q;  // ← 해결!
if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
{
    Error_Handler();
}
```

**PLL1Q 클럭 계산 (성공):**
- PLL1 소스: HSI 64MHz ✓
- PLL1 VCI: 64MHz / 4 = 16MHz
- PLL1 VCO: 16MHz × 31 = 496MHz
- PLL1Q: 496MHz / 2 = **248MHz** (SPI1 클럭) ✓

---

## 최종 클럭 트리 요약

```
HSI (64MHz, 내장 RC 오실레이터)
  │
  ├─> PLL1
  │    ├─ VCI: 16MHz (PLLM=4)
  │    ├─ VCO: 496MHz (PLLN=31)
  │    ├─ PLLP: 248MHz (÷2) → SYSCLK (시스템 클럭)
  │    ├─ PLLQ: 248MHz (÷2) → SPI1_CLK (SPI 클럭)
  │    └─ PLLR: 248MHz (÷2)
  │
  └─> LSI (32kHz, 저전력 클럭)
       └─> DAC 클럭 소스
```

**주요 클럭:**
- 시스템 클럭 (SYSCLK): 248MHz
- AHB 클럭: 248MHz (÷1)
- APB1/2/3 클럭: 248MHz (÷1)
- SPI1 클럭: 248MHz (PLL1Q)
- DAC 클럭: 32kHz (LSI)
- Flash Latency: 5 wait states

**메모리 사용량:**
```
   text	   data	    bss	    dec	    hex	filename
  79740	    112	  40472	 120324	  1d604	audio_dac_v100.elf
```
- Flash: 79.8KB / 256KB (31%)
- RAM: 40.5KB / 272KB (15%)

---

## 디버깅 체크리스트

외부 클럭에서 내부 클럭으로 변경 시 확인 사항:

### 1. 시스템 클럭 설정 (main.c - SystemClock_Config)

- [ ] `OscillatorType`에서 HSE 제거
- [ ] HSI/CSI 활성화 확인
- [ ] PLL 소스를 HSI/CSI로 변경
- [ ] PLLM, PLLN, PLLP 값이 유효 범위 내인지 확인
- [ ] VCI 범위가 PLLRGE와 일치하는지 확인
- [ ] VCO 범위가 PLLVCOSEL과 일치하는지 확인
- [ ] Flash Latency가 SYSCLK에 적합한지 확인

### 2. 주변장치 클럭 설정 (stm32h5xx_hal_msp.c)

- [ ] **SPI 클럭**: PLL3 (HSE) → PLL1Q (HSI) 변경
- [ ] **UART 클럭**: PLL2/PLL3 사용 시 소스 확인
- [ ] **ADC/DAC 클럭**: LSI/HSI 사용 확인
- [ ] **타이머 클럭**: APB 클럭 확인

### 3. 클럭 계산 공식

```
VCI = Input Clock / PLLM
VCO = VCI × PLLN
Output = VCO / PLLP (or PLLQ, PLLR)
```

**유효 범위 (STM32H523):**
- HSI: 64MHz (고정)
- CSI: 4MHz (고정)
- VCI: 1-16MHz (PLLRGE로 지정)
  - VCIRANGE_0: 1-2MHz
  - VCIRANGE_1: 2-4MHz
  - VCIRANGE_2: 4-8MHz
  - VCIRANGE_3: 8-16MHz
- VCO: 128-560MHz (WIDE) 또는 128-560MHz (MEDIUM)
- PLLP/Q/R: 1, 2-128 (일부 제약 있음)

### 4. Flash Latency (SYSCLK 기준, VOS0)

| SYSCLK 범위 | Flash Latency |
|-------------|---------------|
| 0-42 MHz    | 0 WS          |
| 42-84 MHz   | 1 WS          |
| 84-126 MHz  | 2 WS          |
| 126-168 MHz | 3 WS          |
| 168-210 MHz | 4 WS          |
| 210-250 MHz | 5 WS          |

---

## 일반적인 에러와 해결 방법

### 에러 1: HAL_RCC_OscConfig() 실패

**원인:**
- 외부 클럭(HSE/LSE)이 없는데 사용 시도
- PLL 파라미터 범위 오류
- VCI/VCO 범위와 설정 불일치

**해결:**
1. 외부 클럭 사용 시 하드웨어 확인
2. 내부 클럭(HSI/CSI) 사용으로 변경
3. PLL 계산기로 유효성 확인

### 에러 2: HAL_xxx_Init() 실패 (주변장치)

**원인:**
- 주변장치 클럭 소스가 비활성화된 PLL 사용
- 클럭 속도가 주변장치 최대 속도 초과

**해결:**
1. MSP 파일에서 클럭 소스 확인
2. PLL1/HSI 같은 활성화된 소스로 변경
3. 클럭 속도를 주변장치 사양 내로 조정

### 에러 3: 부팅 후 바로 멈춤

**원인:**
- Flash Latency 부족
- 전압 스케일링 설정 오류
- 클럭 속도가 MCU 최대 속도 초과

**해결:**
1. Flash Latency 증가
2. 전압 스케일링을 SCALE0으로 설정
3. 시스템 클럭을 250MHz 이하로 제한

---

## STM32CubeMX 재생성 시 주의사항

STM32CubeMX로 코드를 재생성하면 클럭 설정이 초기화될 수 있습니다.

**보호 방법:**

1. **main.c**: `USER CODE BEGIN/END` 섹션 사용
   ```c
   /* USER CODE BEGIN SysInit */
   // 여기에 클럭 설정 수동 수정 코드 추가
   /* USER CODE END SysInit */
   ```

2. **stm32h5xx_hal_msp.c**: SPI/UART MSP Init 함수의 USER CODE 섹션 활용
   ```c
   /* USER CODE BEGIN SPI1_MspInit 0 */
   // SPI 클럭 소스 강제 설정
   /* USER CODE END SPI1_MspInit 0 */
   ```

3. **.ioc 파일 백업**: 클럭 설정 변경 전 백업

---

## 권장 클럭 설정 (외부 오실레이터 없을 때)

### Option 1: 최대 성능 (248MHz)

```c
// HSI 64MHz 기반
RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_HSI;
RCC_OscInitStruct.PLL.PLLM = 4;   // VCI = 16MHz
RCC_OscInitStruct.PLL.PLLN = 31;  // VCO = 496MHz
RCC_OscInitStruct.PLL.PLLP = 2;   // SYSCLK = 248MHz
RCC_OscInitStruct.PLL.PLLQ = 2;   // Peripheral = 248MHz
```

### Option 2: 저전력 (64MHz)

```c
// HSI 직접 사용 (PLL 없음)
RCC_OscInitStruct.PLL.PLLState = RCC_PLL_OFF;
RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
// Flash Latency = 1
```

### Option 3: 안정성 중시 (200MHz)

```c
// HSI 64MHz 기반
RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_HSI;
RCC_OscInitStruct.PLL.PLLM = 4;   // VCI = 16MHz
RCC_OscInitStruct.PLL.PLLN = 25;  // VCO = 400MHz
RCC_OscInitStruct.PLL.PLLP = 2;   // SYSCLK = 200MHz
RCC_OscInitStruct.PLL.PLLQ = 2;   // Peripheral = 200MHz
// Flash Latency = 4
```

---

## 참고 자료

- **STM32H523 Reference Manual**: RM0481
- **STM32H523 Datasheet**: DS13851
- **AN4995**: STM32H5 시리즈 시스템 아키텍처
- **STM32CubeMX**: 클럭 설정 도구
- **STM32CubeH5 HAL**: HAL 라이브러리 문서

---

## 문의

클럭 설정 관련 문제 발생 시:
1. 시리얼 터미널에서 부팅 메시지 확인
2. 디버거로 Error_Handler() 호출 스택 확인
3. 이 문서의 체크리스트 점검
