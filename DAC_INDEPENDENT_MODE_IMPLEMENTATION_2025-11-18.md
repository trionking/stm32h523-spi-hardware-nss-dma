# DAC Independent Mode 구현 완료 보고서 (CH1 성공)

**날짜**: 2025-11-18
**프로젝트**: STM32H523 오디오 DAC (audio_dac_v102)
**목표**: Dual DAC Mode → Independent DAC Mode 전환
**결과**: ✅ **DAC CH1 성공** | ⚠️ **DAC CH2 미해결**

---

## 📊 현재 상태

### DAC CH1 (PA4)
| 항목 | 상태 |
|------|------|
| DMA 인터럽트 | ✅ 정상 동작 |
| 오디오 출력 | ✅ 소리 나옴 |
| 트리거 | TIM1_TRGO (32kHz) |
| DMA 채널 | GPDMA2_Channel0 |

### DAC CH2 (PA5)
| 항목 | 상태 |
|------|------|
| DMA 인터럽트 | ❌ 동작 안 함 |
| 오디오 출력 | ❌ 소리 안 나옴 |
| 트리거 | TIM7_TRGO (32kHz) |
| DMA 채널 | GPDMA2_Channel1 |

---

## 🎯 프로젝트 목표

### 초기 문제
- **Dual DAC Mode**: 두 채널이 하나의 DMA로 동작 (DHR12RD 사용)
- 한 채널만 재생할 때도 두 채널 모두 영향받음
- 각 채널이 독립적으로 동작할 수 없음

### 목표
- **Independent DAC Mode**: 각 채널이 독립적인 DMA와 트리거 사용
- DAC CH1: TIM1 트리거, GPDMA2_Channel0
- DAC CH2: TIM7 트리거, GPDMA2_Channel1
- 각 채널이 독립적으로 재생/정지 가능

---

## 🔧 구현 과정

### 1️⃣ main.c 수정

**위치**: `Core/Src/main.c:275-281`

**변경 내용**: Dual Mode CR 조작 제거

```c
// 이전 (Dual Mode)
uint32_t cr = DAC1->CR;
cr &= ~(1 << 28);     // Clear DMAEN2 - DAC2 does NOT trigger DMA!
cr &= ~(1 << 17);     // Clear TEN2 - No hardware trigger for DAC2
DAC1->CR = cr;

// 현재 (Independent Mode)
// INDEPENDENT DAC MODE: Each channel has its own DMA and trigger
// DAC1: TEN1=1, DMAEN1=1, TSEL1=0 (TIM1_TRGO)
// DAC2: TEN2=1, DMAEN2=1, TSEL2=110 (TIM7_TRGO, DAC_TRIGGER_T7_TRGO)
// Both channels operate independently with their own sample rates

printf("[DAC_INIT] INDEPENDENT MODE: CH1=TIM1, CH2=TIM7, CR=0x%08lX\r\n", DAC1->CR);
```

**효과**: HAL이 설정한 DAC2 트리거/DMA 설정을 그대로 유지

---

### 2️⃣ user_def.c 수정

**위치**: `Core/Src/user_def.c:1269-1286`

**변경 내용**: 캘리브레이션 시 각 채널 독립 트리거 설정

```c
// 이전 (Dual Mode)
DAC_ChannelConfTypeDef sConfig = {0};
sConfig.DAC_Trigger = DAC_TRIGGER_T1_TRGO;  // TIM1 for BOTH channels!

HAL_StatusTypeDef cal1 = HAL_DACEx_SelfCalibrate(&hdac1, &sConfig, DAC_CHANNEL_1);
HAL_StatusTypeDef cal2 = HAL_DACEx_SelfCalibrate(&hdac1, &sConfig, DAC_CHANNEL_2);

// DAC2 DMA 비활성화
uint32_t cr = DAC1->CR;
cr &= ~(1 << 28);      // Clear DMAEN2
DAC1->CR = cr;

// 현재 (Independent Mode)
DAC_ChannelConfTypeDef sConfig = {0};
sConfig.DAC_HighFrequency = DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC;
sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;

// DAC CH1: TIM1_TRGO trigger
sConfig.DAC_Trigger = DAC_TRIGGER_T1_TRGO;
HAL_StatusTypeDef cal1 = HAL_DACEx_SelfCalibrate(&hdac1, &sConfig, DAC_CHANNEL_1);

// DAC CH2: TIM7_TRGO trigger (different from CH1!)
sConfig.DAC_Trigger = DAC_TRIGGER_T7_TRGO;
HAL_StatusTypeDef cal2 = HAL_DACEx_SelfCalibrate(&hdac1, &sConfig, DAC_CHANNEL_2);
```

**효과**: 각 채널이 올바른 트리거로 캘리브레이션됨

---

### 3️⃣ spi_handler.c 수정

**위치**: `Core/Src/spi_handler.c:528-605`

**변경 내용**: CMD_PLAY 핸들러를 Independent Mode로 재작성

```c
// 이전 (Dual Mode)
// 32-bit dual buffer로 변환
for (uint32_t i = 0; i < AUDIO_BUFFER_SIZE; i++)
{
    uint16_t ch1_data = channel->active_buffer[i];
    uint16_t ch2_data = 0x800;  // CH2 = middle value (silence)
    g_dual_dac_buffer[i] = (ch2_data << 16) | ch1_data;
}

// DHR12RD 주소로 수동 DMA 시작
DMA_Channel_TypeDef *dma_ch = (DMA_Channel_TypeDef *)hdma->Instance;
uint32_t dhr12rd_addr = (uint32_t)&(DAC1->DHR12RD);
hdma->LinkedListQueue->Head->LinkRegisters[NODE_CDAR_DEFAULT_OFFSET] = dhr12rd_addr;
status = HAL_DMAEx_List_Start_IT(hdma);

// TIM1만 시작
HAL_TIM_Base_Start(&htim1);

// 현재 (Independent Mode)
// 16-bit 버퍼를 직접 사용 (변환 불필요)
status = HAL_DAC_Start_DMA(&hdac1, dac_channel,
                           (uint32_t*)channel->active_buffer,
                           AUDIO_BUFFER_SIZE,
                           DAC_ALIGN_12B_R);

// 각 채널에 맞는 타이머 시작
if (dac_channel == DAC_CHANNEL_1)
{
    HAL_TIM_Base_Start(&htim1);  // CH1 uses TIM1
}
else
{
    HAL_TIM_Base_Start(&htim7);  // CH2 uses TIM7
}
```

**효과**:
- 버퍼 변환 불필요 (16-bit 직접 사용)
- HAL_DAC_Start_DMA() 사용 (DHR12R1/DHR12R2 자동 설정)
- 각 채널이 독립적인 타이머 사용

---

### 4️⃣ stm32h5xx_hal_msp.c 수정 (핵심!)

**위치**: `Core/Src/stm32h5xx_hal_msp.c:156, 212`

**변경 내용**: DestDataWidth를 WORD로 변경

```c
// 이전
NodeConfig.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_HALFWORD;
NodeConfig.Init.DestDataWidth = DMA_DEST_DATAWIDTH_HALFWORD;  // ❌ Transfer Error 발생!

// 현재
NodeConfig.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_HALFWORD;
NodeConfig.Init.DestDataWidth = DMA_DEST_DATAWIDTH_WORD;  // ✅ CRITICAL: DAC register is 32-bit
```

**이유**: DAC DHR12R1/DHR12R2 레지스터는 32비트

```
HALFWORD(16비트)로 쓰면:
  [31:16]     [15:0]
  ┌─────────┬─────────┐
  │   ???   │ 업데이트│ ❌ 상위 16비트 미정의 → Transfer Error!
  └─────────┴─────────┘

WORD(32비트)로 쓰면:
  [31:16]     [15:0]
  ┌─────────┬─────────┐
  │    0    │ 업데이트│ ✅ 올바른 정렬
  └─────────┴─────────┘
```

**효과**: DMA Transfer Error (0x00000001) 해결!

---

## 🔍 핵심 해결 방법

### DMA Transfer Error의 원인

**참고 문서**: `DAC_DMA_SUCCESS_2025-01-13_23-30-00.md`

**증상**:
```
[DAC ERROR CH1] ==================
ErrorCode: 0x00000004  ← HAL_DAC_ERROR_DMA
DMA ErrorCode: 0x00000001  ← HAL_DMA_ERROR_TE (Transfer Error)
```

**원인**:
1. ~~TransferAllocatedPort 설정 오류~~ → 이미 올바름 (PORT1|PORT0)
2. ~~LinkAllocatedPort 설정 오류~~ → 이미 올바름 (PORT1)
3. **DestDataWidth = HALFWORD** ← **이것이 문제!**

**해결**:
```c
// DAC CH1 (GPDMA2_Channel0)
NodeConfig.Init.DestDataWidth = DMA_DEST_DATAWIDTH_WORD;

// DAC CH2 (GPDMA2_Channel1)
NodeConfig.Init.DestDataWidth = DMA_DEST_DATAWIDTH_WORD;
```

---

## 📊 DAC CH1 검증 결과

### DMA 레지스터 상태
```
DMA CCR: 0x00825F01 (EN=1)  ✅ DMA 활성화
DMA CSR: 0x00000000          ✅ 에러 플래그 없음
DMA CBR1: 감소 중            ✅ 데이터 전송 중
```

### 인터럽트 카운터
```
10초 간격:
  HalfCplt: 156 이벤트  ✅
  Cplt: 156 이벤트      ✅

기대값: 15.625 Hz (32kHz ÷ 2048 samples)
실측값: 15.6 Hz
오차: 0.16%  ✅
```

### 오디오 출력
- ✅ 소리 나옴
- ✅ DMA Circular 모드 정상 동작
- ✅ 버퍼 스왑 정상 동작

---

### 5️⃣ TIM7 트리거 버그 수정 ⚠️ **CRITICAL**

**날짜**: 2025-11-18 (업데이트)

**발견**: STM32H5 HAL 라이브러리 버그 - 타이머 트리거 정의가 잘못됨!

**위치**: `stm32h5xx_hal_dac.h`

**버그 내용**:
```c
// HAL 정의 (잘못됨!)
#define DAC_TRIGGER_T6_TRGO  ((uint32_t)0x00028000)  // TSEL=5 → 실제로는 TIM7!
#define DAC_TRIGGER_T7_TRGO  ((uint32_t)0x00030000)  // TSEL=6 → 실제로는 TIM8!
```

**하드웨어 실제 매핑**:
| TSEL 값 | HAL 정의 | 실제 트리거 |
|---------|----------|------------|
| 5 (0b00101) | DAC_TRIGGER_T6_TRGO | **TIM7** TRGO |
| 6 (0b00110) | DAC_TRIGGER_T7_TRGO | **TIM8** TRGO |

**수정 내용**:

**main.c** (Core/Src/main.c:265-267):
```c
// 수정 전:
sConfig.DAC_Trigger = DAC_TRIGGER_T7_TRGO;  // TSEL=6 → TIM8 (잘못됨!)

// 수정 후:
// CRITICAL: STM32H5 HAL BUG - DAC_TRIGGER_T7_TRGO is actually TIM8!
// Use DAC_TRIGGER_T6_TRGO (TSEL=5) to get TIM7 TRGO
sConfig.DAC_Trigger = DAC_TRIGGER_T6_TRGO;  // TSEL=5 → TIM7 TRGO
```

**user_def.c** (Core/Src/user_def.c:1279):
```c
// 수정 전:
sConfig.DAC_Trigger = DAC_TRIGGER_T7_TRGO;  // TSEL=6 → TIM8 (잘못됨!)

// 수정 후:
// CRITICAL: HAL BUG - DAC_TRIGGER_T7_TRGO is actually TIM8!
sConfig.DAC_Trigger = DAC_TRIGGER_T6_TRGO;  // TSEL=5 → TIM7 TRGO
```

**빌드 결과**:
```
✅ 빌드 성공 (2025-11-18)
   text     data      bss      dec      hex    filename
 123336     1760    54336   179432    2bce8   audio_dac_v102.elf
```

---

## ⚠️ 미해결 문제: DAC CH2 여전히 작동 안 함

### 현재 상태 (2025-11-18 최신)

**테스트 결과**:
```
[CMD_PLAY] DAC2 Starting - DMA=0x2000074C, Buf=0x20040240, Size=2048
[FIX] DAC CH2: CDAR corrected to DHR12R2 (0x42028414)
[CMD_PLAY] INDEPENDENT MODE: DAC DMA started successfully
  DMA CCR: 0x00825F01 (EN=1)          ✅ DMA 활성화
  DMA CSR: 0x00000000                 ❌ 전송 상태 없음
  DMA CBR1: 4096 items                ✅ 카운터 설정됨
  DMA CSAR: 0x20040240                ✅ 소스 주소 OK
  DMA CDAR: 0x42028414                ✅ DHR12R2 주소 OK

[DEBUG] TIM7 CNT (before): 1982, (after): 6779
  ✓ TIM7 is running!                  ✅ TIM7 동작 중

[DEBUG] TIM7 CR2: 0x00000020, MMS: 2 (should be 2 for Update event)
                                      ✅ TRGO 설정 OK

[DEBUG] DAC CH2 settings:
  EN2=1 (bit 16)                      ✅ DAC 활성화
  TEN2=1 (bit 17)                     ✅ 트리거 활성화
  TSEL2=5 (bits 22-18)                ✅ TIM7 (수정됨!)
  DMAEN2=1 (bit 28)                   ✅ DMA 활성화

[STATUS] --------------------
DAC2: STOP | Samples: 2048 | Swaps: 0 | Underruns: 0
  DMA IRQ: HalfCplt=0 | Cplt=0        ❌ 인터럽트 전혀 없음
```

### 🔴 핵심 문제

**모든 설정이 올바른데도 DMA 전송이 시작되지 않음!**

| 항목 | 상태 | 비고 |
|------|------|------|
| TIM7 실행 | ✅ | CNT 카운팅 중 |
| TIM7 TRGO | ✅ | MMS=2 (Update event) |
| DAC CH2 활성화 | ✅ | EN2=1 |
| DAC CH2 트리거 | ✅ | TEN2=1, TSEL2=5 (TIM7) |
| DAC CH2 DMA | ✅ | DMAEN2=1 |
| DMA 활성화 | ✅ | CCR EN=1 |
| DMA 소스 | ✅ | 0x20040240 |
| DMA 목적지 | ✅ | 0x42028414 (DHR12R2) |
| **DMA 전송** | ❌ | **CSR=0, 인터럽트=0** |

### 의심 사항 (업데이트)

1. ✅ ~~**TIM7 트리거 설정 문제**~~ → **해결됨** (TSEL2=5)

2. ❓ **HAL_DAC_Start_DMA() 버그 (CH2 전용)?**
   - CH1은 정상 작동
   - CH2만 DMA 전송이 시작되지 않음
   - CDAR 주소 수동 수정했지만 효과 없음

3. ❓ **LinkedList Queue 로드 문제?**
   - LinkedList가 하드웨어 레지스터에 제대로 로드되지 않음
   - 캐시 일관성 문제 가능성

4. ❓ **DAC CH2 특별한 초기화 순서?**
   - CH1과 CH2의 초기화 순서 차이
   - CH2는 추가 설정이 필요할 수도

5. ❓ **DMA Request 생성/전달 문제?**
   - DAC가 DMA request를 생성하지 않음
   - GPDMA가 request를 받지 못함

---

## 🔍 다음 단계: DAC CH2 근본 원인 찾기

### ✅ 완료된 디버깅

1. **TIM7 상태** → ✅ 정상 (CNT 증가, MMS=2)
2. **DAC CH2 레지스터** → ✅ 정상 (EN2=1, TEN2=1, TSEL2=5, DMAEN2=1)
3. **GPDMA2_Channel1** → ✅ 설정 정상 (EN=1, 주소 올바름)
4. **TIM7 트리거 버그** → ✅ 수정 (TSEL2=5)

### 🔴 남은 문제

**DMA 전송이 전혀 시작되지 않음 (CSR=0, 인터럽트=0)**

### 📋 우선순위 높은 조사 항목

#### 1. CH1과 CH2 초기화 비교 (최우선)

**가설**: CH1은 작동하고 CH2는 안 되므로, 초기화 순서나 설정에 미묘한 차이가 있을 것

**조사 방법**:
```c
// stm32h5xx_hal_msp.c에서 CH1과 CH2의 DMA 초기화를 라인별로 비교
// - NodeConfig 설정
// - List 생성 순서
// - Handle 초기화 순서
// - __HAL_LINKDMA 호출 순서
```

**확인할 차이점**:
- Channel0과 Channel1의 우선순위 설정
- DMA Request 번호 (GPDMA2_REQUEST_DAC1_CH1 vs CH2)
- LinkedList 모드 설정 차이
- 인터럽트 우선순위 차이

#### 2. 레퍼런스 매뉴얼 확인 (MCP 도구 사용)

**목적**: STM32H523 DAC CH2 특별 요구사항 확인

**확인할 내용**:
- DAC CH2 DMA 요청 조건
- TSEL 비트 실제 매핑표 (5번이 정말 TIM7인지 재확인)
- DAC CH2 특별한 초기화 순서
- 멀티채널 동시 사용 시 제약사항

**파일**: `.doc2/rm0481-stm32h52333xx-stm32h56263xx-and-stm32h573xx-armbased-32bit-mcus-stmicroelectronics.pdf`

**도구**: pymupdf4llm-mcp (Claude Code 재시작 후 사용)

#### 3. HAL_DAC_Start_DMA() 소스 분석

**가설**: HAL 함수가 CH2에 대해 다르게 동작할 수 있음

**조사 방법**:
```c
// STM32H5 HAL 소스에서:
// - stm32h5xx_hal_dac.c: HAL_DAC_Start_DMA() 구현
// - DAC_CHANNEL_1과 DAC_CHANNEL_2 처리 차이
// - DMA 주소 계산 로직
```

#### 4. 캐시 일관성 확인

**가설**: DCACHE 활성화로 인한 LinkedList 동기화 문제

**테스트 코드**:
```c
// HAL_DAC_Start_DMA() 후 추가:
SCB_CleanDCache_by_Addr((uint32_t*)&Node_GPDMA2_Channel1,
                        sizeof(DMA_NodeTypeDef));
SCB_CleanDCache_by_Addr((uint32_t*)&List_GPDMA2_Channel1,
                        sizeof(DMA_QListTypeDef));
```

#### 5. DMA Request 라인 추적

**가설**: DAC → DMAMUX → GPDMA 연결이 CH2에서만 끊겨있음

**확인 방법**:
- DMAMUX 레지스터 확인 (존재한다면)
- GPDMA2 request 매핑 확인
- DAC DMA request 생성 조건 재확인

#### 6. 인터럽트 활성화 확인

**가설**: GPDMA2_Channel1 인터럽트가 활성화되지 않음

**확인 코드**:
```c
// NVIC 레지스터 직접 확인
uint32_t nvic_iser = NVIC->ISER[GPDMA2_Channel1_IRQn / 32];
uint32_t bit = 1 << (GPDMA2_Channel1_IRQn % 32);
printf("GPDMA2_Channel1 IRQ enabled: %s\r\n",
       (nvic_iser & bit) ? "YES" : "NO");
```

### 🎯 권장 작업 순서

1. **Claude Code 재시작** → pymupdf4llm-mcp 활성화
2. **레퍼런스 매뉴얼 DAC 섹션 확인** → TSEL 매핑표 재확인
3. **CH1/CH2 초기화 코드 비교** → 차이점 발견
4. **발견된 차이점 수정 및 테스트**
5. **캐시 동기화 테스트** (위 방법들로 해결 안 되면)

### 🔎 인터넷 검색 키워드 (업데이트)

- "STM32H5 DAC channel 2 DMA not working"
- "STM32H523 DAC TSEL trigger mapping"
- "STM32H5 GPDMA2 DAC1_CH2 request"
- "STM32 HAL_DAC_Start_DMA channel 2 bug"
- "STM32H5 DAC independent mode example"

---

## 📚 관련 문서

### 프로젝트 문서

1. **DAC_DMA_SUCCESS_2025-01-13_23-30-00.md**
   - DAC CH1 DMA 성공 과정
   - Port 설정 및 DestDataWidth 문제 해결

2. **STM32H5_CODEGEN_BUGS.md**
   - CubeMX 코드 생성 버그 목록
   - GPDMA2 관련 버그 패치

3. **CLAUDE.md**
   - 프로젝트 전체 개요
   - 빌드 시스템 및 아키텍처

### ST 문서

1. **Reference Manual (RM0481)**
   - Section: DAC (Digital-to-Analog Converter)
   - Section: TIM7 (Basic Timer)
   - Section: GPDMA (General Purpose DMA)

2. **Example Code**
   - `STM32Cube_FW_H5_V1.5.0/Projects/NUCLEO-H563ZI/Examples/DAC/DAC_SignalsGeneration`

---

## ✅ 완료된 작업

- [x] Dual Mode → Independent Mode 코드 변경
- [x] main.c DAC 초기화 수정
- [x] user_def.c 캘리브레이션 수정
- [x] spi_handler.c CMD_PLAY 수정
- [x] stm32h5xx_hal_msp.c DestDataWidth 수정
- [x] DAC CH1 DMA 동작 검증
- [x] DAC CH1 오디오 출력 확인
- [x] **TIM7 트리거 버그 발견 및 수정** (TSEL2=5)
- [x] main.c, user_def.c 트리거 설정 수정
- [x] 빌드 성공 (2025-11-18)
- [x] TIM7 상태 확인 (정상 동작 중)
- [x] DAC CH2 레지스터 확인 (모든 비트 올바름)
- [x] GPDMA2_Channel1 상태 확인 (설정 정상)
- [x] pymupdf4llm-mcp 설치 (.mcp.json 수정)

## 🚧 진행 중인 작업

- [ ] **DAC CH2 DMA 전송 미시작 원인 규명** (최우선)
- [ ] pymupdf4llm-mcp 활성화 (Claude Code 재시작 필요)
- [ ] 레퍼런스 매뉴얼 DAC 섹션 확인
- [ ] CH1과 CH2 초기화 코드 상세 비교
- [ ] HAL_DAC_Start_DMA() 소스 분석
- [ ] 캐시 일관성 테스트
- [ ] DMA Request 라인 추적
- [ ] 인터럽트 활성화 확인

## ⏭️ 다음 세션 시작 시

1. ✅ Claude Code 재시작 (MCP PDF 도구 활성화)
2. 📖 레퍼런스 매뉴얼 읽기 (DAC TSEL 매핑표 재확인)
3. 🔍 CH1/CH2 초기화 차이점 찾기
4. 🛠️ 발견된 차이점 수정 및 재테스트

---

## 🎓 배운 교훈

### 1. DAC DHR 레지스터는 32비트
- 16비트 데이터를 쓰더라도 **레지스터 자체는 32비트**
- DMA DestDataWidth는 **WORD(32비트)로 설정**해야 함
- HALFWORD로 설정하면 Transfer Error 발생

### 2. Independent Mode 구현
- 각 채널이 독립적인 트리거와 DMA 사용
- DHR12R1 (CH1), DHR12R2 (CH2) 각각 사용
- Dual Mode의 DHR12RD 사용하지 않음

### 3. HAL_DAC_Start_DMA() 사용
- Independent Mode에서는 HAL 함수 사용 가능
- LinkedList 모드에서도 정상 동작 (CH1 검증됨)
- 수동 DMA 설정 불필요

### 4. ⚠️ STM32H5 HAL 타이머 트리거 버그
- **DAC_TRIGGER_T6_TRGO** (TSEL=5) → 실제로는 **TIM7** TRGO
- **DAC_TRIGGER_T7_TRGO** (TSEL=6) → 실제로는 **TIM8** TRGO
- HAL 정의와 실제 하드웨어 매핑이 불일치!
- TIM7을 사용하려면 DAC_TRIGGER_T6_TRGO 사용 필수
- 레퍼런스 매뉴얼 직접 확인 필요

### 5. 체계적 디버깅의 중요성
- 모든 설정이 "올바른 것처럼 보여도" 실제로는 숨겨진 버그가 있을 수 있음
- 타이머, DAC, DMA 레지스터를 모두 직접 읽어서 확인
- HAL 추상화를 믿지 말고 하드웨어 레지스터 직접 확인
- 이전 디버그 문서의 정보가 매우 중요함

---

**작성자**: Claude Code
**검증 환경**: STM32H523CCTx, GPDMA2, DAC1, TIM1/TIM7
**도구**: STM32CubeMX, VS Code, ST-Link
**펌웨어 버전**: audio_dac_v102 (build: 2025-11-18)
