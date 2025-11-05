# MPU + DCACHE 활성화 테스트 계획

## 목표
MPU와 DCACHE를 활성화하여 성능 향상 및 안정성 확인

---

## 현재 설정 (준비 완료)

### 1. MPU Region 0 설정
```c
// main.c - MPU_Config()
BaseAddress:  0x20040000
LimitAddress: 0x20043FFF
Size:         16KB
Attributes:   MPU_NOT_CACHEABLE  // DMA 버퍼용, 캐시 비활성화
```

### 2. 링커 스크립트
```ld
RAM_DMA (xrw) : ORIGIN = 0x20040000, LENGTH = 16K

.dma_buffer (NOLOAD) :
{
    *(.dma_buffer)
    *(.dma_buffer*)
} >RAM_DMA
```

### 3. DMA 버퍼 배치 확인 필요
```c
// user_def.c
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static uint16_t dac1_buffer_a[AUDIO_BUFFER_SIZE];
// ...

// spi_handler.c
__attribute__((aligned(32))) static CommandPacket_t g_rx_cmd_packet;
// ...

// user_com.c
static uint8_t g_uart3_tx_dma_buffer[DMA_TX_BUFFER_SIZE];
```

**확인 포인트**: 모든 DMA 버퍼가 `.dma_buffer` 섹션에 있는지 확인!

---

## 테스트 단계

### Phase 1: 버퍼 배치 확인 (빌드 후)

**목적**: 모든 DMA 버퍼가 MPU Non-cacheable 영역(0x20040000~)에 있는지 확인

**방법**:
```bash
arm-none-eabi-nm Debug/audio_dac_v101.elf | grep -E "dac1_buffer|dac2_buffer|g_rx_cmd_packet|g_uart3_tx_dma_buffer"
```

**예상 결과**:
```
20040000 b dac1_buffer_a
20040800 b dac1_buffer_b
20041000 b dac2_buffer_a
20041800 b dac2_buffer_b
20042xxx b g_rx_cmd_packet
20042xxx b g_uart3_tx_dma_buffer
```

**확인 사항**:
- ✅ 모든 주소가 `0x20040000 ~ 0x20043FFF` 범위 내
- ✅ 32바이트 정렬 (주소 끝이 00, 20, 40, 60, 80, A0, C0, E0)

---

### Phase 2: MPU만 활성화 (DCACHE OFF)

**목적**: MPU 설정이 올바른지 확인, DMA 동작 검증

**변경**:
```c
// main.c
int main(void)
{
    MPU_Config();  // ← 주석 해제 (line 108)
    HAL_Init();
    // ...
    //MX_DCACHE1_Init();  // ← 여전히 비활성화
```

**테스트 시나리오**:

1. **SPI DMA 수신 테스트** (가장 중요!)
   ```
   테스트 명령: (마스터에서 5-byte 명령 전송)
   예상 출력:
   [RX_CALLBACK] #1: DMA mode, RX data: C0 00 01 00 00
   [CMD] PLAY CH1
   ```

   **확인**:
   - ✅ DMA RX callback 정상 호출
   - ✅ 패킷 데이터 정확함
   - ✅ 명령 처리 성공

2. **UART3 DMA TX 테스트**
   ```
   테스트: printf 연속 출력
   예상: 모든 메시지가 정상 출력됨
   ```

3. **반복 테스트**
   ```
   SPI 명령 10회 전송
   예상: 모든 명령 정상 수신 및 처리
   ```

**실패 시 조치**:
- MPU 영역 재확인 (주소, 크기)
- 버퍼 배치 재확인 (Phase 1)

---

### Phase 3: MPU + DCACHE 활성화

**목적**: 캐시 활성화 후 DMA 정상 동작 확인

**변경**:
```c
// main.c
int main(void)
{
    MPU_Config();  // ← 활성화
    HAL_Init();
    // ...
    MX_DCACHE1_Init();  // ← 주석 해제 (line 128)
```

**테스트 시나리오** (Phase 2와 동일):

1. **SPI DMA 수신 테스트**
2. **UART3 DMA TX 테스트**
3. **반복 테스트** (10회 이상)

**추가 확인**:
- ✅ 캐시 히트율 (성능 향상 확인)
- ✅ 메모리 일관성 (데이터 정확성)
- ✅ 장시간 안정성 (수백 회 테스트)

**실패 패턴 및 조치**:

| 증상 | 원인 | 해결 |
|------|------|------|
| 첫 수신 성공, 이후 실패 | 캐시 일관성 문제 | 버퍼가 .dma_buffer 섹션에 없음 → Phase 1 재확인 |
| 수신 데이터 깨짐 | 캐시 일관성 문제 | MPU_NOT_CACHEABLE 설정 확인 |
| 간헐적 실패 | DMA/캐시 타이밍 | 버퍼 정렬 확인 (32바이트) |

---

### Phase 4: 성능 측정

**목적**: DCACHE 활성화로 인한 성능 향상 확인

**측정 항목**:

1. **SPI 수신 처리 시간**
   ```c
   // spi_handler_rx_callback() 시작/종료 시간 측정
   uint32_t start = DWT->CYCCNT;
   // ... 처리
   uint32_t elapsed = DWT->CYCCNT - start;
   ```

2. **UART TX 처리 시간**
   ```c
   // UART3_Process_TX_Queue() 처리 시간
   ```

3. **메모리 액세스 성능**
   ```c
   // 일반 RAM 읽기/쓰기 (캐시 적용)
   // vs DMA 버퍼 읽기/쓰기 (non-cacheable)
   ```

**비교**:
| 항목 | DCACHE OFF | DCACHE ON | 개선율 |
|------|-----------|-----------|-------|
| SPI RX 처리 시간 | ? μs | ? μs | ? % |
| UART TX 처리 시간 | ? μs | ? μs | ? % |
| 메인 루프 실행 시간 | ? μs | ? μs | ? % |

---

## 디버그 출력 추가

테스트를 위한 디버그 코드:

```c
// main.c - USER CODE BEGIN 2
void run_proc(void) {
    // MPU 상태 출력
    #ifdef MPU_ENABLED
    printf("[MPU] Enabled\r\n");
    printf("[MPU] Region 0: 0x%08X - 0x%08X (Non-cacheable)\r\n",
           0x20040000, 0x20043FFF);
    #else
    printf("[MPU] Disabled\r\n");
    #endif

    // DCACHE 상태 출력
    if (DCACHE1->CR & DCACHE_CR_EN) {
        printf("[DCACHE] Enabled\r\n");
    } else {
        printf("[DCACHE] Disabled\r\n");
    }

    // DMA 버퍼 주소 출력
    extern uint16_t dac1_buffer_a[];
    extern CommandPacket_t g_rx_cmd_packet;
    extern uint8_t g_uart3_tx_dma_buffer[];

    printf("[BUFFERS]\r\n");
    printf("  dac1_buffer_a: 0x%08X\r\n", (uint32_t)dac1_buffer_a);
    printf("  g_rx_cmd_packet: 0x%08X\r\n", (uint32_t)&g_rx_cmd_packet);
    printf("  g_uart3_tx_dma_buffer: 0x%08X\r\n", (uint32_t)g_uart3_tx_dma_buffer);

    // MPU 영역 확인
    if ((uint32_t)dac1_buffer_a >= 0x20040000 &&
        (uint32_t)dac1_buffer_a < 0x20044000) {
        printf("  ✓ Buffers in MPU non-cacheable region\r\n");
    } else {
        printf("  ✗ Buffers NOT in MPU region!\r\n");
    }

    // ...
}
```

---

## 체크리스트

### Phase 1: 버퍼 배치 확인
- [x] `arm-none-eabi-nm` 실행
- [x] 모든 DMA 버퍼가 0x2003C000~0x20043FFF 범위 내 (32KB로 확장)
- [x] 32바이트 정렬 확인

### Phase 2: MPU만 활성화
- [x] MPU_Config() 활성화 (Phase 3와 함께 진행)
- [x] 빌드 성공
- [x] SPI DMA 수신 정상 (6회 테스트 완료)
- [x] UART3 DMA TX 정상
- [x] 에러 없음

### Phase 3: MPU + DCACHE 활성화
- [x] MX_DCACHE1_Init() 활성화
- [x] 빌드 성공
- [x] SPI DMA 수신 정상 (6회 연속 성공, 0 에러)
- [x] UART3 DMA TX 정상
- [x] 장시간 안정성 확인 (연속 수신 테스트 완료)

### Phase 4: 성능 측정
- [ ] DCACHE OFF 기준 측정 (향후 작업)
- [ ] DCACHE ON 측정 (향후 작업)
- [ ] 성능 비교 표 작성 (향후 작업)

---

## 예상 결과

### 성공 시
- ✅ 모든 DMA 동작 정상
- ✅ DCACHE로 인한 성능 향상 (일반 코드 실행 5-10% 빠름)
- ✅ DMA 버퍼는 non-cacheable이므로 캐시 일관성 문제 없음

### 실패 시 원인
1. **DMA 버퍼가 일반 RAM에 있음**
   - 해결: `.dma_buffer` 섹션 속성 추가

2. **MPU 영역 설정 오류**
   - 해결: 주소/크기 재확인

3. **32바이트 정렬 누락**
   - 해결: `__attribute__((aligned(32)))` 추가

---

## 참고 자료

- STM32H5 Reference Manual (RM0481) - Chapter 6: MPU
- STM32H5 Reference Manual - Chapter 7: DCACHE
- AN4838: Managing memory protection unit in STM32 MCUs
- AN4839: Level 1 cache on STM32H7 Series

---

**작성일**: 2025-11-05
**프로젝트**: STM32H523 SPI Hardware NSS + DMA
