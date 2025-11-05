# STM32H523 SPI Slave 수신 (IT 모드)

STM32H523 마이크로컨트롤러를 사용한 SPI 슬레이브 오디오 DAC 프로젝트입니다.

Software NSS + EXTI + IT(Interrupt) 모드로 SPI 슬레이브 통신을 구현하여, 1ms CS-SCK 지연이 있는 마스터와의 안정적인 통신을 달성했습니다.

## 📋 프로젝트 개요

### 하드웨어
- **MCU**: STM32H523CCTx (Cortex-M33, 250MHz)
- **Flash**: 256KB
- **RAM**: 272KB (256KB 캐시 가능 + 16KB DMA 전용)
- **SPI1**: Full-Duplex Slave
  - PA5: SCK
  - PA6: MISO
  - PA7: MOSI
  - PA15: CS (GPIO_EXTI15)
- **DAC1**: Dual Channel (PA4, PA5), 12-bit, 32kHz
- **UART3**: Debug 출력 (PB1 RX, PB10 TX) @ 115200 baud

### 주요 기능
- ✅ SPI 슬레이브 모드로 5-byte 커맨드 패킷 수신
- ✅ Software NSS + EXTI falling edge 감지
- ✅ IT(Interrupt) 모드 수신 (`HAL_SPI_Receive_IT`)
- ✅ EXTI pending flag 수동 처리 (STM32H5 특이사항)
- ✅ 1ms CS-SCK 지연 지원
- ✅ PLAY/STOP/VOLUME/RESET 명령 처리
- ✅ 듀얼 채널 오디오 출력 (DAC1 CH1/CH2)

---

## 🎯 문제 해결 과정

### ❌ Hardware NSS 모드 실패
초기에는 Hardware NSS + DMA 모드로 구현을 시도했으나 실패했습니다.

**문제 상황:**
- DMA_RX = 0 (수신 없음)
- Abort Error (0xC0) 발생
- SPI State = 0x05 (BUSY)에서 멈춤

**근본 원인:**
마스터의 타이밍이 Hardware NSS 요구사항과 불일치
```
CS  : ____╲___________[1ms 지연]___________╱____
SCK : ________________________||||||||||||____
                              ↑ 50us (5 bytes)
```

Hardware NSS 모드는 **NSS LOW 직후 즉시 SCK 시작**을 요구하지만, 마스터는 1ms 지연 후 SCK를 시작했습니다.

**상세 분석**: [`Hardware_NSS_실패_분석_2025-01-04_22시.md`](Hardware_NSS_실패_분석_2025-01-04_22시.md)

---

### ✅ Software NSS + IT 모드 성공

Hardware NSS의 타이밍 제약을 우회하기 위해 **Software NSS + EXTI + IT 모드**로 전환했습니다.

**핵심 전략:**
1. **Software NSS**: 하드웨어 타이밍 제약 제거
2. **EXTI**: PA15를 GPIO_EXTI15로 설정, CS falling edge 감지
3. **IT 모드**: `HAL_SPI_Receive_IT()` 사용 (타이밍 유연성)
4. **수동 EXTI 처리**: STM32H5에서 `EXTI->FPR1` 직접 클리어

**테스트 결과:**
```
[RX_CALLBACK] #1-8: IT mode, RX data: C0 00 01 00 00
EXTI: Total: 5 | CS_LOW: 5 | DMA_RX: 5
SPI:  Errors: 0 | Invalid Headers: 0
DAC1: PLAY (명령 정상 처리)
```

**상세 구현**: [`IT모드_SPI_수신_성공_2025-01-05_00시.md`](IT모드_SPI_수신_성공_2025-01-05_00시.md)

---

## 🔧 핵심 구현 사항

### 1. SPI 초기화 (Software NSS)
```c
// main.c
hspi1.Init.NSS = SPI_NSS_SOFT;  // Software NSS 사용
hspi1.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;  // 1 byte threshold
```

### 2. EXTI 설정 (CS falling edge)
```c
// main.c - GPIO 초기화
GPIO_InitStruct.Pin = SPI1_EXT_NSS_Pin;  // PA15
GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;  // Falling edge
GPIO_InitStruct.Pull = GPIO_NOPULL;
HAL_GPIO_Init(SPI1_EXT_NSS_GPIO_Port, &GPIO_InitStruct);

HAL_NVIC_SetPriority(EXTI15_IRQn, 1, 0);
HAL_NVIC_EnableIRQ(EXTI15_IRQn);
```

### 3. EXTI 인터럽트 핸들러 (STM32H5 특이사항)
```c
// stm32h5xx_it.c
void EXTI15_IRQHandler(void)
{
    // STM32H5: EXTI pending flag 수동 처리
    if (EXTI->FPR1 & (1U << 15))
    {
        EXTI->FPR1 = (1U << 15);  // Clear flag
        HAL_GPIO_EXTI_Callback(SPI1_EXT_NSS_Pin);
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == SPI1_EXT_NSS_Pin)
    {
        GPIO_PinState pin_state = HAL_GPIO_ReadPin(SPI1_EXT_NSS_GPIO_Port, SPI1_EXT_NSS_Pin);

        if (pin_state == GPIO_PIN_RESET)  // CS LOW
        {
            spi_handler_cs_falling();  // SPI IT 수신 시작
        }
    }
}
```

### 4. SPI IT 수신
```c
// spi_handler.c
void spi_handler_cs_falling(void)
{
    if (g_hspi->State != HAL_SPI_STATE_READY)
        return;  // 충돌 방지

    memset(&g_rx_cmd_packet, 0xFF, sizeof(CommandPacket_t));

    g_rx_state = SPI_STATE_RECEIVE_CMD;
    HAL_SPI_Receive_IT(g_hspi, (uint8_t*)&g_rx_cmd_packet, 5);  // 5 bytes
}

void spi_handler_rx_callback(SPI_HandleTypeDef *hspi)
{
    if (g_rx_state == SPI_STATE_RECEIVE_CMD)
    {
        if (g_rx_cmd_packet.header == HEADER_CMD)  // 0xC0
        {
            process_command_packet(&g_rx_cmd_packet);
        }
        g_rx_state = SPI_STATE_WAIT_HEADER;
    }
}
```

---

## 📡 SPI 프로토콜 (v2.0)

### Command Packet (5 bytes)
```
[0] Header:    0xC0 (HEADER_CMD)
[1] Command:   0x00=PLAY, 0x01=STOP, 0x02=VOLUME, 0x03=RESET
[2] Channel:   0x01=DAC1, 0x02=DAC2
[3] Param_H:   Parameter 상위 바이트
[4] Param_L:   Parameter 하위 바이트
```

**예시:**
- `C0 00 01 00 00` = DAC1 채널 재생 시작 (PLAY)
- `C0 01 01 00 00` = DAC1 채널 정지 (STOP)
- `C0 02 01 00 64` = DAC1 채널 볼륨 100 설정 (VOLUME)

### 타이밍
- CS-SCK 지연: 1ms (허용됨)
- SCK 주파수: ~1MHz
- 5 bytes 전송 시간: 50us (40 clocks)

---

## 🚀 빌드 및 플래시

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
STM32_Programmer_CLI.exe -c port=SWD reset=HWrst -w Debug/audio_dac_v100.elf -v -rst
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
 108952      1760     37520    148232     24308   audio_dac_v100.elf
```

- **Flash**: 108KB / 256KB (42%)
- **RAM**: 39KB / 272KB (14%)

---

## 📖 주요 파일

### 소스코드
- `Core/Src/spi_handler.c` - SPI IT 모드 수신 처리
- `Core/Src/stm32h5xx_it.c` - EXTI/SPI 인터럽트 핸들러
- `Core/Src/audio_channel.c` - DAC 오디오 채널 관리
- `Core/Src/main.c` - 주변장치 초기화 (CubeMX 생성)
- `Core/Src/user_def.c` - 사용자 애플리케이션 로직

### 헤더파일
- `Core/Inc/spi_handler.h` - SPI 핸들러 API
- `Core/Inc/spi_protocol.h` - 프로토콜 정의
- `Core/Inc/audio_channel.h` - 오디오 채널 구조체

### 설정파일
- `audio_dac_v100.ioc` - STM32CubeMX 프로젝트
- `STM32H523CCTX_FLASH.ld` - 링커 스크립트
- `.vscode/tasks.json` - VS Code 빌드 태스크

### 문서
- `IT모드_SPI_수신_성공_2025-01-05_00시.md` - 성공 사례 상세 분석
- `Hardware_NSS_실패_분석_2025-01-04_22시.md` - 실패 분석 및 디버깅 과정
- `CLAUDE.md` - 프로젝트 가이드 (Claude Code용)

---

## 🔍 디버깅

### UART3 디버그 출력
코드 어디서나 `printf()` 사용 가능 (USART3로 자동 리다이렉션)
```c
printf("[DEBUG] SPI RX: %02X %02X %02X %02X %02X\r\n", ...);
```

### 상태 모니터링
```
[STATUS] --------------------
DAC1: PLAY | Samples: 0 | Swaps: 0 | Underruns: 0
DAC2: STOP | Samples: 0 | Swaps: 0 | Underruns: 0
SPI:  Errors: 0 | Invalid Headers: 0
      State: 0x01 | ErrorCode: 0x00000000
EXTI: Total: 5 | CS_LOW: 5 | CS_HIGH: 0 | DMA_RX: 5
DEBUG: Callback_Count: 5 | Last_Pin: 0x8000
LAST_RX: C0 00 01 00 00
----------------------------
```

### 오실로스코프 분석
CS(PA15), SCK(PA5), MOSI(PA7) 신호를 측정하여 타이밍 검증

---

## 📌 핵심 교훈

### 1. Hardware NSS의 엄격한 타이밍 요구사항
- Hardware NSS는 **NSS LOW 직후 즉시 SCK 시작**을 요구
- 1ms 이상 지연 시 Abort Error 발생
- 마스터 타이밍을 제어할 수 없다면 Software NSS 사용 필수

### 2. STM32H5 EXTI 특이사항
- `HAL_GPIO_EXTI_IRQHandler()` 사용 시 callback이 호출되지 않음
- `EXTI->FPR1/RPR1` 레지스터를 직접 확인 및 클리어 필요
- Forward declaration 필수: `void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);`

### 3. IT vs DMA 선택
- **IT 모드**: 타이밍 유연성, 간단한 구조 (5 byte 패킷에 적합)
- **DMA 모드**: CPU 부하 감소 (대용량 연속 데이터에 적합)

### 4. 오실로스코프의 중요성
- 타이밍 문제는 오실로스코프 없이 진단 불가능
- CS, SCK, MOSI 신호를 동시에 측정하여 프로토콜 검증

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

**프로젝트**: STM32H523 Audio DAC v1.00

**개발 기간**: 2025-01-04 ~ 2025-01-05

---

## 🤖 개발 지원

이 프로젝트는 [Claude Code](https://claude.com/claude-code)의 지원을 받아 개발되었습니다.

Co-Authored-By: Claude <noreply@anthropic.com>
