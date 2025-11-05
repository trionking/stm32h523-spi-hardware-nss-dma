# Slave 팀 긴급 공지

**발신**: Master 개발팀
**수신**: Slave 개발팀
**최초 작성**: 2025-11-01
**최종 업데이트**: 2025-11-03
**중요도**: 🔴 **최고 긴급**

---

## 🚨 긴급 업데이트 (2025-11-03)

### ⚠️ SPI 1-byte shift 문제 발견 - 즉시 수정 필요!

**Master-Slave 통합 테스트 결과, Slave가 첫 번째 바이트를 놓치는 치명적인 문제가 발견되었습니다.**

#### 문제 증상
```
Master 전송:   [0xC0, 0x00, 0x00, 0x01, 0x00, 0x00]
                ^^^^  ^^^^  ^^^^  ^^^^  ^^^^  ^^^^
                HDR   ID    CH    CMD   PH    PL

Slave 수신:    [????, 0xC0, 0x00, 0x00, 0x01, 0x00]
                      ^^^^  ^^^^  ^^^^  ^^^^  ^^^^
                      ID    CH    CMD   PH    PL (shift됨!)
```

**결과**: 헤더가 SlaveID 위치로 shift → `SlaveID=192` (0xC0)로 잘못 인식

#### 즉시 수정 방법

**CS falling edge 인터럽트에서 첫 1바이트를 읽어 버리기:**

```c
// CS 핀 EXTI 콜백 추가/수정
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == CS_Pin) {
        if (HAL_GPIO_ReadPin(CS_GPIO_Port, CS_Pin) == GPIO_PIN_RESET) {
            // CS falling edge

            // ⚠️ 첫 바이트는 손실되므로 1바이트 읽어서 버림
            uint8_t dummy;
            HAL_SPI_Receive(&hspi, &dummy, 1, 10);

            // RDY LOW
            HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_RESET);

            // 실제 헤더부터 수신 시작
            HAL_SPI_Receive_IT(&hspi, &rx_header, 1);
            rx_state = STATE_WAIT_HEADER;
        }
    }
}
```

#### 구현 단계

1. **CS 핀을 EXTI로 설정**
   ```c
   GPIO_InitStruct.Pin = CS_Pin;
   GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
   GPIO_InitStruct.Pull = GPIO_PULLUP;
   HAL_GPIO_Init(CS_GPIO_Port, &GPIO_InitStruct);

   HAL_NVIC_SetPriority(EXTIx_IRQn, 0, 0);
   HAL_NVIC_EnableIRQ(EXTIx_IRQn);
   ```

2. **위의 EXTI 콜백 구현**

3. **테스트**: `SPITEST BASIC 0` 명령으로 확인

#### 예상 작업 시간
**1~2시간** (구현 + 테스트)

#### 자세한 내용
**SLAVE_IMPLEMENTATION_SPEC.md** 파일의 "🚨 긴급 공지" 섹션 참조

---

## 🚨 중요 공지사항 (2025-11-01)

### 1. SPI 클럭 속도 최종 확정

**중요**: Master 측 SPI 클럭 속도가 정확히 계산되었습니다.

```
Master 시스템 클럭: 550MHz
APB2 클럭: 137.5MHz
SPI BaudRatePrescaler: 16
→ SPI 클럭: 137.5MHz / 16 = 8.59375MHz
```

**결론**: **약 8.6MHz** (10MHz 이하)

**⚠️ Slave 팀 긴급 확인 필요**:
- [ ] Slave 측에서 **8.6MHz** SPI 클럭 수신 가능한가요?
- [ ] Slave 측 SPI 최소/최대 허용 속도는?
- [ ] 테스트 전에 회신 부탁드립니다!

---

### 2. Master 펌웨어 업데이트 완료

**변경 사항**:
- ✅ SPI BaudRatePrescaler: 2 → 16 변경
- ✅ CS 셋업 타임: 50μs 구현
- ✅ RESET 명령 (0xFF) 추가
- ✅ DMA 콜백 최적화

**빌드 정보**:
- 빌드 일시: 2025-11-01
- 펌웨어 버전: v1.00 (통합 테스트용)
- 메모리: text=129KB, data=356B, bss=187KB

**⚠️ 플래시 예정**:
- 오늘 저녁까지 Master 보드에 플래시 완료 예정
- 내일 테스트 시 최신 펌웨어 사용

---

### 3. 테스트 준비 현황

**Master 팀 완료**:
- [x] SPI 클럭 속도 조정
- [x] 펌웨어 빌드 완료
- [x] 프로토콜 호환성 검증
- [x] 문서 작성 완료
  - MASTER_RESPONSE.md (상세 답변, 413줄)
  - INTEGRATION_TEST_CHECKLIST.md (테스트 절차)

**Master 팀 진행 중**:
- [ ] Master 보드 플래시 (오늘 완료 예정)
- [ ] 테스트 WAV 파일 생성 (오늘 완료 예정)
  - test_1khz.wav (1kHz 정현파, 10초)
  - test_500hz.wav (500Hz 정현파, 10초)
  - test_music.wav (음악, 30초)

---

### 4. 하드웨어 연결 최종 확인

**Slave 0 연결 핀** (최종 확정):

| 신호 | Master 핀 | Slave 0 핀 | 비고 |
|------|----------|-----------|------|
| SCK | **PA5** | PA5 | SPI 클럭 (8.6MHz) |
| MOSI | **PB5** | PB5 | Master → Slave 데이터 |
| MISO | **PB4** | PB4 | (현재 미사용) |
| CS | **PA4** | PA15 | Chip Select |
| RDY | **PF13** (입력) | PA8 (출력) | Ready 신호 |
| GND | GND | GND | ⚠️ **필수 연결** |

**연결 순서 (권장)**:
1. **GND 먼저** 연결 (통신 안정성)
2. 3.3V 연결
3. SPI 신호선 (SCK, MOSI, MISO)
4. CS, RDY 핀
5. 멀티미터로 전압 확인

---

### 5. 통합 테스트 일정 확정

**일시**: 2025-11-02 (내일), 오전 10:00 시작
**장소**: 하드웨어 랩
**예상 소요**: 3~4시간

**테스트 Phase**:
1. **Phase 1** (30분): 하드웨어 연결 및 부팅 확인
2. **Phase 2** (30분): 명령 패킷 테스트 (PLAY, STOP)
3. **Phase 3** (1시간): 데이터 패킷 및 DAC 출력 확인
4. **Phase 4** (30분): 장시간 안정성 테스트 (30초)

---

## ✅ Slave 팀 확인 요청사항

### 긴급 (오늘 회신)

- [ ] **SPI 8.6MHz 수신 가능 여부** 확인
- [ ] Slave 측 SPI 최소/최대 허용 속도 알려주세요
- [ ] 내일 통합 테스트 참석 가능 확인

### 내일 테스트 준비물

- [ ] Slave 0 보드 (펌웨어 플래시 완료)
- [ ] UART3 케이블 (115200 baud 모니터링용)
- [ ] 오실로스코프 (DAC 출력 확인용)
- [ ] 듀폰 케이블 (SPI 연결용)
- [ ] 노트북 (UART 터미널)

---

## 📋 추가 전달 사항

### 프로토콜 설정 확인

**Master SPI 설정** (최종):
```c
Mode: MASTER
CPOL: 0 (LOW)
CPHA: 0 (1 EDGE)
Data Size: 8-bit
First Bit: MSB First
Clock: 8.6MHz
NSS: Software (GPIO 제어)
```

**Slave 측 설정이 일치하는지 재확인 부탁드립니다!**

### 패킷 포맷 확인

**명령 패킷 (6 bytes)**:
```
Byte 0: 0xC0 (헤더)
Byte 1: Slave ID (0~2)
Byte 2: Channel (0=DAC1, 1=DAC2)
Byte 3: Command (0x01=PLAY, 0x02=STOP, 0x03=VOLUME, 0xFF=RESET)
Byte 4: Param High
Byte 5: Param Low
```

**데이터 패킷**:
```
Byte 0: 0xDA (헤더)
Byte 1: Slave ID (0~2)
Byte 2: Channel (0=DAC1, 1=DAC2)
Byte 3: Sample Count High (Big-endian)
Byte 4: Sample Count Low (Big-endian)
Byte 5~: Audio samples (16-bit Little-endian 각각)
```

### 타이밍 확인

- CS 셋업 타임: **50μs** (Master에서 보장)
- RDY 핀 응답: **50μs 이내** (Slave 구현 확인)
- 전송 주기: **50~60ms** (Master에서 제어)
- 패킷 간 간격: **>100μs** (자동 보장)

---

## 🔍 테스트 시 모니터링 항목

### Slave UART 로그 확인

**정상 동작 시 예상 로그**:
```
Slave ready - waiting for Master commands

[CMD] PLAY on CH0
[SPI] RX 2048 samples CH0
[DAC] Buffer swap (CH0)
[SPI] RX 2048 samples CH0
[DAC] Buffer swap (CH0)
...

[STATUS] --------------------
DAC1: PLAY | Samples: 20480 | Swaps: 10 | Underruns: 0
DAC2: STOP | Samples: 0 | Swaps: 0 | Underruns: 0
SPI:  Errors: 0 | Invalid Headers: 0 | Invalid IDs: 0
----------------------------
```

**에러 발생 시**:
```
[SPI] Invalid header: 0xXX  → CPOL/CPHA 불일치 또는 클럭 문제
[SPI] Invalid Slave ID: X   → 패킷 손상
[ERR] Buffer underrun!      → Master 전송 주기 문제
```

### Slave 측 디버깅 준비

**권장사항**:
1. UART 로그 실시간 모니터링
2. 통계 출력 활성화 (5초마다)
3. 에러 카운터 초기화 후 테스트
4. 로직 분석기 준비 (선택)

---

## 📞 연락처

### 테스트 전 질문/확인

**Master 팀**:
- 담당자: [이름 입력 필요]
- 이메일: [이메일 입력 필요]
- 전화: [전화번호 입력 필요]

**긴급 질문이 있으시면 즉시 연락 주세요!**

---

## 📚 참고 문서

**필독**:
1. **INTEGRATION_TEST_CHECKLIST.md** - 통합 테스트 절차 및 체크리스트
2. **MASTER_RESPONSE.md** - 상세 답변 및 기술 정보
3. **PROTOCOL_SUMMARY.md** - 프로토콜 전체 요약

**선택**:
- `Core/Src/spi_protocol.c` - Master SPI 전송 로직
- `Core/Src/audio_stream.c` - 스트리밍 구현
- `Core/Src/main.c:338` - SPI 클럭 설정 코드

---

**문서 버전**: 1.0
**최종 업데이트**: 2025-11-01 18:00

**⚠️ 긴급 회신 필요**:
- **SPI 8.6MHz 수신 가능 여부** (오늘 회신)
- **내일 테스트 참석 가능 확인**

**Master 팀 오늘 작업**:
- Master 보드 플래시
- 테스트 WAV 파일 준비
- 하드웨어 준비 완료

**내일 오전 10시에 뵙겠습니다!**
