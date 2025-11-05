# 🚨 긴급: 마스터 CS-SCK 타이밍 문제 발견

## 날짜: 2025-11-04

## 오실로스코프 측정 결과

### 측정 조건
- 타임스케일: 1.00ms/div
- CH1 (노란색): CS 신호
- CH3 (붉은색): SCK 신호

### ⚠️ 문제 발견

**CS falling edge 후 약 3~5ms 후에 SCK가 시작됨!**

```
타이밍:
CS:  ¯¯¯¯¯\_________________________________/¯¯¯¯¯
          ↑                              ↑
          |<------- 약 8~10ms -------->|

SCK: ¯¯¯¯¯¯¯¯¯¯¯||||||||||||||||||||¯¯¯¯¯¯¯¯¯¯¯¯
               ↑ 약 3~5ms 후 시작
               ↑ 버스트 지속 시간: 약 50μs
```

## 🔥 이것이 불안정한 수신의 원인!

### SPI 표준 타이밍 (정상)

```
CS와 SCK는 거의 동시에 시작해야 함!

CS:  ¯¯\_________________/¯¯¯¯
SCK: ¯¯¯||||||||||||||||¯¯¯¯¯
       ↑ 최대 10~20μs 이내
```

### 현재 타이밍 (비정상)

```
CS:  ¯¯\_______________________/¯¯¯¯
                   ↑
SCK: ¯¯¯¯¯¯¯¯||||||||||||||||¯¯¯¯¯
       ↑ 3~5ms 지연! (비정상)
```

## 📉 왜 수신 값이 매번 다른가?

### 슬레이브 동작:
1. CS falling edge 감지 (t=0ms)
2. 즉시 `HAL_SPI_Receive_IT()` 호출
3. SPI 하드웨어가 SCK를 기다림
4. **3~5ms 후** 마스터가 SCK 시작 (t=3~5ms)
5. 이 긴 대기 시간 동안 SPI 상태 머신 불안정
6. **비트 동기화가 매번 달라짐**
7. 결과: 같은 데이터인데 다르게 수신됨

## ✅ 해결 방법

### 마스터 코드 수정 필요!

#### 현재 코드 (추정):
```c
void send_command(uint8_t *packet, uint8_t len)
{
    GPIO_WritePin(CS0_Pin, LOW);  // CS 내림

    // ← 여기서 무언가 하고 있음! (3~5ms 소요)
    //   - printf 디버그?
    //   - 다른 처리?
    //   - 불필요한 대기?

    HAL_SPI_Transmit(&hspi, packet, len, timeout);  // SCK 시작
    GPIO_WritePin(CS0_Pin, HIGH);
}
```

#### 수정된 코드:
```c
void send_command(uint8_t *packet, uint8_t len)
{
    // 모든 준비를 미리 완료!

    GPIO_WritePin(CS0_Pin, LOW);  // CS 내림
    // 즉시 전송! (지연 없음)
    HAL_SPI_Transmit(&hspi, packet, len, timeout);  // SCK 시작
    GPIO_WritePin(CS0_Pin, HIGH);  // CS 올림

    // 디버그 printf는 CS HIGH 후에!
    // printf("[MASTER] Sent %d bytes\r\n", len);
}
```

## 🔍 마스터 측 체크 사항

### 1. CS와 HAL_SPI_Transmit() 사이 코드 확인

다음 사이에 **어떤 코드도 없어야** 합니다:

```c
GPIO_WritePin(CS_Pin, LOW);
HAL_SPI_Transmit(...);  // ← 이 사이에 코드 없음!
```

특히 다음을 제거해야 함:
- `HAL_Delay()`
- `printf()`
- 다른 GPIO 제어
- 루프나 조건문

### 2. 하드웨어 NSS 사용 확인

만약 하드웨어 NSS를 사용한다면:

```c
hspi_master.Init.NSS = SPI_NSS_HARD_OUTPUT;
```

이 경우 CS를 GPIO로 제어하지 말고, HAL이 자동으로 처리하게 해야 합니다:

```c
// GPIO CS 제어 제거!
// GPIO_WritePin(CS_Pin, LOW);  // ← 삭제
HAL_SPI_Transmit(&hspi, packet, len, timeout);
// GPIO_WritePin(CS_Pin, HIGH);  // ← 삭제
```

### 3. 여러 슬레이브 제어 시

각 슬레이브 전송은 독립적으로:

```c
// 잘못됨:
GPIO_WritePin(CS0_Pin, LOW);
GPIO_WritePin(CS1_Pin, LOW);
GPIO_WritePin(CS2_Pin, LOW);
// ← 여기서 전송 준비
HAL_SPI_Transmit(...);  // ← 어느 슬레이브?

// 올바름:
// Slave 0
GPIO_WritePin(CS0_Pin, LOW);
HAL_SPI_Transmit(&hspi, packet0, 5, 100);
GPIO_WritePin(CS0_Pin, HIGH);

// Slave 1
GPIO_WritePin(CS1_Pin, LOW);
HAL_SPI_Transmit(&hspi, packet1, 5, 100);
GPIO_WritePin(CS1_Pin, HIGH);
```

## 🧪 테스트 방법

### 오실로스코프로 확인:

수정 후 다시 측정:
1. 타임스케일: **10μs/div** 로 확대
2. CS falling edge에서 트리거
3. 첫 SCK rising edge까지 시간 측정

**예상 결과:**
```
CS falling → 첫 SCK: < 20μs (이상적)
                    < 100μs (허용)
```

### 슬레이브 로그 확인:

수정 후 슬레이브는 안정적으로 수신해야 합니다:

```
[RX_PKT] C0 00 01 00 00  (시도 1) ✓
[RX_PKT] C0 00 01 00 00  (시도 2) ✓
[RX_PKT] C0 00 01 00 00  (시도 3) ✓
...
(매번 같은 값)
```

## 📝 요약

**문제:** CS와 SCK 사이 3~5ms 간격으로 슬레이브 동기화 실패

**해결:** CS LOW 직후 즉시 HAL_SPI_Transmit() 호출 (지연 없음)

**목표 타이밍:** CS falling → SCK 시작 < 20μs

---

**슬레이브는 정상입니다. 마스터의 타이밍을 수정해주세요!**
