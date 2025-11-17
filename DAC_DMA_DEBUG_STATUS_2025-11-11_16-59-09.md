# DAC DMA 디버깅 진행 상황

**날짜:** 2025-11-11 16:59:09
**프로젝트:** STM32H523 오디오 DAC (audio_dac_v102)
**목표:** DAC DMA를 통한 32kHz 오디오 재생 구현

---

## 1. 현재 상태 (최종 테스트 결과)

### ✅ 해결된 문제들

1. **SPI 데이터 수신 완료**
   - 가변 길이 패킷 수신 정상 작동 (CS edge detection)
   - 50 샘플 DATA 패킷 수신 확인 (104 bytes)
   - 데이터 손상 문제 해결 (memset race condition, SPI reset)

2. **DAC DMA 기본 설정 완료**
   - DMA 활성화 확인 (CCR EN=1)
   - TIM7 트리거 올바르게 연결 (TSEL=5)
   - DAC DMAUDR (underrun) 에러 해결
   - 버퍼 주소/정렬 확인: 0x2003E2C0 (32-byte aligned, non-cacheable)

### ⚠️ 현재 이슈

**Samples 카운터가 증가하지 않음** (0으로 유지)
- DMA가 활성화되었지만 Half Transfer Complete 인터럽트가 아직 발생하지 않음
- 실제 오디오 샘플이 버퍼에 채워지지 않았을 가능성
- 다음 테스트 필요: `SPITEST DATA 0` 여러 번 전송 → 버퍼 채우기 → STATUS 확인

### 최종 레지스터 상태

```
[첫 번째 PLAY - 성공]
DMA CCR: 0x00805F01 (EN=1)        ✅ DMA 활성화
DMA CSR: 0x00000000               ✅ 에러 없음
DMA CBR1: 4096 items              ✅ 전송 대기 중 (2048 샘플 × 2바이트)
DAC CR: 0x00163017                ✅ TSEL1=5 (TIM7), DMAEN1=1, EN1=1
DAC SR: 0x00000800                ✅ BWST만, DMAUDR 없음!

TIM7: CNT=15, ARR=7811            ✅ 32kHz @ 250MHz HCLK
      CR1=0x00000081 (CEN=1)      ✅ 타이머 실행 중
      CR2=0x00000020 (MMS=2)      ✅ Update TRGO

[두 번째 PLAY - HAL_BUSY]
DAC State: 0x02 (BUSY)            ✅ 정상 (첫 번째가 아직 실행 중)
ErrorCode: 0x00000004 (TIMEOUT)   ⚠️ BUSY 상태에서 시작 시도
```

---

## 2. 발견한 STM32 HAL 버그들

### 🐛 버그 #1: DAC_TRIGGER_T6/T7_TRGO 정의 오류

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
- `Core/Src/main.c:240` (DAC CH1 설정)
- `Core/Src/user_def.c:1142` (Calibration)

---

### 🐛 버그 #2: CubeMX DAC OutputBuffer 누락

**문제:** STM32CubeMX가 DAC OutputBuffer 설정을 생성하지 않음

**해결:** 수동으로 추가
```c
sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;  // main.c:241, 251
```

---

### 🐛 버그 #3: DCACHE 활성화 시 DMA 에러

**문제:** DCACHE와 non-cacheable RAM(0x2003C000) 충돌

**해결:** DCACHE 비활성화
```c
// MX_DCACHE1_Init();  // main.c:127 - DISABLED
```

---

## 3. 적용한 수정사항

### A. 하드웨어 설정 (STM32CubeMX)

#### DMA Priority 상승
```
GPDMA1 Channel 6 (DAC1_CH1): DMA_LOW_PRIORITY_HIGH_WEIGHT
GPDMA1 Channel 7 (DAC1_CH2): DMA_LOW_PRIORITY_HIGH_WEIGHT
```
- 이전: `DMA_LOW_PRIORITY_MID_WEIGHT`
- 파일: `Core/Src/stm32h5xx_hal_msp.c:148, 175`

#### DMA Data Width
```c
SrcDataWidth = DMA_SRC_DATAWIDTH_HALFWORD    // 16-bit
DestDataWidth = DMA_DEST_DATAWIDTH_HALFWORD  // 16-bit
```
- DAC_ALIGN_12B_R과 호환

---

### B. 소프트웨어 수정

#### 1. TIM7 트리거 수정
```c
// main.c:240, user_def.c:1142
sConfig.DAC_Trigger = DAC_TRIGGER_T6_TRGO;  // HAL 버그 우회
```

#### 2. DAC OutputBuffer 추가
```c
// main.c:241, 251
sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
```

#### 3. DAC HighFrequency 모드
```c
// main.c:236, 250
sConfig.DAC_HighFrequency = DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC;
```

#### 4. DCACHE 비활성화
```c
// main.c:127
// MX_DCACHE1_Init();  // DISABLED
```

#### 5. SPI 데이터 손상 수정
```c
// spi_handler.c:646 - memset 제거 (race condition)
// spi_handler.c:693 - SPI DeInit/ReInit 추가 (상태 초기화)
```

#### 6. 디버그 출력 수정
```c
// spi_handler.c:437, 416
printf("  DMA CCR: 0x%08lX (EN=%d)\r\n", dma_ch->CCR, ...);  // CSR → CCR
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
HSE: 25MHz (external oscillator)
HCLK: 250MHz (PLL)
APB1/2/3: 250MHz (no prescaler)
```

### TIM7 설정 (32kHz DAC 샘플레이트)
```
Prescaler: 0
ARR: 7811
Frequency: 250MHz / (0+1) / (7811+1) = 32.005 kHz
TRGO: Update Event (MMS=2)
```

### DMA 전송 타이밍
```
2048 샘플 @ 32kHz = 64ms (전체 버퍼)
1024 샘플 @ 32kHz = 32ms (Half Transfer)
```

---

## 6. 테스트 프로토콜

### 현재 테스트 시퀀스
```
1. SPITEST PLAY 0 0       → DAC DMA 시작
2. STATUS                 → 초기 상태 확인
3. SPITEST DATA 0         → 50 샘플 전송 (× N회)
4. STATUS                 → Samples/Swaps 카운터 확인
```

### 기대 결과
```
- Samples: 증가 (DMA Half Transfer마다 +1024)
- Swaps: 증가 (버퍼 스왑마다 +1)
- Underruns: 0 유지
```

---

## 7. 다음 단계

### ⚡ 긴급 테스트
1. **`SPITEST DATA 0` 여러 번 실행** (10-20회)
   - 버퍼를 실제 데이터로 채우기
   - 2048 샘플 = 41번의 DATA 명령 필요 (50 샘플씩)

2. **STATUS 확인**
   - Samples 카운터 증가 확인
   - Swaps 카운터 증가 확인

### 🔍 추가 디버깅 (필요시)
1. **DMA 레지스터 직접 모니터링**
   - CBR1 값이 감소하는지 확인 (DMA 전송 진행 중)
   - CSR 에러 플래그 확인

2. **DMA 인터럽트 핸들러에 디버그 추가**
   - Half Transfer Complete 콜백 호출 확인
   - Transfer Complete 콜백 호출 확인

3. **오실로스코프 확인** (가능한 경우)
   - PA4 (DAC CH1 출력) 파형 확인
   - 32kHz 샘플레이트 확인

---

## 8. 주요 파일 목록

### 수정된 파일
```
Core/Src/main.c                     ← DAC 트리거, OutputBuffer, DCACHE
Core/Src/user_def.c                 ← DAC Calibration 트리거
Core/Src/stm32h5xx_hal_msp.c        ← DMA Priority (CubeMX 재생성)
Core/Src/spi_handler.c              ← SPI reset, 디버그 출력 수정
STM32H523CCTX_FLASH.ld              ← MPU 설정 (이미 올바름)
```

### 설정 파일
```
audio_dac_v102.ioc                  ← CubeMX 설정
.vscode/tasks.json                  ← 빌드/플래시 태스크
```

### 참고 문서
```
CLAUDE.md                           ← 프로젝트 개요
to-master.md                        ← Master MCU 통신 가이드
```

---

## 9. 알려진 제약사항

### CubeMX 재생성 시 주의사항
1. **DCACHE가 다시 활성화됨** → 수동으로 주석 처리 필요
2. **DAC OutputBuffer 누락** → 수동으로 추가 필요
3. **DAC HighFrequency는 유지됨** ✓

### HAL 라이브러리 버그
1. **DAC_TRIGGER_T6/T7 정의 오류** → 항상 T6_TRGO 사용
2. **이 버그는 ST에 리포트 필요**

---

## 10. 연락처 / 참고자료

### STM32 참고 문서
- RM0481: STM32H5 Reference Manual
- AN5557: STM32H5 DAC application note
- STM32CubeH5 Examples: `Projects/NUCLEO-H563ZI/Examples/DAC/DAC_SignalsGeneration`

### 디버깅 명령어
```bash
# 빌드
cd Debug && make -j8 all

# 플래시
STM32_Programmer_CLI.exe -c port=SWD reset=HWrst -w Debug/audio_dac_v102.elf -v -rst

# 시리얼 모니터
python -m serial.tools.miniterm COM9 115200
```

---

## 11. 세션 복구 시 체크리스트

세션이 리셋된 경우, 다음을 확인하세요:

- [ ] 최신 빌드가 타겟에 플래시되었는지 확인
- [ ] TIM7 트리거 = `DAC_TRIGGER_T6_TRGO` (not T7!)
- [ ] DCACHE 비활성화 확인 (main.c:127)
- [ ] DAC OutputBuffer 설정 확인 (main.c:241, 251)
- [ ] DMA Priority = HIGH_WEIGHT (stm32h5xx_hal_msp.c:148, 175)
- [ ] SPI 데이터 수신 정상 동작 확인 (`SPITEST DATA 0`)
- [ ] 현재 상태: **DMA 활성화 완료, Samples 카운터 테스트 진행 중**

---

**마지막 업데이트:** 2025-11-11 16:59:09 (DMA 활성화 성공, 샘플 카운터 테스트 대기 중)
