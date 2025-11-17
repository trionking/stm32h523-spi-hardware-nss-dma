# DAC Independent Mode 구현 완료 보고서 (최종)

**날짜**: 2025-11-18 (최종 완료)
**프로젝트**: STM32H523 오디오 DAC (audio_dac_v102)
**목표**: Dual DAC Mode → Independent DAC Mode 전환 및 양 채널 완전 독립 동작
**결과**: ✅ **성공! DAC CH1 및 CH2 모두 정상 동작**

---

## 📊 최종 상태

### DAC CH1 (PA4)
| 항목 | 상태 |
|------|------|
| DMA 인터럽트 | ✅ 정상 동작 |
| 오디오 출력 | ✅ 소리 나옴 |
| 트리거 | TIM1_TRGO (32kHz) |
| DMA 채널 | GPDMA2_Channel0 |
| 콜백 함수 | HAL_DAC_ConvCpltCallbackCh1 |

### DAC CH2 (PA5)
| 항목 | 상태 |
|------|------|
| DMA 인터럽트 | ✅ 정상 동작 (수정 후) |
| 오디오 출력 | ✅ 소리 나옴 |
| 트리거 | TIM7_TRGO (32kHz) |
| DMA 채널 | GPDMA2_Channel1 |
| 콜백 함수 | HAL_DACEx_ConvCpltCallbackCh2 ⭐ |

---

## 🎯 프로젝트 목표

### 초기 문제
- **Dual DAC Mode**: 두 채널이 하나의 DMA로 동작 (DHR12RD 사용)
- 한 채널만 재생할 때도 두 채널 모두 영향받음
- 각 채널이 독립적으로 동작할 수 없음

### 최종 목표
- **Independent DAC Mode**: 각 채널이 독립적인 DMA와 트리거 사용
- DAC CH1: TIM1 트리거, GPDMA2_Channel0
- DAC CH2: TIM7 트리거, GPDMA2_Channel1
- 각 채널이 독립적으로 재생/정지 가능

---

## 🔧 문제 해결 과정

### Phase 1: DAC CH1 성공 (2025-01-13)

**참고 문서**: `DAC_DMA_SUCCESS_2025-01-13_23-30-00.md`

**핵심 해결책**:
- DMA DestDataWidth를 HALFWORD → **WORD**로 변경
- DAC DHR12R1/DHR12R2 레지스터는 32비트이므로 WORD로 써야 함

```c
// stm32h5xx_hal_msp.c
NodeConfig.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_HALFWORD;
NodeConfig.Init.DestDataWidth = DMA_DEST_DATAWIDTH_WORD;  // ✅ CRITICAL
```

**결과**: DAC CH1 DMA 인터럽트 정상 동작, 오디오 출력 성공

---

### Phase 2: DAC CH2 트리거 설정 (2025-11-18 초기)

**문제**: DAC CH2는 모든 설정이 올바른데도 DMA 전송이 시작되지 않음

**초기 가설**: STM32H5 HAL 버그 - TSEL 매핑이 잘못되었다고 추측
- 문서에서 DAC_TRIGGER_T6_TRGO (TSEL=5)를 TIM7로 사용하려 함
- 실제로는 이것이 **TIM6**였음

**레퍼런스 매뉴얼 확인 결과**:

| TSEL 값 | HAL 정의 | 실제 트리거 |
|---------|----------|------------|
| 5 | DAC_TRIGGER_T6_TRGO | **TIM6** TRGO |
| 6 | DAC_TRIGGER_T7_TRGO | **TIM7** TRGO ✅ |

**수정 내용**:

#### 1. main.c (Core/Src/main.c:266)
```c
// 수정 전
sConfig.DAC_Trigger = DAC_TRIGGER_T6_TRGO;  // TSEL=5 → TIM6 ❌

// 수정 후
sConfig.DAC_Trigger = DAC_TRIGGER_T7_TRGO;  // TSEL=6 → TIM7 ✅
```

#### 2. user_def.c (Core/Src/user_def.c:1281)
```c
// 수정 전
sConfig.DAC_Trigger = DAC_TRIGGER_T6_TRGO;  // TSEL=5 → TIM6 ❌

// 수정 후
sConfig.DAC_Trigger = DAC_TRIGGER_T7_TRGO;  // TSEL=6 → TIM7 ✅
```

**결과**: TSEL2=6으로 올바르게 설정되고 TIM7이 정상 동작하지만... **여전히 DMA 인터럽트 발생 안 함!**

---

### Phase 3: 콜백 함수 이름 불일치 발견 (2025-11-18 최종)

**핵심 발견**: 인터럽트는 발생했지만 **콜백 함수 이름이 달라서** weak 함수(빈 함수)만 실행됨!

#### DMA 레지스터 분석

**CCR (Channel Control Register) = 0x00825F01**
```
활성화된 인터럽트:
- TCIE (Transfer Complete Interrupt) = 1 ✅
- HTIE (Half Transfer Interrupt) = 1 ✅
→ 인터럽트는 모두 활성화되어 있음!
```

**CSR (Channel Status Register) = 0x00040000**
```
- TCF (Transfer Complete Flag) = 0
- HTF (Half Transfer Flag) = 0
→ 인터럽트 플래그가 설정되지 않음
→ 왜? 콜백이 호출되지 않아서!
```

#### HAL 라이브러리 분석 (Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_dac_ex.c:970)

```c
#if (USE_HAL_DAC_REGISTER_CALLBACKS == 1)
  hdac->ConvHalfCpltCallbackCh2(hdac);
#else
  HAL_DACEx_ConvHalfCpltCallbackCh2(hdac);  // ⭐ Ex 포함!
#endif
```

**문제**: 구현된 함수는 `HAL_DAC_ConvHalfCpltCallbackCh2` (Ex 없음)

#### 함수 이름 비교

| 채널 | HAL이 호출하는 함수 | 구현해야 할 함수 |
|------|-------------------|-----------------|
| CH1 | `HAL_DAC_ConvCpltCallbackCh1` | `HAL_DAC_ConvCpltCallbackCh1` |
| CH2 | `HAL_DACEx_ConvCpltCallbackCh2` | `HAL_DACEx_ConvCpltCallbackCh2` ⭐ |

**왜 CH2만 Ex가 붙는가?**
- CH1은 기본 DAC 기능 (stm32h5xx_hal_dac.c)
- CH2는 확장 DAC 기능 (stm32h5xx_hal_dac_ex.c)
- HAL 설계상 CH2는 "Extended" 기능으로 분류됨

#### 최종 수정 (Core/Src/stm32h5xx_it.c)

```c
// 수정 전 (❌ 호출되지 않음)
void HAL_DAC_ConvHalfCpltCallbackCh2(DAC_HandleTypeDef *hdac) { ... }
void HAL_DAC_ConvCpltCallbackCh2(DAC_HandleTypeDef *hdac) { ... }
void HAL_DAC_ErrorCallbackCh2(DAC_HandleTypeDef *hdac) { ... }

// 수정 후 (✅ 정상 호출됨)
void HAL_DACEx_ConvHalfCpltCallbackCh2(DAC_HandleTypeDef *hdac) { ... }
void HAL_DACEx_ConvCpltCallbackCh2(DAC_HandleTypeDef *hdac) { ... }
void HAL_DACEx_ErrorCallbackCh2(DAC_HandleTypeDef *hdac) { ... }
```

**결과**: DAC CH2 DMA 인터럽트 정상 발생! 오디오 출력 성공! 🎉

---

## 📝 전체 수정 내용 요약

### 1. main.c (Core/Src/main.c)

**Line 266: DAC CH2 트리거 설정**
```c
sConfig.DAC_Trigger = DAC_TRIGGER_T7_TRGO;  // TSEL=6 → TIM7 TRGO
```

**Line 278: 주석 업데이트**
```c
// DAC2: TEN2=1, DMAEN2=1, TSEL2=6 (TIM7_TRGO)
```

### 2. user_def.c (Core/Src/user_def.c)

**Line 1281: DAC CH2 캘리브레이션 트리거**
```c
sConfig.DAC_Trigger = DAC_TRIGGER_T7_TRGO;  // TSEL=6 → TIM7 TRGO
```

### 3. stm32h5xx_it.c (Core/Src/stm32h5xx_it.c)

**Line 553, 562, 613: 콜백 함수 이름 수정**
```c
void HAL_DACEx_ConvHalfCpltCallbackCh2(DAC_HandleTypeDef *hdac) { ... }
void HAL_DACEx_ConvCpltCallbackCh2(DAC_HandleTypeDef *hdac) { ... }
void HAL_DACEx_ErrorCallbackCh2(DAC_HandleTypeDef *hdac) { ... }
```

### 4. spi_handler.c (Core/Src/spi_handler.c)

**Line 543-550, 634, 644: 주석 및 디버그 메시지 업데이트**
- DHR12R2 주소: 0x42028414 (정확한 주소)
- TSEL2 비트 필드: bits [21-18], 값 = 6

---

## 📊 빌드 결과

```
   text     data      bss      dec      hex    filename
 123440     1760    54336   179536    2bd50   audio_dac_v102.elf
```

**메모리 사용량**:
- Flash: 123KB / 256KB (48%)
- RAM: 54KB / 272KB (20%)

---

## ✅ 테스트 결과

### 테스트 환경
- STM32H523CCTx @ 250MHz
- SPI 슬레이브 모드, 마스터와 통신
- 샘플레이트: 32kHz
- 버퍼 크기: 2048 samples (64ms)

### DAC CH1 테스트
```
[CMD_PLAY] DAC1 Starting - DMA=0x200006EC, Buf=0x2003E240, Size=2048
[CMD_PLAY] INDEPENDENT MODE: DAC DMA started successfully
  DMA CCR: 0x00825F01 (EN=1)
  DMA CSR: 0x00000000

[STATUS] --------------------
DAC1: PLAY | Samples: 2048 | Swaps: 156 | Underruns: 0
  DMA IRQ: HalfCplt=312 | Cplt=312  ✅
```

### DAC CH2 테스트 (수정 후)
```
[CMD_PLAY] DAC2 Starting - DMA=0x2000074C, Buf=0x20040240, Size=2048
[DEBUG] DAC CH2 settings:
  EN2=1 (bit 16)
  TEN2=1 (bit 17)
  TSEL2=6 (bits 21-18, should be 6 for TIM7 TRGO)  ✅
  DMAEN2=1 (bit 28)

[STATUS] --------------------
DAC2: PLAY | Samples: 2048 | Swaps: 156 | Underruns: 0
  DMA IRQ: HalfCplt=312 | Cplt=312  ✅
```

**검증**:
- ✅ TSEL2 = 6 (TIM7 TRGO)
- ✅ DMA 인터럽트 정상 발생
- ✅ 버퍼 스왑 정상 동작
- ✅ 오디오 출력 확인
- ✅ 언더런 없음

---

## 🔍 핵심 레지스터 정보

### DAC 레지스터 주소 (STM32H523)

| 레지스터 | 오프셋 | 절대 주소 | 설명 |
|---------|--------|-----------|------|
| DAC1 Base | - | 0x42028400 | DAC1 베이스 주소 |
| DHR12R1 | 0x08 | 0x42028408 | DAC CH1, 12비트 우측 정렬 |
| DHR12R2 | 0x14 | 0x42028414 | DAC CH2, 12비트 우측 정렬 |
| DHR8R2 | 0x1C | 0x4202841C | DAC CH2, 8비트 우측 정렬 |

### DAC 트리거 선택 (TSEL[3:0])

| TSEL 값 | HAL 정의 | 트리거 소스 |
|---------|----------|------------|
| 0 (0b0000) | DAC_TRIGGER_NONE | 자동 변환 (트리거 없음) |
| 1 (0b0001) | DAC_TRIGGER_SOFTWARE | 소프트웨어 트리거 |
| 2 (0b0010) | DAC_TRIGGER_T1_TRGO | **TIM1 TRGO** (CH1 사용) |
| 5 (0b0101) | DAC_TRIGGER_T6_TRGO | **TIM6 TRGO** |
| **6 (0b0110)** | **DAC_TRIGGER_T7_TRGO** | **TIM7 TRGO** (CH2 사용) ⭐ |
| 7 (0b0111) | DAC_TRIGGER_T8_TRGO | **TIM8 TRGO** |

### GPDMA 레지스터 분석

**CCR (Channel Control Register)**
```
Bit 0: EN = 1 (채널 활성화)
Bit 8: TCIE = 1 (Transfer Complete Interrupt Enable)
Bit 9: HTIE = 1 (Half Transfer Interrupt Enable)
Bit 10-14: 기타 에러 인터럽트 활성화
Bit 17: LAP = 1 (Linked-list Allocated Port 1)
Bit 22-23: PRIO = 0b10 (HIGH Priority)
```

**CSR (Channel Status Register)**
```
Bit 0: IDLEF (Idle Flag)
Bit 8: TCF (Transfer Complete Flag)
Bit 9: HTF (Half Transfer Flag)
Bit 16-23: FIFOL (FIFO Level, 현재 FIFO의 데이터 바이트 수)
```

---

## 🎓 배운 교훈

### 1. Weak 함수 오버라이드는 정확한 시그니처가 필수

**문제**:
- 함수가 "구현되어 있다" ≠ "올바르게 호출된다"
- Weak 함수 이름이 1바이트라도 다르면 실패
- 컴파일러는 에러를 내지 않음 (weak 함수가 계속 호출됨)

**교훈**:
- HAL 소스 코드를 직접 확인해야 함
- 특히 `#if (USE_HAL_XXX_REGISTER_CALLBACKS == 1)` 조건부 컴파일 확인
- CH1과 CH2의 함수 이름이 다를 수 있음 (HAL_DAC_* vs HAL_DACEx_*)

### 2. HAL 네이밍의 일관성 부족

**DAC CH1 vs CH2**:
- CH1: `HAL_DAC_ConvCpltCallbackCh1` (표준)
- CH2: `HAL_DACEx_ConvCpltCallbackCh2` (확장, Ex 추가)

**이유**:
- CH2는 "Extended" 기능으로 분류됨 (stm32h5xx_hal_dac_ex.c)
- 이런 차이를 예상하기 어려움

**교훈**:
- HAL 문서만 믿지 말고 소스 코드 직접 확인
- grep으로 실제 호출되는 함수 이름 검색

### 3. 체계적 디버깅의 중요성

**성공적인 디버깅 순서**:
1. 하드웨어 레벨 확인 (레지스터, 클럭, 핀)
2. 인터럽트 활성화 확인 (NVIC, DMA CCR)
3. 인터럽트 발생 확인 (CSR 플래그)
4. **콜백 함수 호출 확인** ← 이 단계를 놓침!

**교훈**:
- "인터럽트가 활성화되어 있다" ≠ "콜백이 호출된다"
- 각 레이어를 개별적으로 검증해야 함
- 특히 소프트웨어 레이어(함수 이름, 링킹)도 확인 필요

### 4. 레퍼런스 매뉴얼의 중요성

**TSEL 매핑 확인**:
- 문서나 인터넷 검색만으로는 불충분
- 레퍼런스 매뉴얼(RM0481)에서 직접 확인 필요
- HAL 상수 정의도 직접 확인 (stm32h5xx_hal_dac.h)

**교훈**:
- 레퍼런스 매뉴얼은 최종 진실의 원천
- HAL 추상화를 믿되, 하드웨어 레지스터로 검증

### 5. DMA DestDataWidth의 중요성

**DAC DHR12R1/DHR12R2는 32비트 레지스터**:
- 16비트(HALFWORD)로 쓰면 Transfer Error 발생
- 반드시 32비트(WORD)로 설정해야 함

```c
NodeConfig.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_HALFWORD;  // 소스는 16비트
NodeConfig.Init.DestDataWidth = DMA_DEST_DATAWIDTH_WORD;    // 목적지는 32비트!
```

**교훈**:
- 레지스터의 실제 크기 확인 필수
- DMA 전송 시 소스와 목적지의 데이터 폭이 다를 수 있음

---

## ⚠️ 남은 작업: RDY 핀 확장 (하드웨어 수정 필요)

### 현재 상태

**RDY 핀 1개로는 2개 채널의 독립적인 상태 표현 불가**:

```
시나리오:
- DAC CH1: 버퍼 가득 찼음 (RDY = HIGH 원함)
- DAC CH2: 버퍼 비어있음 (RDY = LOW 원함)
→ RDY 핀은 하나의 상태만 가능! 충돌 발생
```

**문제점**:
- 한 채널은 데이터를 받을 준비가 되었는데
- 다른 채널은 버퍼가 가득 차서 못 받는 상황
- RDY 핀이 어떤 상태를 표시해야 할지 모호함

### 해결 방법: 하드웨어 수정 (권장)

**RDY 핀 2개로 확장**:
- RDY1 (PA8) - DAC CH1 상태 표시
- RDY2 (새 GPIO 핀) - DAC CH2 상태 표시

**장점**:
- ✅ 완벽한 채널 독립성
- ✅ 실시간 플로우 컨트롤
- ✅ 마스터가 각 채널 상태를 즉시 확인
- ✅ 로직이 단순하고 명확
- ✅ 오디오 스트리밍의 타이밍 요구사항 충족

**필요 작업**:
1. PCB 재설계 (RDY2 핀 추가)
2. 슬레이브 펌웨어 수정 (`spi_handler_update_rdy()` 함수 분리)
3. 마스터 펌웨어 수정 (RDY1, RDY2 개별 모니터링)

**구현 예시**:
```c
// 슬레이브 (STM32H523)
void spi_handler_update_rdy(void)
{
    // CH1 상태 → RDY1 핀
    bool ch1_ready = (g_dac1_channel.fill_index < AUDIO_BUFFER_SIZE);
    HAL_GPIO_WritePin(RDY1_GPIO_Port, RDY1_Pin,
                      ch1_ready ? GPIO_PIN_RESET : GPIO_PIN_SET);

    // CH2 상태 → RDY2 핀
    bool ch2_ready = (g_dac2_channel.fill_index < AUDIO_BUFFER_SIZE);
    HAL_GPIO_WritePin(RDY2_GPIO_Port, RDY2_Pin,
                      ch2_ready ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
```

### 임시 해결책 (소프트웨어, 하드웨어 수정 없음)

**Option: OR 로직**
```c
void spi_handler_update_rdy(void)
{
    bool ch1_ready = (g_dac1_channel.fill_index < AUDIO_BUFFER_SIZE);
    bool ch2_ready = (g_dac2_channel.fill_index < AUDIO_BUFFER_SIZE);

    // 양쪽 채널 모두 준비되었을 때만 RDY = LOW
    if (ch1_ready && ch2_ready) {
        HAL_GPIO_WritePin(OT_RDY_GPIO_Port, OT_RDY_Pin, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(OT_RDY_GPIO_Port, OT_RDY_Pin, GPIO_PIN_SET);
    }
}
```

**단점**:
- 한 채널이 가득 차면 다른 채널도 데이터 못 받음
- 비효율적 (한쪽 채널이 대기)
- 동시 재생 시 성능 저하

**권장 사항**:
- 단일 채널 재생: 현재 구현으로 문제없음
- 동시 재생: 하드웨어 수정 필요 (RDY 핀 2개)

---

## 📚 관련 문서

### 프로젝트 문서

1. **DAC_DMA_SUCCESS_2025-01-13_23-30-00.md**
   - DAC CH1 DMA 성공 과정
   - DestDataWidth 문제 해결
   - Port 설정 및 레지스터 분석

2. **DAC_INDEPENDENT_MODE_IMPLEMENTATION_2025-11-18.md**
   - Independent Mode 초기 구현
   - TSEL 버그 추적 (잘못된 가설)
   - DAC CH2 디버깅 과정

3. **STM32H5_CODEGEN_BUGS.md**
   - CubeMX 코드 생성 버그 목록
   - GPDMA2 관련 버그 패치
   - 코드 재생성 후 체크리스트

4. **CLAUDE.md**
   - 프로젝트 전체 개요
   - 빌드 시스템 및 아키텍처
   - 주변장치 사용법

### ST 문서

1. **Reference Manual (RM0481)**
   - Section: DAC (Digital-to-Analog Converter)
   - Section: TIM7 (Basic Timer)
   - Section: GPDMA (General Purpose DMA)
   - DAC 레지스터 주소 및 TSEL 매핑표

2. **HAL Driver Source**
   - `stm32h5xx_hal_dac.c` - DAC CH1 기본 기능
   - `stm32h5xx_hal_dac_ex.c` - DAC CH2 확장 기능
   - `stm32h5xx_hal_dac.h` - DAC 상수 정의
   - `stm32h523xx.h` - 레지스터 정의

---

## 🎯 체크리스트

### ✅ 완료된 작업

- [x] Dual Mode → Independent Mode 코드 변경
- [x] main.c DAC 초기화 수정 (TSEL=6)
- [x] user_def.c 캘리브레이션 수정 (TSEL=6)
- [x] spi_handler.c CMD_PLAY 수정
- [x] stm32h5xx_hal_msp.c DestDataWidth 수정 (WORD)
- [x] DAC CH1 DMA 동작 검증
- [x] DAC CH1 오디오 출력 확인
- [x] TIM7 트리거 설정 수정 (DAC_TRIGGER_T7_TRGO)
- [x] 레퍼런스 매뉴얼 확인 (TSEL 매핑)
- [x] **콜백 함수 이름 수정 (HAL_DACEx_*)**
- [x] DAC CH2 DMA 동작 검증
- [x] DAC CH2 오디오 출력 확인
- [x] 빌드 성공 및 테스트 완료

### 🚧 향후 작업

- [ ] **하드웨어 수정: RDY 핀 2개로 확장**
  - [ ] PCB 재설계
  - [ ] 슬레이브 펌웨어 수정 (RDY1, RDY2 개별 제어)
  - [ ] 마스터 펌웨어 수정 (RDY1, RDY2 개별 모니터링)
- [ ] 동시 재생 테스트 (양 채널 동시 오디오 스트리밍)
- [ ] 성능 최적화 (CPU 사용률, 지터 측정)
- [ ] 에러 핸들링 개선 (언더런 복구 로직)
- [ ] 문서화 업데이트 (CLAUDE.md에 RDY 핀 정보 추가)

---

## 📊 최종 빌드 정보

**빌드 날짜**: 2025-11-18
**빌드 환경**: STM32CubeIDE 1.18.0, ARM GCC

```
   text     data      bss      dec      hex    filename
 123440     1760    54336   179536    2bd50   audio_dac_v102.elf
```

**메모리 사용량**:
- Flash: 123,440 bytes / 256KB (48.2%)
- RAM (data): 1,760 bytes
- RAM (bss): 54,336 bytes
- Total RAM: 56,096 bytes / 272KB (20.1%)

**주요 변경사항**:
- DAC CH2 트리거 설정 수정
- 콜백 함수 이름 수정 (3개 함수)
- 주석 및 디버그 메시지 업데이트

---

## 🎉 결론

**DAC Independent Mode 구현 완전 성공!**

두 개의 독립적인 DAC 채널이 각자의 DMA와 타이머 트리거를 사용하여 정상 동작합니다. 핵심 문제는 콜백 함수 이름 불일치였으며, HAL 소스 코드를 직접 분석하여 해결했습니다.

**주요 성과**:
- ✅ DAC CH1: TIM1 트리거, 정상 동작
- ✅ DAC CH2: TIM7 트리거, 정상 동작
- ✅ 각 채널 독립적으로 재생/정지 가능
- ✅ DMA Circular 모드 정상 동작
- ✅ 버퍼 스왑 및 언더런 감지 동작

**남은 개선 사항**:
- RDY 핀 2개로 확장 (하드웨어 수정 필요)
- 완벽한 동시 재생 지원을 위함

---

**작성자**: Claude Code
**검증 환경**: STM32H523CCTx, GPDMA2, DAC1, TIM1/TIM7
**도구**: STM32CubeIDE, VS Code, ST-Link
**펌웨어 버전**: audio_dac_v102 (build: 2025-11-18)
**최종 업데이트**: 2025-11-18
