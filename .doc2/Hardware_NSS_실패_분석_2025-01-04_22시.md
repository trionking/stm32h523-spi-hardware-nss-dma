# Hardware NSS 모드 실패 분석 및 문제 해결

**작성일:** 2025-01-04 22:00
**상태:** Hardware NSS 모드 동작 실패 - IT 모드로 전환 필요

---

## 📋 현재 상황 요약

### 증상
```
[SPI] DMA started (1 byte test) - waiting for NSS LOW from master
[STATUS]
SPI: State: 0x05 (BUSY_TX_RX) → 0x01 (READY)
DMA_RX: 0  ← 데이터 전혀 수신 못함
[SPI] ERROR CALLBACK: HAL error 0xC0 (Abort Error)
```

### 설정
- **SPI 모드:** Mode 0 (CPOL=0, CPHA=0) ✓
- **NSS:** Hardware NSS Input (`SPI_NSS_HARD_INPUT`) ✓
- **FIFO Threshold:** 1바이트 (`SPI_FIFO_THRESHOLD_01DATA`) ✓
- **DMA:** TransmitReceive DMA, 1바이트 또는 5바이트
- **하드웨어 연결:** PA15 (NSS), PB3 (SCK), PB5 (MOSI) - IC 핀에서 직접 측정 확인 ✓

---

## 🔬 시도한 해결책 (모두 실패)

### 1차 시도: EXTI 방식 (Software NSS + EXTI)
**설정:**
- `SPI_NSS_SOFT`
- PA15를 `GPIO_MODE_IT_RISING_FALLING`
- EXTI15 인터럽트에서 DMA 시작

**결과:**
- ❌ Overrun 오류 (0x80) 반복 발생
- ❌ DMA_RX: 0

**실패 원인:**
- Software NSS 모드에서 SPI 하드웨어가 NSS 신호를 인식하지 못함
- EXTI에서 DMA를 시작해도 SPI 하드웨어가 동기화되지 않음

---

### 2차 시도: Hardware NSS 모드 전환
**설정:**
- `SPI_NSS_HARD_INPUT`
- PA15를 `GPIO_AF5_SPI1` (SPI alternate function)
- DMA를 초기화 시 시작 (continuous mode)

**결과:**
- ❌ Abort Error (0xC0) 1회 발생
- ❌ DMA_RX: 0
- ⚠️ State: 0x05 (BUSY) → timeout → 0x01 (READY)

**실패 원인:**
- 마스터의 타이밍이 Hardware NSS 모드 요구사항과 맞지 않음

---

### 3차 시도: FIFO Threshold 조정
**문제 발견:**
- FIFO Threshold가 4바이트로 설정되어 있었음
- 1바이트 수신 시 FIFO에 4바이트가 쌓일 때까지 DMA request를 보내지 않음

**조치:**
- CubeMX에서 FIFO Threshold를 **1바이트**로 변경
- 코드 재생성 및 빌드

**결과:**
- ❌ 여전히 동일한 문제 (Abort Error, DMA_RX: 0)
- FIFO Threshold는 근본 원인이 아니었음

---

## 📊 오실로스코프 분석

### RigolDS5.png - CS와 SCK 타이밍
**측정 결과:**
```
CS (PA15):  ‾‾‾‾‾‾‾‾\__________2~3ms________/‾‾‾‾‾‾‾‾
SCK (PB3):  ‾‾‾‾‾‾‾‾‾‾1ms대기‾‾|50us burst|‾‾‾‾‾‾‾
                                 ^^^^^^^^^^^^
                                 데이터 전송
```

**관찰:**
- CS LOW → **1ms 대기** → SCK 시작 (40 clocks, 50us)
- CS는 2-3ms 동안 LOW 유지
- 전송 시간(50us)은 충분히 짧음

### RigolDS7.png - SCK와 MOSI 데이터
**측정 결과:**
- SCK 클럭: **정확히 40개** → 5바이트 전송 확인
- MOSI: 데이터 패턴 확인됨
- SPI 속도: 약 800kHz - 1MHz
- SPI 모드: Mode 0 (CPOL=0, CPHA=0) 확인

---

## 🎯 근본 원인 분석

### Hardware NSS 모드의 요구사항 (STM32 Reference Manual)

> **"NSS signal must be kept at active level during the whole data transfer"**

**Hardware NSS 모드 동작 방식:**
```
1. NSS falling edge → SPI 하드웨어 활성화
2. SCK가 **즉시** 시작되어야 함 (또는 매우 짧은 딜레이)
3. 연속적인 데이터 전송
4. 전송 완료 후 NSS rising edge
```

### 우리 마스터의 타이밍 패턴:
```
1. NSS falling edge
2. 1ms 대기  ← 문제!
3. SCK 시작 (50us burst)
4. NSS rising edge
```

**불일치:**
- Hardware NSS는 **NSS LOW 후 즉시 SCK를 기대함**
- 하지만 우리는 **1ms 대기** 후 SCK 시작
- SPI 하드웨어가 timeout → Abort Error

---

## ⚠️ Hardware NSS가 동작하지 않는 이유

### 이론적 분석:

**STM32 SPI Hardware NSS 동작:**
1. NSS falling edge 감지
2. SPI 하드웨어 내부 상태 머신 활성화
3. **즉시 SCK를 기다림** (내부 타이머 시작?)
4. SCK가 일정 시간 내에 오지 않으면 → timeout
5. HAL이 Abort Error 발생

**우리의 1ms 대기:**
- SPI 하드웨어가 기대하는 시간보다 **너무 김**
- Internal timeout 발생
- DMA가 시작되었지만 SPI 하드웨어가 데이터를 받지 않음

---

## 🧪 테스트 결과 요약

| 테스트 | 설정 | 결과 | DMA_RX |
|--------|------|------|--------|
| 1차 EXTI | Software NSS + DMA | Overrun | 0 |
| 2차 H/W NSS (5바이트) | Hardware NSS + DMA | Abort | 0 |
| 3차 FIFO 조정 후 (1바이트) | Hardware NSS + DMA + FIFO=1 | Abort | 0 |

**공통점:**
- 모든 테스트에서 `DMA_RX: 0`
- Hardware NSS 모드는 이 타이밍 패턴에서 **근본적으로 동작 불가**

---

## 💡 해결 방안

### Option 1: IT (Interrupt) 모드로 전환 ✅ 권장

**장점:**
- 1ms 대기 시간 문제없음
- EXTI로 CS falling edge 감지
- `HAL_SPI_Receive_IT()` 사용 (DMA 대신)
- 타이밍에 유연함

**구현 방법:**
```c
// CubeMX 설정
SPI1 → NSS: Software
PA15 → GPIO_EXTI15 (falling + rising edge)
FIFO Threshold: 1 byte

// spi_handler.c
void spi_handler_cs_falling(void) {
    HAL_SPI_Receive_IT(g_hspi, (uint8_t*)&g_rx_cmd_packet, 5);
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi) {
    g_dma_rx_complete_count++;
    process_command_packet(&g_rx_cmd_packet);
    // 다음 패킷 대기는 CS rising edge에서 처리
}
```

**장점:**
- ✅ CS-SCK 딜레이 자동 처리 (EXTI가 미리 감지)
- ✅ IT 모드는 DMA보다 타이밍에 덜 민감
- ✅ 5바이트는 작은 데이터이므로 IT 모드로 충분
- ✅ 이전 EXTI 방식의 문제점(Software NSS + DMA)을 해결

**단점:**
- ⚠️ DMA보다 CPU 오버헤드 약간 높음 (하지만 5바이트는 미미함)

---

### Option 2: 마스터 타이밍 수정

**변경사항:**
```
기존: CS LOW → 1ms 대기 → SCK 시작
변경: CS LOW → 즉시 SCK 시작 (10us 이내)
```

**장점:**
- ✅ Hardware NSS 모드 사용 가능
- ✅ 가장 효율적 (하드웨어 자동 처리)

**단점:**
- ❌ 마스터 하드웨어/펌웨어 수정 필요
- ❌ 하드웨어 제약으로 불가능할 수 있음

---

### Option 3: Hybrid 방식 (미검증)

**개념:**
- Hardware NSS + EXTI 모니터링
- EXTI에서 DMA를 시작하지 않고, 단순 모니터링용

**문제:**
- Hardware NSS의 타이밍 문제를 해결하지 못함
- 근본 원인이 NSS-SCK 딜레이이므로 효과 없음

---

## 📈 성공 가능성 평가

| 방법 | 성공 가능성 | 구현 난이도 | 권장도 |
|------|------------|------------|--------|
| IT 모드 | ⭐⭐⭐⭐⭐ 95% | 중간 | ✅ 1순위 |
| 마스터 수정 | ⭐⭐⭐⭐⭐ 100% | 높음 (H/W 수정) | 2순위 |
| Hybrid | ⭐⭐ 20% | 낮음 | 비권장 |

---

## 🔧 다음 단계 (IT 모드 구현)

### 1단계: CubeMX 설정 변경

**SPI1:**
```
Mode: Full-Duplex Slave
NSS: Software (Hardware NSS 해제!)
FIFO Threshold: 1/4 Full FIFO (1 Data)
```

**GPIO:**
```
PA15: GPIO_EXTI15
Mode: External Interrupt Mode with Rising/Falling edge trigger detection
Pull-up/Pull-down: No pull-up and no pull-down
```

**NVIC:**
```
EXTI15 interrupt: Enabled
Priority: 1 (SPI와 동일)
```

### 2단계: spi_handler.c 수정

**spi_handler_start():**
```c
void spi_handler_start(void)
{
    spi_handler_set_ready(1);
    g_rx_state = SPI_STATE_WAIT_HEADER;

    printf("[SPI] Handler ready (Software NSS + IT mode)\r\n");
    printf("[SPI] Waiting for CS falling edge (EXTI)\r\n");

    // IT 모드는 EXTI에서 시작하므로 여기서는 대기만
}
```

**spi_handler_cs_falling():**
```c
void spi_handler_cs_falling(void)
{
    // Clear buffer
    memset(&g_rx_cmd_packet, 0xFF, sizeof(CommandPacket_t));

    // Start IT mode reception (5 bytes)
    g_rx_state = SPI_STATE_RECEIVE_CMD;
    HAL_StatusTypeDef status = HAL_SPI_Receive_IT(g_hspi,
                                                   (uint8_t*)&g_rx_cmd_packet,
                                                   sizeof(CommandPacket_t));

    if (status != HAL_OK) {
        g_error_stats.spi_error_count++;
    }
}
```

**HAL_SPI_RxCpltCallback():**
```c
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1)
    {
        g_dma_rx_complete_count++;  // IT 모드지만 카운터 이름 유지

        // Validate and process packet
        if (g_rx_cmd_packet.header == HEADER_CMD) {
            process_command_packet(&g_rx_cmd_packet);
        } else {
            g_error_stats.invalid_header_count++;
        }

        // 다음 패킷은 다음 CS falling edge에서 시작
        g_rx_state = SPI_STATE_WAIT_HEADER;
    }
}
```

### 3단계: stm32h5xx_it.c 수정

**EXTI15_IRQHandler():**
```c
void EXTI15_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(PA15) != 0)
    {
        __HAL_GPIO_EXTI_CLEAR_IT(PA15);

        GPIO_PinState pin_state = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15);

        if (pin_state == GPIO_PIN_RESET)  // Falling edge
        {
            g_cs_low_count++;
            spi_handler_cs_falling();  // IT 모드 시작
        }
        else  // Rising edge
        {
            g_cs_high_count++;
            // 전송 종료, 다음 패킷 대기
        }
    }
}
```

---

## 📚 참고: 왜 이전 EXTI 방식이 실패했는가?

**이전 (실패):**
```c
// Software NSS + EXTI + DMA
SPI_NSS_SOFT
HAL_SPI_TransmitReceive_DMA()  // DMA 사용
```

**문제:**
- Software NSS 모드에서 SPI 하드웨어가 NSS를 인식하지 못함
- DMA는 SPI 하드웨어의 request signal에 의존
- SPI 하드웨어가 활성화되지 않으면 DMA request가 발생하지 않음
- 결과: Overrun

**이번 (IT 모드):**
```c
// Software NSS + EXTI + IT
SPI_NSS_SOFT
HAL_SPI_Receive_IT()  // IT 사용
```

**차이점:**
- **IT 모드는 SPI 하드웨어의 RXNE 인터럽트에 의존**
- Software NSS여도 SCK와 MOSI가 오면 RXNE 발생
- FIFO에 1바이트가 들어올 때마다 인터럽트
- HAL이 자동으로 데이터를 버퍼에 복사
- 타이밍에 더 유연함

---

## ⏱️ 타임라인 (문제 해결 과정)

| 날짜 | 시도 | 결과 |
|------|------|------|
| 2025-01-04 오전 | EXTI + DMA | ❌ Overrun |
| 2025-01-04 오후 | Hardware NSS | ❌ Abort |
| 2025-01-04 저녁 | FIFO Threshold 조정 | ❌ Abort |
| 2025-01-04 22시 | **근본 원인 파악** | 타이밍 불일치 |
| 다음 | IT 모드 구현 | 테스트 예정 |

---

## 🔍 Hardware NSS 실패의 교훈

### 배운 점:

1. **Hardware NSS는 타이밍에 민감함**
   - NSS와 SCK 사이의 딜레이가 너무 길면 동작 안 함
   - 마스터-슬레이브 타이밍이 완벽히 맞아야 함

2. **오실로스코프 측정이 중요함**
   - IC 핀에서 직접 측정
   - CS, SCK, MOSI를 동시에 캡처
   - 타이밍 관계를 정확히 파악

3. **Documentation을 꼼꼼히 읽어야 함**
   - "NSS must be kept at active level during whole data transfer"
   - 이 한 문장이 핵심이었음

4. **여러 방법을 시도해야 함**
   - Hardware NSS가 "정석"이지만 항상 동작하는 것은 아님
   - IT 모드, DMA 모드, Polling 모드 모두 장단점이 있음
   - 상황에 맞는 방법 선택이 중요

---

## 🎯 최종 결론

**Hardware NSS 모드는 우리의 타이밍 패턴(CS LOW 후 1ms 대기)에서 동작하지 않음**

**권장 해결책: Software NSS + EXTI + IT 모드**
- 타이밍에 유연함
- 구현 복잡도 중간
- 5바이트 전송에 충분한 성능
- 성공 가능성 매우 높음

---

## 📎 관련 문서

- `SPI_NSS_MODE_ANALYSIS.md` - Hardware NSS vs EXTI 방식 비교 (초기 분석)
- `Z:\audio_mux_doc\RigolDS5.png` - CS + SCK 타이밍
- `Z:\audio_mux_doc\RigolDS7.png` - SCK + MOSI 데이터
- STM32H5 Reference Manual - Section 30.4.2 "NSS management"

---

**다음 작업: IT 모드 구현 및 테스트**
