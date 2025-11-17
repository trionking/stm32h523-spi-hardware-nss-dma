# DAC DMA 디버깅 진행 상황

**날짜:** 2025-01-11 23:45:00
**프로젝트:** STM32H523 오디오 DAC (audio_dac_v102)
**목표:** DAC DMA를 통한 32kHz 오디오 재생 구현

---

## 1. 현재 상태 (최신 테스트 결과)

### ✅ 해결된 문제들

1. **SPI 데이터 수신 완료**
   - 가변 길이 패킷 수신 정상 작동 (CS edge detection)
   - 50 샘플 DATA 패킷 수신 확인 (104 bytes)
   - 데이터 손상 문제 해결 (memset race condition, SPI reset)

2. **DAC 기본 동작 복구**
   - DAC Calibration 성공 (CH1: 0, CH2: 0)
   - 사이렌 테스트 정상 작동 (수동 DAC 출력 확인)
   - DAC trigger mode 문제 해결

3. **DMA Linked List Circular Mode 적용**
   - CubeMX에서 Circular Mode 설정 완료
   - NODE TYPE을 LINEAR로 수정
   - DMA DestDataWidth를 WORD(32-bit)로 수정

### ⚠️ 현재 이슈

**DMA 전송이 전혀 일어나지 않음**
- DMA가 활성화되었지만 CBR1 카운터가 감소하지 않음 (4096으로 고정)
- Software trigger 테스트로도 DMA가 반응하지 않음 확인 필요
- TIM7 TRGO 신호 또는 DMA request routing 문제 의심

### 최종 레지스터 상태

```
[PLAY 명령 실행 후]
DMA CCR: 0x00C05F01 (EN=1)        ✅ DMA 활성화
DMA CSR: 0x00000000               ✅ 에러 없음
DMA CBR1: 4096 items              ❌ 감소하지 않음!
DAC CR: 0x00163017                ✅ TSEL1=5 (TIM7), DMAEN1=1, EN1=1
DAC SR: 0x00000800                ✅ Reserved bit (문제 아님)

TIM7: CNT=15, ARR=7811            ✅ 32kHz @ 250MHz HCLK
      CR1=0x00000081 (CEN=1)      ✅ 타이머 실행 중
      CR2=0x00000020 (MMS=2)      ✅ Update TRGO
      DIER=0x00000000             ⚠️ 인터럽트 비활성화 (정상)
      SR=0x00000001 (UIF=1)       ✅ Update flag set

[Software Trigger 테스트]
Trigger 1~3: CBR1=4096            ❌ 변화 없음 (DMA 무반응)
```

---

## 2. 발견한 주요 문제들

### 🐛 문제 #1: DAC_TRIGGER_T6/T7_TRGO 정의 오류 (STM32 HAL 버그)

**위치:** `Drivers/STM32H5xx_HAL_Driver/Inc/stm32h5xx_hal_dac.h:214-215`

**문제:**
```c
#define DAC_TRIGGER_T6_TRGO  (DAC_CR_TSEL1_2 | DAC_CR_TSEL1_0 | DAC_CR_TEN1)  // TSEL=101 (5)
#define DAC_TRIGGER_T7_TRGO  (DAC_CR_TSEL1_2 | DAC_CR_TSEL1_1 | DAC_CR_TEN1)  // TSEL=110 (6)
```

**실제 하드웨어 연결:**
- TSEL=101 (5) → **TIM7** TRGO (T6_TRGO가 실제로는 TIM7)
- TSEL=110 (6) → **TIM8** TRGO (T7_TRGO가 실제로는 TIM8)

**해결:**
```c
// TIM7을 사용하려면 DAC_TRIGGER_T6_TRGO를 사용!
sConfig.DAC_Trigger = DAC_TRIGGER_T6_TRGO;  // ← TIM7 연결
```

**적용 위치:**
- `Core/Src/main.c:244` (DAC CH1 설정)
- `Core/Src/user_def.c:1142` (Calibration)

---

### 🐛 문제 #2: DMA NodeType이 2D_NODE로 생성됨

**문제:** CubeMX가 DAC DMA를 2D_NODE로 생성했으나, 단순 1D circular 전송에는 LINEAR_NODE가 필요

**해결:** 수동으로 변경
```c
// stm32h5xx_hal_msp.c:149, 204
NodeConfig.NodeType = DMA_GPDMA_LINEAR_NODE;  // 2D_NODE → LINEAR_NODE
// RepeatBlockConfig 설정도 제거
```

---

### 🐛 문제 #3: DMA DestDataWidth가 HALFWORD로 설정됨

**문제:** DAC 레지스터는 32비트이므로 WORD로 설정해야 함

**해결:**
```c
// stm32h5xx_hal_msp.c:156, 211
NodeConfig.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_HALFWORD;   // 16-bit (소스)
NodeConfig.Init.DestDataWidth = DMA_DEST_DATAWIDTH_WORD;     // 32-bit (DAC 레지스터)
NodeConfig.DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
```

---

### 🐛 문제 #4: DAC_HIGH_FREQUENCY_INTERFACE_MODE_DISABLE 사용 시 DAC 먹통

**문제:** 예제를 따라 DISABLE로 설정했더니 DAC가 완전히 작동하지 않음 (사이렌 테스트도 실패)

**원인:** 250MHz HCLK에서는 AUTOMATIC 모드를 사용해야 함

**해결:**
```c
// main.c:240, user_def.c:1141
sConfig.DAC_HighFrequency = DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC;
```

---

### 🐛 문제 #5: 사이렌 테스트 실패 (DAC trigger 모드 간섭)

**문제:** DAC가 초기화 시 TIM7 trigger 모드로 설정되어 있어서, 수동으로 `HAL_DAC_SetValue()`를 호출해도 출력되지 않음

**해결:** 사이렌 테스트 시작 시 DAC를 NO TRIGGER 모드로 재설정
```c
// user_def.c:324-340
DAC_ChannelConfTypeDef sConfig = {0};
sConfig.DAC_Trigger = DAC_TRIGGER_NONE;  // No trigger - manual control
sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
// ... 기타 설정
HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1);
HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_2);
```

---

### 🐛 문제 #6: HAL_Delay() 인터럽트 컨텍스트에서 블로킹

**문제:** EXTI 인터럽트 핸들러 내에서 `HAL_Delay()`를 호출하면 시스템이 멈춤

**해결:** 소프트웨어 루프로 대체
```c
// spi_handler.c:450
// HAL_Delay(1);  // ← 인터럽트 컨텍스트에서 사용 금지
for (volatile int i = 0; i < 1000; i++);  // ← 대신 이것 사용
```

---

### 🐛 문제 #7: CubeMX DAC OutputBuffer 누락

**문제:** STM32CubeMX가 DAC OutputBuffer 설정을 생성하지 않음

**해결:** 수동으로 추가
```c
// main.c:245, 255
sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
```

---

### 🐛 문제 #8: DCACHE 활성화 시 DMA 에러

**문제:** DCACHE와 non-cacheable RAM(0x2003C000) 충돌

**해결:** DCACHE 비활성화
```c
// main.c:131
// MX_DCACHE1_Init();  // DISABLED
```

---

## 3. 적용한 수정사항

### A. DMA 설정 (stm32h5xx_hal_msp.c)

#### DMA Linked List Circular Mode
```c
// Line 149, 204
NodeConfig.NodeType = DMA_GPDMA_LINEAR_NODE;  // FIX: 2D → LINEAR

// Line 156, 211
NodeConfig.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_HALFWORD;
NodeConfig.Init.DestDataWidth = DMA_DEST_DATAWIDTH_WORD;  // FIX: HALFWORD → WORD

// Line 162, 217
NodeConfig.TriggerConfig.TriggerPolarity = DMA_TRIG_POLARITY_MASKED;
// RepeatBlockConfig 제거됨

// Line 175, 230
HAL_DMAEx_List_SetCircularMode(&List_GPDMA1_Channel7);

// Line 185, 240
handle_GPDMA1_Channel7.InitLinkedList.LinkedListMode = DMA_LINKEDLIST_CIRCULAR;
```

#### DMA Priority
```c
// Line 186, 241
handle_GPDMA1_Channel7.InitLinkedList.Priority = DMA_HIGH_PRIORITY;
```

---

### B. DAC 설정 (main.c, user_def.c)

#### 1. TIM7 트리거 수정
```c
// main.c:244, user_def.c:1142
sConfig.DAC_Trigger = DAC_TRIGGER_T6_TRGO;  // HAL 버그 우회 (TIM7 연결)
```

#### 2. DAC OutputBuffer 추가
```c
// main.c:245, 255
sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
```

#### 3. DAC HighFrequency 모드
```c
// main.c:240, user_def.c:1141
sConfig.DAC_HighFrequency = DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC;
```

#### 4. 사이렌 테스트용 DAC 재설정
```c
// user_def.c:324-340
// 사이렌 테스트 시작 시 NO TRIGGER 모드로 재설정
sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1);
HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_2);
```

---

### C. 기타 수정사항

#### 1. DCACHE 비활성화
```c
// main.c:131
// MX_DCACHE1_Init();  // DISABLED
```

#### 2. SPI 데이터 손상 수정
```c
// spi_handler.c:646 - memset 제거 (race condition)
// spi_handler.c:693 - SPI DeInit/ReInit 추가 (상태 초기화)
```

#### 3. Software Trigger 테스트 추가
```c
// spi_handler.c:446-468
// DAC를 software trigger 모드로 전환하여 DMA 반응 테스트
uint32_t cr_backup = DAC1->CR;
DAC1->CR = (cr_backup & ~DAC_CR_TSEL1_Msk) | (0b111 << DAC_CR_TSEL1_Pos);
for (int i = 0; i < 3; i++) {
    DAC1->SWTRIGR = DAC_SWTRIGR_SWTRIG1;
    // CBR1 값 확인
}
DAC1->CR = cr_backup;  // 복원
```

---

## 4. 메모리 맵

### RAM 레이아웃
```
0x20000000 - 0x2003BFFF: 240KB Cacheable RAM (일반 변수/스택/힙)
0x2003C000 - 0x20043FFF: 32KB  Non-cacheable RAM (DMA 버퍼)
```

### MPU 설정
```c
Region 0: 0x2003C000 - 0x20043FFF
  - Non-cacheable (MPU_NOT_CACHEABLE)
  - Read/Write (MPU_REGION_ALL_RW)
  - Inner Shareable (MPU_ACCESS_INNER_SHAREABLE)
```

### DMA 버퍼 위치
```c
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static uint16_t g_audio_buffer1[AUDIO_BUFFER_SIZE];  // 0x2003E2C0
static uint16_t g_audio_buffer2[AUDIO_BUFFER_SIZE];
```

---

## 5. 타이밍 설정

### 클럭 구성
```
HSI: 64MHz (internal oscillator)
HCLK: 250MHz (PLL: HSI/4 * 250 / 2)
APB1/2/3: 250MHz (no prescaler)
```

### TIM7 설정 (32kHz DAC 샘플레이트)
```
Prescaler: 0
ARR: 7811
Frequency: 250MHz / (0+1) / (7811+1) = 32.005 kHz
TRGO: Update Event (MMS=2)
Master Mode: MASTERSLAVEMODE_DISABLE
```

### DMA 전송 타이밍
```
2048 샘플 @ 32kHz = 64ms (전체 버퍼)
1024 샘플 @ 32kHz = 32ms (Half Transfer)
```

### HAL Length 계산
```
HAL_DAC_Start_DMA(..., 2048, ...)
→ HAL이 내부적으로 바이트 변환
→ DMA CBR1 = 2048 * 2 = 4096 (SrcDataWidth=HALFWORD)
```

---

## 6. 테스트 프로토콜

### 현재 테스트 시퀀스

#### A. 사이렌 테스트 (DAC 기본 동작 확인)
```
1. 메뉴 2번 선택
2. 사이렌 소리 확인 (500Hz~2kHz sweep)
   → ✅ 정상 작동 확인됨
```

#### B. DMA 테스트 (TIM7 + DMA + DAC)
```
1. SPITEST PLAY 0 0       → DAC DMA 시작
2. STATUS                 → 레지스터 상태 확인
   → DMA CCR EN=1, CBR1=4096 (감소하지 않음)
3. Software Trigger 테스트 → DMA 반응 확인 필요
```

### 기대 결과
```
- DMA CBR1: 4096 → 감소 (전송 진행 중)
- Samples: 증가 (DMA Half Transfer마다 +1024)
- Swaps: 증가 (버퍼 스왑마다 +1)
- Underruns: 0 유지
```

---

## 7. 다음 단계

### ⚡ 긴급 테스트
1. **Software Trigger 테스트 실행**
   ```
   SPITEST PLAY 0 0
   ```
   - CBR1이 감소하는지 확인
   - 감소하면: TIM7 TRGO 문제
   - 감소하지 않으면: DMA 설정 근본 문제

2. **TIM7 TRGO 출력 확인** (CBR1 감소하는 경우)
   - TIM7 clock enable 확인
   - TRGO 신호 routing 확인
   - DAC trigger input 확인

3. **DMA Request Routing 확인** (CBR1 감소하지 않는 경우)
   - GPDMA1_REQUEST_DAC1_CH1 연결 확인
   - DMA Enable 순서 확인
   - HAL_DAC_Start_DMA() 내부 로직 분석

### 🔍 추가 디버깅

1. **DMA 레지스터 직접 모니터링**
   - CBR1 값 폴링 (1ms마다)
   - CSR 에러 플래그 확인
   - CCR EN bit 상태 확인

2. **DMA 인터럽트 핸들러에 디버그 추가**
   - Half Transfer Complete 콜백 호출 확인
   - Transfer Complete 콜백 호출 확인
   - Error 콜백 확인

3. **오실로스코프 확인** (가능한 경우)
   - PA4 (DAC CH1 출력) 파형 확인
   - 32kHz 샘플레이트 확인
   - TIM7 TRGO 신호 확인 (내부 신호)

---

## 8. 주요 파일 목록

### 수정된 파일
```
Core/Src/main.c                     ← DAC 트리거, OutputBuffer, HighFrequency, DCACHE
Core/Src/user_def.c                 ← DAC Calibration, 사이렌 테스트 수정
Core/Src/stm32h5xx_hal_msp.c        ← DMA LINEAR_NODE, DestDataWidth WORD, Circular Mode
Core/Src/spi_handler.c              ← SPI reset, Software Trigger 테스트, HAL_Delay 제거
STM32H523CCTX_FLASH.ld              ← MPU 설정 (이미 올바름)
```

### 설정 파일
```
audio_dac_v102.ioc                  ← CubeMX 설정 (Circular Mode)
.vscode/tasks.json                  ← 빌드/플래시 태스크
```

### 참고 문서
```
CLAUDE.md                           ← 프로젝트 개요
to-master.md                        ← Master MCU 통신 가이드
DAC_DMA_DEBUG_STATUS_2025-11-11_16-59-09.md  ← 이전 세션 상태
```

---

## 9. 알려진 제약사항

### CubeMX 재생성 시 주의사항
1. **DCACHE가 다시 활성화됨** → 수동으로 주석 처리 필요 (`main.c:131`)
2. **DAC OutputBuffer 누락** → 수동으로 추가 필요 (`main.c:245, 255`)
3. **DMA NodeType이 2D_NODE로 생성됨** → LINEAR_NODE로 수동 변경 필요
4. **DMA DestDataWidth가 HALFWORD로 생성됨** → WORD로 수동 변경 필요
5. **DAC HighFrequency는 유지됨** ✓

### HAL 라이브러리 버그
1. **DAC_TRIGGER_T6/T7 정의 오류** → 항상 T6_TRGO 사용 (TIM7 연결)
2. **이 버그는 ST에 리포트 필요**

### 인터럽트 컨텍스트 제약
1. **HAL_Delay() 사용 금지** → 소프트웨어 루프 사용
2. **printf() 사용 가능** (UART가 폴링 모드로 동작)

---

## 10. 비교: 동작하는 것 vs 동작하지 않는 것

### ✅ 정상 작동하는 것
| 항목 | 상태 | 비고 |
|------|------|------|
| DAC Calibration | ✅ 성공 | CH1: 0, CH2: 0 |
| 사이렌 테스트 (수동 DAC) | ✅ 정상 | NO TRIGGER 모드로 재설정 후 |
| SPI 데이터 수신 | ✅ 정상 | 가변 길이 패킷 수신 |
| TIM7 카운터 | ✅ 동작 | CNT 증가, UIF 플래그 set |
| DMA 활성화 | ✅ 정상 | CCR EN=1, CSR 에러 없음 |
| DAC 활성화 | ✅ 정상 | EN1=1, DMAEN1=1, TEN1=1 |

### ❌ 동작하지 않는 것
| 항목 | 상태 | 증상 |
|------|------|------|
| DMA 데이터 전송 | ❌ 실패 | CBR1=4096으로 고정 |
| DMA Half Transfer 인터럽트 | ❌ 발생 안 함 | Samples 카운터 0 유지 |
| Software Trigger 반응 | ❌ 무반응 | CBR1 변화 없음 (테스트 필요) |
| TIM7 → DAC 트리거 | ❓ 미확인 | TRGO 신호 전달 여부 불명 |

---

## 11. STM32 예제 비교

### 예제 파일
```
C:\Users\trion\STM32Cube\Repository\STM32Cube_FW_H5_V1.5.0\
Projects\NUCLEO-H563ZI\Examples\DAC\DAC_SignalsGeneration\
```

### 주요 차이점

| 항목 | 예제 | 우리 코드 | 상태 |
|------|------|-----------|------|
| Timer | TIM1 (Advanced) | TIM7 (Basic) | ✓ 둘 다 가능 |
| DAC Clock | HCLK (250MHz) | HCLK (250MHz) | ✓ 동일 |
| DMA NodeType | LINEAR_NODE | LINEAR_NODE | ✅ 수정 완료 |
| DMA DestDataWidth | WORD (32-bit) | WORD (32-bit) | ✅ 수정 완료 |
| Circular Mode | ✓ | ✓ | ✅ 수정 완료 |
| HighFrequency | DISABLE | AUTOMATIC | ⚠️ 차이 있음 |
| 데이터 크기 | 6 samples (8-bit) | 2048 samples (16-bit) | - |

**참고:** 예제도 HighFrequency=DISABLE를 사용하지만, 250MHz HCLK에서 우리는 AUTOMATIC이 필요함 (DISABLE 시 DAC 먹통)

---

## 12. 연락처 / 참고자료

### STM32 참고 문서
- RM0481: STM32H5 Reference Manual
- AN5557: STM32H5 DAC application note
- STM32CubeH5 Examples: `Projects/NUCLEO-H563ZI/Examples/DAC/DAC_SignalsGeneration`
- STM32H5 GPDMA Manual

### 디버깅 명령어
```bash
# 빌드
cd Debug
"C:/ST/STM32CubeIDE_1.18.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.make.win32_2.2.0.202409170845/tools/bin/make.exe" -j8 all

# 플래시
STM32_Programmer_CLI.exe -c port=SWD reset=HWrst -w Debug/audio_dac_v102.elf -v -rst

# 시리얼 모니터
python -m serial.tools.miniterm COM9 115200
```

---

## 13. 세션 복구 시 체크리스트

세션이 리셋된 경우, 다음을 확인하세요:

- [ ] 최신 빌드가 타겟에 플래시되었는지 확인
- [ ] TIM7 트리거 = `DAC_TRIGGER_T6_TRGO` (not T7!)
- [ ] DCACHE 비활성화 확인 (`main.c:131`)
- [ ] DAC OutputBuffer 설정 확인 (`main.c:245, 255`)
- [ ] DAC HighFrequency = AUTOMATIC (`main.c:240`)
- [ ] DMA NodeType = LINEAR_NODE (`stm32h5xx_hal_msp.c:149, 204`)
- [ ] DMA DestDataWidth = WORD (`stm32h5xx_hal_msp.c:156, 211`)
- [ ] DMA Priority = HIGH_PRIORITY (`stm32h5xx_hal_msp.c:186, 241`)
- [ ] DMA Circular Mode 설정 확인 (`stm32h5xx_hal_msp.c:175, 230`)
- [ ] 사이렌 테스트 정상 작동 확인 (메뉴 2번)
- [ ] 현재 상태: **DMA 전송 안 됨, Software Trigger 테스트 대기 중**

---

## 14. 의심되는 근본 원인

### A. TIM7 TRGO → DAC Trigger 연결 문제
**가능성:** TIM7의 TRGO 신호가 DAC의 trigger input에 도달하지 않음

**확인 방법:**
- Software trigger로 DMA가 작동하면 → TIM7 문제 확인
- Software trigger로도 안 되면 → DMA 설정 문제

**관련 설정:**
- TIM7: MasterOutputTrigger = TIM_TRGO_UPDATE
- DAC: TSEL1 = 5 (TIM7 TRGO)

### B. DMA Request 생성/전달 문제
**가능성:** DAC가 DMA request를 생성하지 않거나, GPDMA가 request를 받지 못함

**확인 필요:**
- GPDMA1_REQUEST_DAC1_CH1 routing
- DAC DMAEN1 bit 설정 (현재 = 1)
- DMA Request generation timing

### C. Linked List Mode 설정 문제
**가능성:** Circular Linked List 설정이 불완전

**확인 필요:**
- LinkedListQueue 초기화
- Node 연결 상태
- Circular mode flag

---

**마지막 업데이트:** 2025-01-11 23:45:00

**현재 진행 상황:**
- ✅ DAC 기본 동작 확인 (사이렌 테스트 정상)
- ✅ DMA Linked List Circular Mode 설정 완료
- ✅ DMA 활성화 확인 (EN=1)
- ❌ DMA 전송 발생하지 않음 (CBR1 고정)
- ⏳ Software Trigger 테스트로 근본 원인 파악 대기 중
