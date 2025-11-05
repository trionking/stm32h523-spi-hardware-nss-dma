# SPI NSS 방식 선택 분석 및 히스토리

**작성일:** 2025-01-04
**상황:** Claude가 4번째 반복하여 Hardware NSS ↔ EXTI 방식을 왔다갔다 제안 중

---

## 1. 현재 문제 상황

### 증상
```
[SPI] ERROR CALLBACK: HAL error 0xC0 (Abort Error)
[SPI] ERROR CALLBACK: HAL error 0x80 (Overrun Error) - 반복 발생

DMA_RX: 0 (DMA 수신이 전혀 시작되지 않음)
CS 신호: 감지됨 (CS_LOW/CS_HIGH 카운터 증가)
```

### 현재 코드 상태 (2025-01-04 기준)

**SPI 설정 (main.c:405):**
```c
hspi1.Init.NSS = SPI_NSS_SOFT;  // Software NSS 모드
```

**PA15 GPIO 설정 (main.c:596-600):**
```c
GPIO_InitStruct.Pin = SPI1_EXT_NSS_Pin;  // PA15
GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;  // EXTI만 설정 (SPI AF 아님)
```

**동작 방식:**
- `spi_handler_start()` → EXTI 대기 (DMA 시작 안 함)
- EXTI15 interrupt → `spi_handler_cs_falling()` → `HAL_SPI_TransmitReceive_DMA()` 호출

---

## 2. Hardware NSS vs EXTI 방식 비교

### Hardware NSS 방식 (SPI_NSS_HARD_INPUT)

**장점:**
1. ✅ **SPI 하드웨어가 NSS 신호를 자동 감지**
2. ✅ **DMA와 SPI가 자동으로 동기화**
3. ✅ **CS-SCK 딜레이를 하드웨어가 자동 처리** (3-5ms 딜레이 문제 없음)
4. ✅ **안정성 높음** (하드웨어가 모든 타이밍 제어)
5. ✅ **CPU 오버헤드 없음** (인터럽트 불필요)

**단점:**
1. ❌ PA15를 SPI1_NSS alternate function으로 설정해야 함
2. ❌ EXTI 기능 사용 불가 (GPIO가 SPI에 할당됨)
3. ❌ 소프트웨어로 CS 감지 불가 (디버깅 어려움)

**구현 방법:**
```c
// 1. SPI 설정
hspi1.Init.NSS = SPI_NSS_HARD_INPUT;

// 2. PA15 GPIO를 SPI1_NSS alternate function으로 설정 (MSP에서)
GPIO_InitStruct.Pin = GPIO_PIN_15;  // PA15
GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;  // Alternate function
GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;  // SPI1 NSS

// 3. DMA 시작 (한 번만)
HAL_SPI_TransmitReceive_DMA(&hspi1, tx_buf, rx_buf, size);
```

---

### EXTI (외부 인터럽트) 방식

**장점:**
1. ✅ **CS 신호를 소프트웨어로 감지 가능** (디버깅 용이)
2. ✅ **CS falling/rising 엣지 모두 감지** (타이밍 체크 가능)
3. ✅ **유연한 제어** (CS 감지 시 다양한 처리 가능)

**단점:**
1. ❌ **SPI 하드웨어가 NSS를 인식하지 못함** (Software NSS 모드 필요)
2. ❌ **타이밍 동기화 어려움** (EXTI → DMA 시작 사이 딜레이)
3. ❌ **CS-SCK 딜레이 대응 어려움** (3-5ms 딜레이 시 문제 발생 가능)
4. ❌ **인터럽트 오버헤드** (매 CS 신호마다 인터럽트 발생)
5. ❌ **DMA 시작 실패 가능성** (SPI가 이미 데이터를 받기 시작한 경우)

**구현 방법:**
```c
// 1. SPI 설정
hspi1.Init.NSS = SPI_NSS_SOFT;  // Software NSS (하드웨어가 NSS 체크 안 함)

// 2. PA15 GPIO를 EXTI로 설정
GPIO_InitStruct.Pin = SPI1_EXT_NSS_Pin;
GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;  // EXTI

// 3. EXTI 인터럽트에서 DMA 시작
void EXTI15_IRQHandler(void) {
    if (falling_edge) {
        HAL_SPI_TransmitReceive_DMA(&hspi1, tx_buf, rx_buf, size);
    }
}
```

**근본적인 문제:**
- PA15가 SPI alternate function이 아니므로 **SPI 하드웨어가 NSS 신호를 받지 못함**
- Software NSS 모드이므로 SPI는 **항상 활성화 상태**
- DMA는 시작되지만 SPI 하드웨어가 **NSS LOW를 감지 못해 동기화 실패**
- 결과: **Overrun 발생**

---

## 3. 실제 테스트 결과 (사용자 리포트)

### Hardware NSS 방식
- **성공률: 50%** (2번 수신 중 1번 성공)
- 일부 데이터는 정상 수신됨
- 불안정하지만 부분적으로 동작함

### EXTI 방식
- **성공률: 0%** (완전 실패)
- 엉망인 데이터가 들어오거나 아예 수신 안 됨
- DMA_RX 카운터가 0 (DMA가 아예 동작하지 않음)

---

## 4. 왜 Hardware NSS를 선택해야 하는가?

### 이론적 근거

현재 로그를 보면:
```
DMA_RX: 0  ← DMA 수신이 전혀 시작되지 않음
CS_LOW: 5, CS_HIGH: 5  ← EXTI는 정상 동작
SPI Errors: 5 (0x80 Overrun)  ← Overrun 발생
```

**문제의 근본 원인:**
1. PA15가 SPI NSS alternate function이 아님 (EXTI로만 설정됨)
2. `SPI_NSS_SOFT` 모드 → SPI 하드웨어가 NSS 신호를 체크하지 않음
3. EXTI에서 DMA를 시작하지만, **SPI 하드웨어는 NSS를 인식 못함**
4. 마스터가 클럭을 보내면 SPI 하드웨어가 수신 못하고 **Overrun 발생**

**Hardware NSS가 필요한 이유:**
- SPI 하드웨어가 **NSS LOW를 감지해야 수신 시작**
- DMA와 SPI가 **자동으로 동기화**
- 하드웨어가 **CS-SCK 딜레이를 자동 처리**

### 실무적 근거

Hardware NSS 방식이 50% 성공한 이유:
- **SPI 하드웨어가 NSS를 감지**하므로 기본 동작은 정상
- 실패하는 50%는 다른 문제 (타이밍, 버스 충돌, 전기적 문제 등)
- **디버깅 가능한 상태**

EXTI 방식이 완전 실패한 이유:
- SPI 하드웨어가 **NSS를 아예 인식하지 못함**
- DMA는 시작되지만 **SPI는 동작하지 않음**
- **근본적인 구조적 문제**

---

## 5. Hardware NSS 구현 시 주의사항

### CubeMX 설정 변경 필요

**⚠️ 중요: main.c는 CubeMX에서 재생성되므로 직접 수정 금지!**

**CubeMX에서 설정:**
1. SPI1 → NSS → "Hardware NSS Input Signal"로 변경
2. PA15 → SPI1_NSS로 자동 설정됨
3. 코드 재생성

### MSP 변경 사항 (stm32h5xx_hal_msp.c)

CubeMX 재생성 후 PA15가 자동으로 추가됨:
```c
void HAL_SPI_MspInit(SPI_HandleTypeDef* hspi)
{
    // PA15가 SPI1_NSS로 추가됨
    GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);  // 또는 GPIOB
}
```

### spi_handler.c 변경 사항

**제거할 코드:**
```c
// spi_handler_start()에서
// EXTI 대기 코드 제거 - 더 이상 필요 없음
```

**추가할 코드:**
```c
void spi_handler_start(void)
{
    // RDY 핀 HIGH (ready)
    spi_handler_set_ready(1);

    // Hardware NSS 모드: DMA를 바로 시작 (한 번만)
    HAL_SPI_TransmitReceive_DMA(g_hspi, g_dummy_tx, (uint8_t*)&g_rx_cmd_packet,
                                 sizeof(CommandPacket_t));

    printf("[SPI] Hardware NSS mode - DMA started (continuous mode)\r\n");
}
```

### stm32h5xx_it.c 변경 사항

**제거할 코드:**
```c
// EXTI15_IRQHandler 내부의 CS 감지 코드 제거
// spi_handler_cs_falling(), spi_handler_cs_rising() 호출 제거
```

---

## 6. EXTI 방식으로 돌아갈 때 참고사항

### EXTI 방식이 동작하려면

**필수 조건:**
1. PA15를 **SPI NSS alternate function으로 설정** (Hardware NSS 활성화)
2. **동시에 EXTI도 활성화** (STM32는 AF + EXTI 동시 가능)
3. SPI를 `SPI_NSS_HARD_INPUT` 모드로 설정
4. EXTI는 **모니터링용으로만 사용** (DMA 시작 X)

**하이브리드 방식 (Hardware NSS + EXTI 모니터링):**
```c
// CubeMX 설정
// - SPI1_NSS: Hardware NSS Input
// - PA15: SPI1_NSS alternate function
// - PA15 EXTI도 활성화 (모니터링용)

// spi_handler_start()
void spi_handler_start(void)
{
    // DMA 시작 (한 번만)
    HAL_SPI_TransmitReceive_DMA(&hspi1, tx, rx, size);
}

// EXTI15_IRQHandler (모니터링용)
void EXTI15_IRQHandler(void)
{
    // CS 신호를 감지하지만 DMA를 시작하지는 않음
    // 디버깅 카운터만 증가
    if (falling_edge) {
        g_cs_low_count++;
        // LED 토글 등 디버깅용
    }
}
```

### 순수 EXTI 방식은 권장하지 않음

**이유:**
- SPI 하드웨어가 NSS를 감지하지 못하면 **근본적으로 동작 불가**
- 타이밍 동기화 문제
- Overrun 발생 가능성 높음

---

## 7. 결론 및 권장 사항

### 우선순위 1: Hardware NSS 방식 (단순하고 안정적)

**현재 상황:**
- Hardware NSS: 50% 성공 (동작 가능, 디버깅 필요)
- EXTI: 0% 성공 (근본적 구조 문제)

**권장 사항:**
1. **Hardware NSS 방식으로 구현** (CubeMX 재설정)
2. 50% 성공률을 100%로 개선 (타이밍, 전기적 이슈 해결)
3. EXTI 없이 동작 확인

### 우선순위 2: Hybrid 방식 (Hardware NSS + EXTI 모니터링)

**적용 시점:**
- Hardware NSS가 100% 안정화된 후
- 디버깅이 필요한 경우

**장점:**
- Hardware NSS의 안정성 유지
- EXTI로 CS 신호 모니터링 가능
- 최고의 디버깅 환경

### 우선순위 3: 순수 EXTI 방식 (비권장)

**적용 조건:**
- 하드웨어 제약으로 PA15를 SPI NSS로 사용 불가능한 경우만
- 그 외에는 권장하지 않음

---

## 8. 체크리스트

### Hardware NSS 전환 시

- [ ] CubeMX에서 SPI1 NSS를 "Hardware NSS Input"으로 변경
- [ ] PA15가 SPI1_NSS alternate function으로 자동 설정되는지 확인
- [ ] 코드 재생성 후 main.c 확인: `hspi1.Init.NSS = SPI_NSS_HARD_INPUT`
- [ ] MSP 확인: PA15가 GPIO_AF5_SPI1로 설정되는지 확인
- [ ] spi_handler.c: `spi_handler_start()`에서 DMA 바로 시작하도록 수정
- [ ] stm32h5xx_it.c: EXTI15 핸들러 정리 (CS 감지 코드 제거)
- [ ] 빌드 및 테스트

### 문제 발생 시 체크포인트

**Hardware NSS 모드에서도 실패하면:**
1. 마스터와 슬레이브의 SPI 클럭 모드 일치 확인 (CPOL, CPHA)
2. 전기적 신호 품질 확인 (오실로스코프)
3. DMA 버퍼 alignment 확인 (32-byte aligned)
4. DMA priority 확인
5. 마스터의 CS-SCK 딜레이 확인 (최소 몇 us 필요)

---

## 9. 히스토리

| 날짜 | 시도한 방식 | 결과 | 비고 |
|------|------------|------|------|
| 2025-01-04 (1차) | EXTI 방식 | 실패 | Overrun, DMA_RX=0 |
| 2025-01-04 (2차) | Hardware NSS | 50% 성공 | 부분 동작 |
| 2025-01-04 (3차) | EXTI 방식 | 실패 | 반복 |
| 2025-01-04 (4차) | Hardware NSS 제안 | 문서화 | ← 현재 |

**반복 원인:**
- Claude가 로그만 보고 EXTI 방식 제안
- 테스트 후 실패하면 다시 Hardware NSS 제안
- 이전 히스토리를 참고하지 못함

**해결책:**
- 이 문서를 참고하여 Hardware NSS 방식으로 일관되게 진행
- EXTI 방식은 구조적 문제가 있음을 인지

---

## 10. 참고 자료

**STM32H5 Reference Manual:**
- SPI NSS 모드: Section 30.4.2 "NSS management"
- Hardware NSS input for slave: NSS 신호가 LOW일 때만 SPI 활성화

**HAL 함수:**
- `HAL_SPI_TransmitReceive_DMA()`: DMA를 이용한 송수신 시작
- `HAL_SPI_TxRxCpltCallback()`: 송수신 완료 콜백

**핀 배치:**
- PB3: SPI1_SCK
- PB4: SPI1_MISO
- PB5: SPI1_MOSI
- PA15: SPI1_NSS (Hardware NSS로 설정 필요)

---

**최종 결론: Hardware NSS 방식으로 진행하되, 이 문서를 참고하여 더 이상 EXTI 방식으로 돌아가지 말 것.**
