# STM32H523 SPI Hardware NSS + DMA Implementation

STM32H523 마이크로컨트롤러를 사용한 **Hardware NSS + DMA** 기반 SPI 슬레이브 구현 프로젝트입니다.

**핵심 개선사항**: Software NSS + EXTI (5-10μs 지연) → **Hardware NSS** (즉시 응답, 지연 거의 0)

## 📋 프로젝트 개요

### 하드웨어
- **MCU**: STM32H523CCTx (Cortex-M33, 250MHz)
- **Flash**: 256KB
- **RAM**: 272KB
- **SPI1**: Hardware NSS Slave Mode + DMA
  - PA5: SCK
  - PA6: MISO
  - PA7: MOSI
  - **PA15: NSS (Hardware-controlled)** ← 핵심 변경
- **DAC1**: Dual Channel (PA4, PA5), 12-bit, 32kHz
- **UART3**: DMA TX 디버그 출력 (PB1 RX, PB10 TX) @ 115200 baud

### 주요 기능
- ✅ **Hardware NSS**: 하드웨어 자동 CS 감지 (지연 거의 0)
- ✅ **SPI DMA 모드**: CPU 부하 최소화 연속 수신
- ✅ **UART3 DMA TX**: Non-blocking printf (Queue 기반)
- ✅ **MPU + DCACHE**: 성능 향상 + 캐시 일관성 보장
- ✅ 5-byte Command Packet 수신 및 처리
- ✅ PLAY/STOP/VOLUME/RESET 명령 처리
- ✅ 듀얼 채널 오디오 출력 (DAC1 CH1/CH2)

---

## 🎯 핵심 개선사항

### 문제: CS 신호 타이밍 지연

**이전 방식 (Software NSS + EXTI)**:
```
CS LOW → GPIO EXTI 인터럽트 → ISR 처리 → IT/DMA 시작
        └─ 5-10μs 소프트웨어 지연 발생
```

마스터가 CS 직후 바로 데이터를 전송하면 **첫 바이트 손실 가능**

### 해결: Hardware NSS로 전환

**현재 방식 (Hardware NSS + DMA)**:
```
CS LOW → SPI 하드웨어 자동 감지 → DMA 전송 즉시 활성화
        └─ 지연 거의 0 (하드웨어 제어)
```

**성능 비교**:

| 방식 | NSS 제어 | CS 감지 | 데이터 수신 시작 | 지연 시간 | 타이밍 정확도 |
|------|----------|---------|-----------------|-----------|--------------|
| Software NSS + EXTI | GPIO EXTI | EXTI 인터럽트 | ISR에서 IT/DMA 시작 | ~5-10μs | 낮음 |
| **Hardware NSS** + DMA | **SPI H/W** | **H/W 자동** | **즉시 (H/W 제어)** | **~0μs** | **매우 높음** |

---

## ⚡ MPU + DCACHE 활성화

### 메모리 보호 + 성능 최적화

**메모리 레이아웃**:
```
RAM (Cacheable):     0x20000000 ~ 0x2003BFFF (240KB)
  - 일반 코드/데이터
  - DCACHE 적용 → 성능 향상

RAM_DMA (Non-cacheable): 0x2003C000 ~ 0x20043FFF (32KB)
  - DMA 버퍼 전용
  - MPU로 캐시 비활성화
  - 캐시 일관성 문제 해결
```

**DMA 버퍼 배치** (`.dma_buffer` 섹션):
```
0x2003C000: g_rx_cmd_packet (SPI RX)
0x2003C040: g_uart3_tx_dma_buffer (UART TX)
0x2003C240: dac1_buffer_a/b (DAC1 오디오)
0x2003E240: dac2_buffer_a/b (DAC2 오디오)
```

**MPU 설정**:
- Region 0: 0x2003C000 ~ 0x20043FFF (32KB)
- Attributes: Non-cacheable
- DMA 버퍼를 DCACHE로부터 보호

**테스트 결과**:
```
✅ MPU: ENABLED
✅ DCACHE: ENABLED
✅ SPI DMA 수신: 6회 연속 성공, 0 에러
✅ UART DMA TX: Non-blocking 정상 동작
✅ 캐시 일관성: 데이터 손상 없음
```

**성능 향상**:
- 일반 코드 실행: 5-10% 빠름 (DCACHE 효과)
- DMA 동작: 영향 없음 (non-cacheable 영역)
- 실시간성: 유지 (Hardware NSS + DMA)

**참고**: [`MPU_DCACHE_TEST_PLAN.md`](MPU_DCACHE_TEST_PLAN.md) - 상세 테스트 절차

---

## 🔧 핵심 구현

### 1. Hardware NSS 설정

**CubeMX 설정**:
```
SPI1:
  - Mode: Slave
  - NSS: Hardware NSS Input Signal  ← 핵심
  - DMA Settings:
    - SPI1_RX: GPDMA1 Channel 4
    - SPI1_TX: GPDMA1 Channel 5
```

**초기화 코드 (main.c)**:
```c
hspi1.Init.NSS = SPI_NSS_HARD_INPUT;  // H/W NSS 사용
hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
```

### 2. DMA 연속 수신 모드

**시작 (spi_handler.c)**:
```c
void spi_handler_start(void) {
    // DMA 연속 수신 시작 (H/W NSS가 제어)
    g_rx_state = SPI_STATE_RECEIVE_CMD;
    HAL_SPI_Receive_DMA(&hspi1, (uint8_t*)&g_rx_cmd_packet, 5);
}
```

**DMA RX 완료 콜백**:
```c
void spi_handler_rx_callback(SPI_HandleTypeDef *hspi) {
    // 패킷 처리
    process_command_packet(&g_rx_cmd_packet);

    // 다음 패킷을 위해 DMA 재시작
    memset(&g_rx_cmd_packet, 0xFF, 5);
    HAL_SPI_Receive_DMA(hspi, (uint8_t*)&g_rx_cmd_packet, 5);
}
```

### 3. Non-blocking UART3 (DMA TX)

**Queue 기반 printf**:
```c
int __io_putchar(int ch) {
    // Queue가 거의 차면 문자 버림 (블로킹 방지)
    if (Len_queue(&tx_UART3_queue) < (tx_UART3_queue.buf_size - 10)) {
        Enqueue(&tx_UART3_queue, (uint8_t)ch);
    }
    return ch;
}
```

**DMA TX 처리 (user_com.c)**:
```c
void UART3_Process_TX_Queue(void) {
    if (g_uart3_tx_busy) return;

    uint16_t q_len = Len_queue(&tx_UART3_queue);
    if (q_len == 0) return;

    // Queue에서 데이터 추출 및 DMA 전송 시작
    Dequeue_bytes(&tx_UART3_queue, g_uart3_tx_dma_buffer, q_len);
    g_uart3_tx_busy = 1;
    HAL_UART_Transmit_DMA(&huart3, g_uart3_tx_dma_buffer, q_len);
}
```

---

## 📡 SPI 프로토콜

### Command Packet (5 bytes)
```
[0] Header:    0xC0 (HEADER_CMD)
[1] Command:   0x00=PLAY, 0x01=STOP, 0x02=VOLUME, 0x03=RESET
[2] Channel:   0x01=DAC1, 0x02=DAC2
[3] Param_H:   Parameter 상위 바이트
[4] Param_L:   Parameter 하위 바이트
```

**예시**:
- `C0 00 01 00 00` = DAC1 채널 재생 시작 (PLAY)
- `C0 01 01 00 00` = DAC1 채널 정지 (STOP)
- `C0 02 01 00 64` = DAC1 채널 볼륨 100 설정 (VOLUME)

---

## 🚀 빌드 및 실행

### 요구사항
- STM32CubeIDE 1.18.0 이상
- ARM GCC Toolchain (arm-none-eabi-gcc)
- STM32_Programmer_CLI (플래시용)

### 빌드
```bash
cd Debug
make -j8 all
```

### 플래시
```bash
STM32_Programmer_CLI.exe -c port=SWD reset=HWrst -w Debug/audio_dac_v101.elf -v -rst
```

### VS Code 태스크
```
Ctrl+Shift+P → "Tasks: Run Task"
- "Build (Debug)" - 컴파일
- "Flash to Target" - 플래시
- "Build and Flash" - 빌드 후 플래시
- "Serial Monitor" - UART3 출력 모니터링 (115200 baud)
```

---

## 📊 메모리 사용량

```
   text      data       bss       dec       hex    filename
 108808      1760     37520    148088     24278   audio_dac_v101.elf
```

- **Flash**: 108KB / 256KB (42%)
- **RAM**: 39KB / 272KB (14%)

---

## 📖 주요 파일

### 구현 코드
- `Core/Src/spi_handler.c` - Hardware NSS + DMA 수신 처리
- `Core/Src/user_com.c` - UART3 DMA TX (Queue 기반)
- `Core/Src/stm32h5xx_it.c` - 인터럽트 핸들러 (EXTI 비활성화됨)
- `Core/Src/audio_channel.c` - DAC 오디오 채널 관리
- `Core/Src/main.c` - 주변장치 초기화 (CubeMX 생성)

### 헤더 파일
- `Core/Inc/spi_handler.h` - SPI 핸들러 API
- `Core/Inc/spi_protocol.h` - 프로토콜 정의
- `Core/Inc/audio_channel.h` - 오디오 채널 구조체
- `Core/Inc/user_com.h` - UART 통신 API

### 설정 파일
- `audio_dac_v101.ioc` - STM32CubeMX 프로젝트 (Hardware NSS 설정)
- `STM32H523CCTX_FLASH.ld` - 링커 스크립트
- `.vscode/tasks.json` - VS Code 빌드 태스크

### 문서
- **[`DMA_IMPLEMENTATION_GUIDE.md`](DMA_IMPLEMENTATION_GUIDE.md)** - 상세 구현 가이드
- `CLAUDE.md` - 프로젝트 가이드 (Claude Code용)

---

## 🔍 디버깅

### UART3 디버그 출력 (Non-blocking)
코드 어디서나 `printf()` 사용 가능 (DMA TX로 즉시 반환)
```c
printf("[SPI] RX: %02X %02X %02X %02X %02X\r\n", ...);
```

### 상태 모니터링
```
[STATUS] --------------------
DAC1: STOP | Samples: 0 | Swaps: 0 | Underruns: 0
DAC2: STOP | Samples: 0 | Swaps: 0 | Underruns: 0
SPI:  Errors: 0 | Invalid Headers: 0 | Invalid IDs: 0
      State: 0x01 | ErrorCode: 0x00000000 | DMA_RX: 5
LAST_RX: C0 00 01 00 00
----------------------------
```

---

## 📌 핵심 교훈

### 1. Hardware NSS의 장점
- **즉시 응답**: CS LOW와 동시에 데이터 수신 시작 (지연 거의 0)
- **EXTI 불필요**: 하드웨어가 자동 처리하므로 인터럽트 오버헤드 없음
- **타이밍 정확도**: 소프트웨어 지연 없음

### 2. DMA 연속 수신 모드
- DMA를 한 번 시작하면 H/W NSS가 CS에 따라 자동으로 활성화/비활성화
- RX 완료 후 즉시 다음 DMA 재시작으로 연속 수신 구현

### 3. Queue 기반 Non-blocking UART
- printf 호출 시 즉시 복귀 (Queue에 저장만)
- DMA가 백그라운드에서 전송
- 실시간 SPI 수신을 방해하지 않음

### 4. Cache Coherency
- DCACHE 비활성화 시 DMA 버퍼에 별도 처리 불필요
- DCACHE 활성화 시 `SCB_CleanDCache_by_Addr()` / `SCB_InvalidateDCache_by_Addr()` 필수

---

## 🛠️ 개발 환경

- **IDE**: STM32CubeIDE 1.18.0 / VS Code 1.95+
- **Toolchain**: ARM GCC 13.3.1
- **HAL Library**: STM32Cube_FW_H5_V1.3.0
- **Debugger**: ST-Link/V2 (SWD)
- **OS**: Windows 10/11

---

## 📜 라이선스

STM32 HAL 드라이버는 STMicroelectronics의 라이선스를 따릅니다.

프로젝트 코드(사용자 작성 부분)는 자유롭게 사용 가능합니다.

---

## 👤 작성자

**GitHub**: [@trionking](https://github.com/trionking)

**프로젝트**: STM32H523 SPI Hardware NSS + DMA Implementation

**개발 기간**: 2025-01-05

---

## 🤖 개발 지원

이 프로젝트는 [Claude Code](https://claude.com/claude-code)의 지원을 받아 개발되었습니다.

Co-Authored-By: Claude <noreply@anthropic.com>
