# CLAUDE.md

이 파일은 Claude Code (claude.ai/code)가 이 저장소의 코드를 작업할 때 참고하는 가이드입니다.

## 프로젝트 개요

STM32H523CCTx 마이크로컨트롤러(Cortex-M33, 250MHz)를 타겟으로 하는 STM32H5 임베디드 오디오 DAC 프로젝트입니다. 오디오 출력을 위한 듀얼 채널 12비트 DAC, SPI 슬레이브 인터페이스, 듀얼 UART 채널, DMA 지원을 포함합니다.

## 빌드 시스템

**VS Code에서 빌드:**
```bash
# VS Code 태스크 사용 (Ctrl+Shift+P → "Tasks: Run Task")
- "Build (Debug)" - 프로젝트 컴파일 (make -j8)
- "Clean (Debug)" - 빌드 산출물 정리
- "Flash to Target" - ST-Link를 통해 프로그래밍
- "Build and Flash" - 빌드 후 프로그래밍
- "Serial Monitor" - COM9에서 UART 출력 모니터링 @ 115200 baud
```

**수동 빌드 명령:**
```bash
cd Debug
make -j8 all              # 프로젝트 빌드
make clean                # 빌드 산출물 정리
arm-none-eabi-size audio_dac_v100.elf  # 메모리 사용량 확인
```

**타겟에 플래시:**
```bash
STM32_Programmer_CLI.exe -c port=SWD reset=HWrst -w Debug/audio_dac_v100.elf -v -rst
```

**툴체인:** ARM GCC (arm-none-eabi-gcc), STM32CubeIDE에서 관리
- 컴파일러 플래그: `-mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb`
- 링커 스크립트: `STM32H523CCTX_FLASH.ld` (256KB Flash @ 0x08000000, 272KB RAM @ 0x20000000)
- 스택/힙: 각 8KB

## 아키텍처

### 하드웨어 구성 (STM32H523CCTx)

**클럭 시스템:**
- HSE: 25MHz 외부 오실레이터 (바이패스 모드)
- 시스템 클럭: 250MHz (PLL에서 생성)
- 모든 APB 버스: 250MHz (프리스케일러 없음)
- DAC 클럭: LSI @ 32kHz (저전력 오디오 클럭)

**주변장치:**
- **DAC1:** 듀얼 채널 오디오 출력 (PA4/PA5), 12비트, LSI 클럭
- **SPI1:** 슬레이브 모드 (125MHz 클럭), 8비트 전송, NSS/RDY 하드웨어 제어 (PA15/PA8)
- **USART3:** 디버그/printf 출력 (PB1 RX, PB10 TX) @ 115200 baud
- **USART1:** 반이중 단선 (PB14) @ 115200 baud
- **TIM7:** 기본 타이머 (1ms 틱)
- **GPDMA1:** SPI/UART/DAC DMA 전송을 위한 6개 채널
- **GPIO:** LED 제어 PB12 (OT_LD_REV), PB13 (OT_LD_SYS)
- **ICACHE/DCACHE:** 명령어 및 데이터 캐시 활성화

### 코드 구조

**실행 흐름:**
```
Reset_Handler (startup_stm32h523cctx.s)
  → SystemInit() (system_stm32h5xx.c)
    → main() (main.c)
      → HAL_Init()
      → SystemClock_Config() - 250MHz PLL 설정
      → MX_GPIO_Init()
      → MX_GPDMA1_Init() - 6개 DMA 채널
      → MX_DAC1_Init()
      → MX_DCACHE1_Init() / MX_ICACHE_Init()
      → MX_SPI1_Init() / MX_USART3_UART_Init() / MX_USART1_UART_Init()
      → MX_TIM7_Init()
      → run_proc() (user_def.c) - 사용자 애플리케이션 진입점
```

**주요 파일:**

- `Core/Src/main.c` - HAL 초기화, 주변장치 설정, 클럭 구성
- `Core/Src/user_def.c` - 애플리케이션 로직 진입점 (`run_proc()`), UART3를 통한 printf 리다이렉션
- `Core/Src/stm32h5xx_hal_msp.c` - 주변장치 초기화/해제를 위한 HAL MSP 콜백
- `Core/Src/stm32h5xx_it.c` - 인터럽트 서비스 루틴 (DMA, UART, SPI, TIM)
- `Core/Src/syscalls.c` - printf 지원을 위한 Newlib syscall
- `Core/Inc/main.h` - GPIO 핀 정의, 주변장치 핸들 선언
- `Core/Inc/user_def.h` - 사용자 함수 프로토타입

**HAL 주변장치 핸들 (main.c에서 extern):**
```c
DAC_HandleTypeDef hdac1;
SPI_HandleTypeDef hspi1;
UART_HandleTypeDef huart1, huart3;
TIM_HandleTypeDef htim7;
DMA_HandleTypeDef handle_GPDMA1_Channel[1-6];
```

### 애플리케이션 로직 위치

사용자 코드는 `Core/Src/user_def.c`에 작성:
- `init_proc()` - HAL 설정 후 1회 초기화
- `run_proc()` - 메인 애플리케이션 루프 (main.c에서 호출됨)

`main.c`는 STM32CubeMX에서 재생성되므로 직접 수정하지 마세요. 꼭 필요한 경우 `USER CODE BEGIN/END` 섹션을 사용하되, `user_def.c`에 로직을 유지하는 것을 권장합니다.

### Printf 디버그 출력

Printf는 USART3 (115200 baud)로 자동 리다이렉션됩니다:
- `__io_putchar()` in user_def.c - 단일 문자 전송
- `_write()` syscall 구현 (stdout/stderr용)

VS Code "Serial Monitor" 태스크 또는 COM 포트의 외부 터미널로 출력을 모니터링하세요.

### 주변장치 사용 참고사항

**DAC (오디오 출력):**
- 초기화되었지만 현재 코드에서 활성화되지 않음
- 오디오 출력: `HAL_DAC_SetValue()` 사용 또는 `HAL_DAC_Start_DMA()`로 DMA 사용
- LSI 클럭 (32kHz)은 낮은 샘플레이트 오디오 애플리케이션을 의미

**SPI (슬레이브 인터페이스):**
- 외부 마스터의 주변장치로 동작
- 데이터 전송: `HAL_SPI_Receive_IT()` 또는 `HAL_SPI_TransmitReceive_DMA()` 사용
- RDY 신호 (PA8)로 흐름 제어

**DMA 채널 할당:**
- 채널 0-1: USART3 RX/TX
- 채널 2-3: USART1 RX/TX
- 채널 4-5: SPI1 RX/TX
- 채널 6: DAC 또는 다른 용도로 사용 가능

### 메모리 레이아웃

링커 스크립트 `STM32H523CCTX_FLASH.ld`에서:

| 영역 | 주소 | 크기 | 용도 |
|--------|---------|------|-------|
| Flash | 0x08000000 | 256KB | 프로그램 코드/상수 |
| RAM | 0x20000000 | 272KB | 변수, 스택, 힙 |

현재 사용량: ~45KB Flash (18%), ~104KB RAM (38%)

## 하드웨어 구성 변경

주변장치, 클럭 또는 핀을 수정하려면:
1. STM32CubeMX에서 `audio_dac_v100.ioc` 열기
2. CubeMX GUI에서 변경
3. 코드 생성 (main.c를 덮어쓰지만 USER CODE 섹션은 보존)
4. **⚠️ 중요: 코드 재생성 후 `STM32H5_CODEGEN_BUGS.md` 문서를 참고해서 알려진 버그들을 패치하세요!**
5. 프로젝트 리빌드

### STM32H5 CubeMX 코드 생성 버그

CubeMX로 코드를 재생성할 때 발생하는 알려진 버그들이 있습니다:
- **GPDMA2 DAC DMA 초기화 불일치** (CRITICAL): Channel 0가 Direct Init으로 생성되어 Circular 모드 작동 안 함
- **DMA Node Type 오류**: 2D_NODE 대신 LINEAR_NODE 필요
- **DCACHE 일관성**: DMA 전/후 캐시 동기화 필요

**📖 자세한 내용은 `STM32H5_CODEGEN_BUGS.md` 문서를 참조하세요.**

CubeMX로 코드를 재생성한 후에는:
1. `STM32H5_CODEGEN_BUGS.md` 문서 열기
2. "체크리스트: 코드 재생성 후 확인 사항" 섹션 확인
3. 필요한 패치 적용
4. 빌드 및 테스트

## 일반적인 패턴

**GPIO 토글:**
```c
HAL_GPIO_TogglePin(OT_LD_SYS_GPIO_Port, OT_LD_SYS_Pin);
```

**DAC 출력:**
```c
HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, value);
HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
```

**SPI 수신 (슬레이브):**
```c
HAL_SPI_Receive_IT(&hspi1, rxBuffer, length);
// HAL_SPI_RxCpltCallback()에서 처리
```

**타이밍 (500ms 예제):**
```c
uint32_t tick_start = HAL_GetTick();
while (1) {
    if (HAL_GetTick() - tick_start > 500) {
        tick_start = HAL_GetTick();
        // 500ms마다 수행할 작업
    }
}
```

## 디버깅

**시리얼 디버그 출력:**
- 코드 어디서나 `printf()` 사용 (USART3로 출력)
- VS Code의 "Serial Monitor" 태스크로 모니터링

**ST-Link 디버거:**
- STM32CubeIDE에서 전체 디버그 지원 (브레이크포인트, 워치, 라이브 표현식)
- SWD 핀: PA13 (SWDIO), PA14 (SWCLK)

**메모리 분석:**
```bash
arm-none-eabi-size Debug/audio_dac_v100.elf
arm-none-eabi-nm Debug/audio_dac_v100.elf | grep <symbol>
arm-none-eabi-objdump -d Debug/audio_dac_v100.elf > disassembly.txt
```

## 중요한 제약사항

- **HAL 사용:** 항상 HAL 함수를 사용하고 직접 레지스터 접근 금지 (CubeMX 호환성)
- **인터럽트 우선순위:** GPDMA 및 TIM7은 우선순위 2, SPI/UART는 우선순위 1
- **스택 오버플로:** 8KB 스택 - 큰 로컬 배열 피하고 힙 또는 정적 할당 사용
- **실시간 제약:** 250MHz는 명령어당 4ns (오디오 샘플레이트 고려 필요)
- **DMA 일관성:** 데이터 캐시 활성화됨 - DMA TX 전에 `SCB_CleanDCache_by_Addr()` 사용, DMA RX 후에 `SCB_InvalidateDCache_by_Addr()` 사용
- 답변은 항상 한글로 해줘.
- 빌드는 직접해 줘. 왜냐하면 문법 오류를 바로 수정하게 하고 싶어.

## ⚠️ STM32H5 CubeMX 버그 패치 프로토콜

**중요**: CubeMX로 코드를 재생성할 때마다 다음 절차를 따르세요:

### 코드 재생성 전
1. 현재 작동하는 코드의 백업 생성
2. `user_def.c`, `user_def.h`의 사용자 코드 확인
3. `main.c`의 `USER CODE BEGIN/END` 섹션 확인

### 코드 재생성 후 (필수!)
1. **즉시** `STM32H5_CODEGEN_BUGS.md` 문서 열기
2. "버그 #1: GPDMA2 DAC DMA 초기화 방식 불일치" 섹션의 패치 적용
   - CubeMX 설정 변경 (권장) 또는
   - 수동 패치 적용 (`stm32h5xx_hal_msp.c`)
3. 체크리스트의 모든 항목 확인
4. 빌드 실행하여 컴파일 오류 확인
5. 기능 테스트 (특히 DAC DMA 동작)

### Claude Code 작업 시
- CubeMX 코드 재생성이 필요한 경우, **반드시** 사용자에게 재생성 후 `STM32H5_CODEGEN_BUGS.md`의 패치를 적용하도록 안내
- 버그 패치가 적용되지 않은 코드는 정상 작동하지 않을 수 있음을 명확히 경고
- GPDMA2 관련 코드 수정 시 항상 LinkedList 방식 사용 여부 확인

### 긴급 참조
가장 중요한 버그: **GPDMA2 Channel 0이 Direct Init 방식으로 생성됨**
→ 해결: CubeMX에서 Channel 0을 "Linked-List Queue" 모드로 변경
- 플래쉬는 CUBEIDE 에서 직접 할거야.