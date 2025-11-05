# SPI 비트 시프트 문제 분석

## 날짜: 2025-11-04

## 🚨 문제 발견

슬레이브가 데이터를 수신하지만 **비트 시프트**되어 수신됨!

### 실제 수신 데이터:

```
마스터 전송: C0 00 00 01 00 00  (PLAY 0 0)
슬레이브 수신: C0 00 00 00 80 00  (Unknown command)

비교:
Byte 0: C0 = C0  ✓
Byte 1: 00 = 00  ✓
Byte 2: 00 = 00  ✓
Byte 3: 01 ≠ 00  ✗ (비트 손실!)
Byte 4: 00 ≠ 80  ✗ (비트 시프트!)
Byte 5: 00 = 00  ✓ (우연히 일치)

비트 분석:
0x01 = 0b00000001
0x80 = 0b10000000  ← 7비트 왼쪽 시프트 또는 1비트 손실
```

## 🔍 원인 분석

### 처음 3바이트는 정상, 나머지 3바이트는 시프트됨

이것은 **6바이트가 2번에 나누어 전송**되고 있음을 강력히 시사합니다!

### 추정되는 마스터 코드:

```c
// 현재 마스터가 이렇게 하고 있을 가능성:
void send_command(uint8_t header, uint8_t slave_id, uint8_t channel,
                  uint8_t cmd, uint8_t param_h, uint8_t param_l)
{
    uint8_t packet1[3] = {header, slave_id, channel};
    uint8_t packet2[3] = {cmd, param_h, param_l};

    GPIO_WritePin(CS_Pin, LOW);
    HAL_SPI_Transmit(&hspi, packet1, 3, timeout);  // 첫 3바이트
    // ← 여기서 짧은 지연 (10~100μs)
    HAL_SPI_Transmit(&hspi, packet2, 3, timeout);  // 나머지 3바이트
    GPIO_WritePin(CS_Pin, HIGH);
}
```

### 문제점:

**두 번의 `HAL_SPI_Transmit()` 호출 사이에 SCLK 간격(gap)이 있습니다.**

```
SCLK: ||||||||||||||||||||||||  (24클럭, 3바이트)
         (갭 10~100μs)
      ||||||||||||||||||||||||  (24클럭, 3바이트)
```

이 갭 동안:
1. 슬레이브 SPI가 대기 상태로 전환
2. FIFO 포인터 또는 비트 카운터가 리셋되지 않음
3. 다음 바이트 수신 시 1비트 어긋남 (misalignment)

## ✅ 해결 방법

### Option 1: 6바이트를 한 번에 전송 (권장)

```c
void send_command(uint8_t header, uint8_t slave_id, uint8_t channel,
                  uint8_t cmd, uint8_t param_h, uint8_t param_l)
{
    uint8_t packet[6] = {header, slave_id, channel, cmd, param_h, param_l};

    GPIO_WritePin(CS_Pin, LOW);
    HAL_SPI_Transmit(&hspi, packet, 6, timeout);  // 6바이트 한번에!
    GPIO_WritePin(CS_Pin, HIGH);
}
```

**장점:**
- SCLK 연속 생성 (간격 없음)
- 비트 동기화 유지
- 더 빠름

### Option 2: NSS 하드웨어 제어 사용

만약 GPIO로 CS를 제어하고 있다면, 하드웨어 NSS로 변경:

```c
hspi_master.Init.NSS = SPI_NSS_HARD_OUTPUT;  // 하드웨어 제어
```

**장점:**
- HAL이 자동으로 NSS를 제어
- 각 바이트마다 타이밍 정확함

## 🔬 마스터 측 확인 요청

### 1. 코드 확인

마스터의 명령 전송 함수를 보여주세요:
- 6바이트를 한 번에 보내는지?
- 여러 번에 나누어 보내는지?
- HAL_SPI_Transmit()을 몇 번 호출하는지?

### 2. 오실로스코프 확인

**SCLK 신호를 자세히 측정해주세요:**

```
측정 사항:
□ SCLK가 연속 48클럭인가요?
  - YES: ||||||||||||||||||||||||||||||||||||||||||||||||
  - NO:  ||||||||||||||||||||||||  (갭)  ||||||||||||||||||||||||

□ SCLK 간격(gap)이 있다면:
  - 갭 위치: 몇 번째 클럭 후?
  - 갭 시간: 얼마나?

□ MOSI 신호를 SPI 디코드해주세요:
  - 실제 전송 데이터: ?? ?? ?? ?? ?? ??
```

### 3. 마스터 디버그 로그

마스터 코드에 바이트별 전송 로그를 추가해주세요:

```c
printf("[MASTER] Sending: ");
for (int i = 0; i < 6; i++) {
    printf("%02X ", packet[i]);
}
printf("\r\n");

printf("[MASTER] HAL_SPI_Transmit START\r\n");
HAL_StatusTypeDef status = HAL_SPI_Transmit(&hspi, packet, 6, 100);
printf("[MASTER] HAL_SPI_Transmit returned: %d\r\n", status);
```

## 📊 슬레이브 측 상태 (정상)

```
✅ SPI 콜백 호출됨: [SPI_CB] CMD_RX_DONE
✅ 6바이트 수신 완료
✅ 처음 3바이트: 정확함
❌ 나머지 3바이트: 비트 시프트됨

EXTI 카운트: 3번 발생 → 1번 성공
→ 여전히 간헐적 성공 패턴
```

## 🎯 결론

**문제는 마스터가 6바이트를 2번에 나누어 전송하면서 SCLK 간격이 발생하는 것입니다.**

**해결책: 6바이트를 단일 `HAL_SPI_Transmit()` 호출로 전송하세요!**

---

**슬레이브는 정상 동작 중입니다. 마스터의 전송 방식을 수정해주세요.**
