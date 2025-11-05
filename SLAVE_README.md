# STM32H523 Slave 펌웨어 - Slave ID 0 구현

## 📋 개요

STM32H523 Slave MCU 펌웨어 v1.0이 구현되었습니다.
Master STM32H723로부터 SPI를 통해 오디오 데이터를 수신하고, 2채널 DAC로 아날로그 출력을 생성합니다.

**현재 구현**: Slave ID 0 (채널 0, 1)

## ✅ 구현 완료 사항

### 1. SPI 프로토콜 (Core/Inc/spi_protocol.h, Core/Src/spi_handler.c)
- ✅ 명령 패킷 (6 bytes, 0xC0 헤더)
- ✅ 데이터 패킷 (5 bytes 헤더 + 최대 2048 샘플)
- ✅ 상태 머신 기반 패킷 수신
- ✅ RDY 핀 제어 (PA8)
- ✅ 에러 처리 및 통계

**지원 명령**:
- `CMD_PLAY (0x01)`: 재생 시작
- `CMD_STOP (0x02)`: 재생 정지
- `CMD_VOLUME (0x03)`: 볼륨 설정 (0-100)
- `CMD_RESET (0xFF)`: 채널 리셋

### 2. 오디오 시스템 (Core/Inc/audio_channel.h, Core/Src/audio_channel.c)
- ✅ 이중 버퍼 시스템 (각 2048 샘플 = 64ms @ 32kHz)
- ✅ DAC1, DAC2 독립 채널 관리
- ✅ 16-bit → 12-bit 변환
- ✅ 볼륨 스케일링
- ✅ 버퍼 언더런 감지

**메모리 사용**:
- DAC1 버퍼: 8KB (2048 * 2 * 2 bytes)
- DAC2 버퍼: 8KB
- SPI 수신 버퍼: 4101 bytes
- 총 메모리: ~16KB

### 3. DMA 및 타이머
- ✅ TIM7: 32kHz DAC 트리거 (명세의 TIM6 대신 사용)
- ✅ DAC DMA: 순환 모드로 연속 출력
- ✅ SPI DMA: 인터럽트 모드 수신
- ✅ 버퍼 스왑 콜백

### 4. 상태 모니터링
- ✅ 5초마다 통계 출력
  - 재생 상태 (PLAY/STOP)
  - 수신 샘플 수
  - 버퍼 스왑 횟수
  - 언더런 카운트
  - SPI 에러 통계

## 📁 파일 구조

```
Core/
├── Inc/
│   ├── spi_protocol.h       ← 프로토콜 정의 (패킷, 명령 코드)
│   ├── audio_channel.h      ← 오디오 채널 관리 (이중 버퍼)
│   ├── spi_handler.h        ← SPI 수신 및 처리
│   ├── user_def.h           ← 메인 애플리케이션
│   └── main.h               ← HAL 설정 (CubeMX 생성)
├── Src/
│   ├── spi_protocol.c       (헤더 only 파일)
│   ├── audio_channel.c      ← 버퍼 관리 구현
│   ├── spi_handler.c        ← 상태 머신 및 패킷 처리
│   ├── user_def.c           ← 메인 루프 및 초기화
│   ├── stm32h5xx_it.c       ← 인터럽트 콜백 (HAL)
│   └── main.c               ← HAL 초기화 (CubeMX 생성)
└── ...
```

## 🔧 빌드 및 플래시

### VS Code Tasks
```bash
Ctrl+Shift+P → "Tasks: Run Task"
- "Build (Debug)"         : 프로젝트 컴파일
- "Flash to Target"       : ST-Link로 플래시
- "Build and Flash"       : 빌드 후 플래시
- "Serial Monitor"        : UART3 모니터링 (115200 baud)
```

### 수동 빌드
```bash
cd Debug
make -j8 all
arm-none-eabi-size audio_dac_v100.elf
```

### 플래시
```bash
STM32_Programmer_CLI.exe -c port=SWD reset=HWrst -w Debug/audio_dac_v100.elf -v -rst
```

## 🚀 사용 방법

### 1. 부팅 시 메뉴 표시
펌웨어가 시작되면 UART3 (115200 baud)로 메뉴가 표시됩니다:

```
========================================
  STM32H523 Slave MCU Firmware v1.0
  Audio Streaming via SPI
========================================
Slave ID: 0
System Clock: 250 MHz
Buffer Size: 2048 samples x 2 buffers
Total RAM: ~16 KB
========================================

========================================
  STM32H523 Slave - Test Menu
========================================
0. Run Slave Mode (Main Application)
1. LED Blink Test
2. DAC Sine Wave Test (1kHz, 500Hz)
3. RDY Pin Toggle Test
4. Legacy GPIO Test
5. Legacy DAC Test (10kHz, 5kHz)
----------------------------------------
Select test (0-5):
```

### 2. Slave 모드 실행
**`0`을 입력하여 메인 애플리케이션 시작**:

```
========================================
  STM32H523 Slave MCU - Audio Streaming
========================================
Slave ID: 0
Channels: DAC1 (CH0), DAC2 (CH1)
Sample Rate: 32kHz
Buffer Size: 2048 samples (64ms)
========================================

[INIT] Audio channels initialized
[INIT] SPI handler initialized
[INIT] SPI reception started

** Slave ready - waiting for Master commands **
```

### 3. 상태 모니터링
5초마다 자동으로 통계가 출력됩니다:

```
[STATUS] --------------------
DAC1: PLAY | Samples: 123456 | Swaps: 60 | Underruns: 0
DAC2: STOP | Samples: 0 | Swaps: 0 | Underruns: 0
SPI:  Errors: 0 | Invalid Headers: 0 | Invalid IDs: 0
----------------------------
```

### 4. 자동 시작 모드 (배포용)
개발이 완료되면 `user_def.c`의 `run_proc()`를 수정하여 자동 시작:

```c
void run_proc(void)
{
    init_proc();

    // 메뉴 없이 바로 slave 모드 실행
    run_slave_mode();
}
```

## 📊 Slave ID 변경 방법

### 현재 (Slave ID 0)
`Core/Inc/spi_protocol.h`:
```c
#define MY_SLAVE_ID  0
```

### Slave ID 1, 2로 변경
각 보드별로 다른 값으로 설정 후 컴파일:

**Slave 1**:
```c
#define MY_SLAVE_ID  1  // 채널 2, 3
```

**Slave 2**:
```c
#define MY_SLAVE_ID  2  // 채널 4, 5
```

## 🔌 하드웨어 연결

### SPI 인터페이스
| 신호 | Slave 핀 | Master 핀 (예상) | 설명 |
|------|---------|-----------------|------|
| SCK | PA5 (SPI1_SCK) | PA5 (SPI1_SCK) | 클럭 (10MHz) |
| MOSI | PB5 (SPI1_MOSI) | PB5 (SPI1_MOSI) | 데이터 (M→S) |
| MISO | PB4 (SPI1_MISO) | PB4 (SPI1_MISO) | 데이터 (S→M, 미사용) |
| CS/NSS | PA15 (SPI1_NSS) | PA4 (Slave 0) | 칩 선택 |
| RDY | PA8 (GPIO_OUT) | PF13 (Slave 0) | 준비 신호 |

### DAC 출력
| 신호 | Slave 핀 | 설명 |
|------|---------|------|
| DAC1_OUT | PA4 | 채널 0 출력 (0~3.3V) |
| DAC2_OUT | PA5 | 채널 1 출력 (0~3.3V) |

### LED
| 신호 | Slave 핀 | 용도 |
|------|---------|------|
| LED_SYS | PB13 | 동작 표시 (0.5초 토글) |
| LED_REV | PB12 | 예비 |

### UART 디버그
| 신호 | Slave 핀 | 설정 |
|------|---------|------|
| USART3_TX | PB10 | 115200 baud, 8N1 |
| USART3_RX | PB1 | (입력용) |

## ⚠️ 주의사항

### 1. CubeMX 재생성 시
`main.c`는 CubeMX가 덮어쓰므로, **절대 수정하지 말 것**!
- 사용자 코드는 `user_def.c`에 작성
- 콜백은 `stm32h5xx_it.c`의 `USER CODE BEGIN/END` 섹션 사용

### 2. 타이머 설정
- 명세: TIM6 사용 권장
- 현재: **TIM7 사용** (이미 초기화됨)
- 필요 시 CubeMX에서 TIM7 → TIM6로 변경 가능

### 3. DMA 채널
현재 DMA 할당 확인 필요 (CubeMX에서):
- Channel 0-1: USART3 RX/TX
- Channel 2-3: USART1 RX/TX
- Channel 4-5: SPI1 RX/TX (확인 필요)
- Channel 6: 예비 (DAC용?)

### 4. 핀 충돌
- **DAC1_OUT (PA4)와 SPI1_NSS (PA15) 확인 필요**
- DAC 핀은 STM32H523 데이터시트 참조

## 🧪 테스트 절차

### Phase 1: 하드웨어 검증 (현재 단계)
1. **LED 블링크** (`1`): GPIO 동작 확인
2. **DAC 정현파** (`2`): DAC 출력 확인 (오실로스코프)
3. **RDY 핀 토글** (`3`): RDY 신호 확인

### Phase 2: SPI 통신 테스트 (다음 단계)
1. Master 연결 없이 루프백 테스트
2. Master 연결하여 명령 패킷 수신 확인
3. 데이터 패킷 수신 및 DAC 출력 확인

### Phase 3: 통합 테스트
1. 단일 채널 연속 재생 (10초 이상)
2. 2채널 동시 재생
3. 버퍼 언더런 테스트 (지연 발생 시)

## 📝 다음 단계 (Master 팀과 협의 필요)

### 1. 핀 연결 최종 확인
- [ ] CS 핀 번호 (Master의 PA4/PF11/PF12)
- [ ] RDY 핀 번호 (Master의 PF13/14/15)
- [ ] 실제 SPI 클럭 속도 (명세: 10MHz)

### 2. 통신 프로토콜 테스트
- [ ] Master에서 테스트용 명령 패킷 전송
- [ ] PLAY 명령 수신 확인
- [ ] 더미 데이터 패킷 전송 (정현파)
- [ ] DAC 출력 파형 확인

### 3. 타이밍 검증
- [ ] 2048 샘플 @ 32kHz = 64ms 검증
- [ ] 버퍼 스왑 타이밍 측정
- [ ] RDY 핀 응답 시간 측정

### 4. 에러 처리 테스트
- [ ] 버퍼 언더런 발생 시 동작 확인
- [ ] SPI 에러 복구 테스트
- [ ] Master 재시작 시 Slave 동작

### 5. 멀티 Slave 테스트 (Phase 3)
- [ ] Slave 0, 1, 2 펌웨어 개별 플래시
- [ ] Master에서 3개 Slave 동시 제어
- [ ] 6채널 동시 재생 테스트

## 🐛 디버깅 팁

### 1. SPI 통신 문제
- RDY 핀 상태 확인 (HIGH → 수신 가능)
- 로직 분석기로 SPI 신호 확인
- UART 로그에서 헤더 에러 확인

### 2. 오디오 출력 문제
- DAC 출력에 RC 필터 추가 (16kHz 차단)
- GND 연결 확인
- 버퍼 언더런 카운트 확인

### 3. 버퍼 언더런 발생
- Master의 전송 주기 확인 (< 64ms)
- RDY 핀 응답 속도 확인
- DMA 우선순위 확인

### 4. 통계 출력으로 디버깅
```c
[STATUS] --------------------
DAC1: PLAY | Samples: 0 | ...        ← 데이터 수신 안됨
SPI:  Errors: 5 | Invalid Headers: 10  ← SPI 통신 문제
```

## 📚 참고 문서

- `SLAVE_IMPLEMENTATION_SPEC.md`: 전체 명세서
- `CLAUDE.md`: 프로젝트 구조 및 빌드 가이드
- Master 펌웨어:
  - `Core/Inc/spi_protocol.h`: 프로토콜 정의
  - `Core/Src/spi_protocol.c`: Master 송신 로직

## ✉️ 문의

구현 중 문제 발생 시:
1. UART 로그 확인 (115200 baud)
2. 에러 통계 확인 (`[STATUS]` 출력)
3. Master 팀과 통신 타이밍 협의

---

**구현 완료**: 2025-11-01
**버전**: v1.0 (Slave ID 0)
**Status**: ✅ 코드 구현 완료, Master 연동 테스트 대기
