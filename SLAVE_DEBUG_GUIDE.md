# Slave 디버깅 가이드 - 패킷 파싱 문제 해결

**날짜**: 2025-11-03
**대상**: STM32H523 Slave 펌웨어 개발자
**상태**: 🔍 **디버깅 중** - 패킷 수신되지만 잘못 파싱됨

---

## 현재 상황

### ✅ 좋은 소식
- **Slave가 패킷을 수신하고 있습니다!**
- 통계: `Total=3, CMD=2, DATA=1` (패킷 3개 수신)
- NSS 소프트웨어 모드 + EXTI 인터럽트 설정 완료

### ❌ 문제
- **수신된 데이터가 잘못 파싱되고 있습니다**

**Master가 보낸 명령:**
```
PLAY 명령: [0xC0, 0x00, 0x00, 0x01, 0x00, 0x00]
            HDR   ID    CH    CMD   PH    PL
            C0    00    00    01    00    00
```

**Slave가 파싱한 결과:**
```
CMD #1: SlaveID=0, Ch=0, Cmd=0x08, Param=7
                          ^^^^      ^^^^
                          틀림!     틀림!
```

**예상**: `Cmd=0x01, Param=0`
**실제**: `Cmd=0x08, Param=7`

---

## 즉시 수행할 디버깅 작업

### 1단계: EXTI 콜백 디버깅

**목적**: CS falling edge가 감지되는지, dummy byte가 제대로 읽히는지 확인

**코드 수정:**
```c
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == NSS_Pin) {
        if (HAL_GPIO_ReadPin(NSS_GPIO_Port, NSS_Pin) == GPIO_PIN_RESET) {
            // ⚠️ 추가: EXTI 호출 확인
            printf("[EXTI] CS falling edge detected!\r\n");

            // 첫 바이트 읽어서 버림
            uint8_t dummy;
            HAL_SPI_Receive(&hspi, &dummy, 1, 10);

            // ⚠️ 추가: dummy byte 값 출력
            printf("[EXTI] Dummy byte read: 0x%02X\r\n", dummy);

            // RDY LOW
            HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_RESET);

            // 실제 헤더 수신 시작
            HAL_SPI_Receive_IT(&hspi, &rx_header, 1);
            rx_state = STATE_WAIT_HEADER;

            // ⚠️ 추가: 수신 시작 확인
            printf("[EXTI] Started header reception\r\n");
        }
    }
}
```

**예상 출력:**
```
[EXTI] CS falling edge detected!
[EXTI] Dummy byte read: 0x??
[EXTI] Started header reception
```

---

### 2단계: RAW 바이트 출력

**목적**: 실제로 수신된 바이트 확인 (파싱 전)

**코드 수정:**
```c
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    static uint8_t rx_cmd_buffer[6];  // 명령 패킷 버퍼

    switch(rx_state) {
        case STATE_WAIT_HEADER:
            // 헤더 수신 완료
            if (rx_header == 0xC0) {
                // 나머지 5바이트 수신
                HAL_SPI_Receive_IT(hspi, rx_cmd_buffer + 1, 5);
                rx_cmd_buffer[0] = rx_header;
                rx_state = STATE_RECEIVE_CMD;
            }
            break;

        case STATE_RECEIVE_CMD:
            // ⚠️ 추가: RAW 바이트 출력
            printf("[RX] Raw bytes: ");
            for (int i = 0; i < 6; i++) {
                printf("%02X ", rx_cmd_buffer[i]);
            }
            printf("\r\n");

            // 구조체로 캐스팅
            CommandPacket_t *cmd = (CommandPacket_t*)rx_cmd_buffer;

            // ⚠️ 추가: 각 필드 개별 출력
            printf("[PARSE] Header=0x%02X, SlaveID=%d, Ch=%d, Cmd=0x%02X, ParamH=0x%02X, ParamL=0x%02X\r\n",
                   cmd->header, cmd->slave_id, cmd->channel,
                   cmd->cmd, cmd->param_h, cmd->param_l);

            uint16_t param = (cmd->param_h << 8) | cmd->param_l;
            printf("[PARSE] Param (combined) = %d\r\n", param);

            // 기존 처리 로직...
            process_command(cmd);

            // 다음 패킷 대기
            HAL_SPI_Receive_IT(hspi, &rx_header, 1);
            rx_state = STATE_WAIT_HEADER;
            break;
    }
}
```

**예상 출력:**
```
[RX] Raw bytes: C0 00 00 01 00 00
[PARSE] Header=0xC0, SlaveID=0, Ch=0, Cmd=0x01, ParamH=0x00, ParamL=0x00
[PARSE] Param (combined) = 0
[CMD #1] SlaveID=0, Ch=0, Cmd=0x01 (PLAY), Param=0
```

---

### 3단계: 구조체 크기 및 정렬 확인

**목적**: 패킷 구조체가 정확히 6바이트인지 확인

**코드 추가 (초기화 시 한 번만):**
```c
void spi_test_init(void)
{
    // ⚠️ 추가: 구조체 크기 확인
    printf("=== Structure Size Check ===\r\n");
    printf("sizeof(CommandPacket_t) = %d bytes (expected: 6)\r\n",
           sizeof(CommandPacket_t));

    // ⚠️ 추가: 각 필드 오프셋 확인
    CommandPacket_t test_packet;
    printf("Offset of header:   %d\r\n", (uint8_t*)&test_packet.header - (uint8_t*)&test_packet);
    printf("Offset of slave_id: %d\r\n", (uint8_t*)&test_packet.slave_id - (uint8_t*)&test_packet);
    printf("Offset of channel:  %d\r\n", (uint8_t*)&test_packet.channel - (uint8_t*)&test_packet);
    printf("Offset of cmd:      %d\r\n", (uint8_t*)&test_packet.cmd - (uint8_t*)&test_packet);
    printf("Offset of param_h:  %d\r\n", (uint8_t*)&test_packet.param_h - (uint8_t*)&test_packet);
    printf("Offset of param_l:  %d\r\n", (uint8_t*)&test_packet.param_l - (uint8_t*)&test_packet);
    printf("============================\r\n");
}
```

**예상 출력:**
```
=== Structure Size Check ===
sizeof(CommandPacket_t) = 6 bytes (expected: 6)
Offset of header:   0
Offset of slave_id: 1
Offset of channel:  2
Offset of cmd:      3
Offset of param_h:  4
Offset of param_l:  5
============================
```

**만약 6바이트가 아니라면:**
```c
// 구조체 정의 확인 - __attribute__((packed)) 필수!
typedef struct __attribute__((packed)) {
    uint8_t header;         // 0xC0
    uint8_t slave_id;       // 0~2
    uint8_t channel;        // 0=DAC1, 1=DAC2
    uint8_t cmd;            // 명령 코드
    uint8_t param_h;        // 파라미터 상위 바이트
    uint8_t param_l;        // 파라미터 하위 바이트
} CommandPacket_t;
```

---

## 테스트 절차

### 1. 위 코드 수정 적용

### 2. 빌드 및 플래시

### 3. Slave를 SPI 테스트 모드(Test 5)로 진입

### 4. Master에서 테스트 실행
```
SPITEST BASIC 0
```

### 5. Slave UART 출력 전체 복사해서 전달

---

## 예상 결과

### 케이스 1: EXTI가 호출되지 않음
```
[STATS] Total=0, CMD=0, DATA=0, Errors=0
(EXTI 출력 없음)
```
→ **문제**: EXTI 인터럽트 설정 오류 또는 NSS 핀 연결 문제

### 케이스 2: RAW 바이트가 정상, 파싱이 틀림
```
[EXTI] CS falling edge detected!
[EXTI] Dummy byte read: 0x??
[RX] Raw bytes: C0 00 00 01 00 00
[PARSE] Header=0xC0, SlaveID=0, Ch=0, Cmd=0x08, ParamH=0x??, ParamL=0x??
```
→ **문제**: 구조체 정렬 문제 또는 캐스팅 문제

### 케이스 3: RAW 바이트 자체가 틀림
```
[EXTI] CS falling edge detected!
[EXTI] Dummy byte read: 0x??
[RX] Raw bytes: ?? C0 00 00 01 00
```
→ **문제**: 여전히 1바이트 shift (dummy byte가 제대로 읽히지 않음)

### 케이스 4: 정상 (목표)
```
[EXTI] CS falling edge detected!
[EXTI] Dummy byte read: 0x??
[EXTI] Started header reception
[RX] Raw bytes: C0 00 00 01 00 00
[PARSE] Header=0xC0, SlaveID=0, Ch=0, Cmd=0x01, ParamH=0x00, ParamL=0x00
[PARSE] Param (combined) = 0
[CMD #1] SlaveID=0, Ch=0, Cmd=0x01 (PLAY), Param=0
```

---

## 추가 확인 사항

### NSS 핀 설정 재확인

```c
// main.c 또는 초기화 함수
void MX_GPIO_Init(void)
{
    // ... 기존 코드 ...

    // NSS 핀: 반드시 GPIO + EXTI 모드여야 함
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = NSS_Pin;  // 예: GPIO_PIN_15
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;  // ⚠️ EXTI 인터럽트
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(NSS_GPIO_Port, &GPIO_InitStruct);  // 예: GPIOA

    // ⚠️ 중요: SPI NSS를 Alternate Function으로 설정하면 안 됨!
    // GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;  // ❌ 이렇게 하면 안 됨!

    // EXTI 인터럽트 활성화
    HAL_NVIC_SetPriority(EXTI15_IRQn, 0, 0);  // 핀 번호에 맞게 조정
    HAL_NVIC_EnableIRQ(EXTI15_IRQn);
}
```

### SPI 초기화 재확인

```c
void MX_SPI_Init(void)
{
    hspi.Instance = SPI1;  // 또는 사용 중인 SPI
    hspi.Init.Mode = SPI_MODE_SLAVE;
    hspi.Init.Direction = SPI_DIRECTION_2LINES;
    hspi.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi.Init.CLKPolarity = SPI_POLARITY_LOW;   // CPOL=0
    hspi.Init.CLKPhase = SPI_PHASE_1EDGE;       // CPHA=0
    hspi.Init.NSS = SPI_NSS_SOFT;  // ⚠️ 소프트웨어 NSS!
    hspi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;

    if (HAL_SPI_Init(&hspi) != HAL_OK) {
        Error_Handler();
    }
}
```

---

## 체크리스트

수정 전 확인:
- [ ] `CommandPacket_t` 구조체에 `__attribute__((packed))` 있는가?
- [ ] NSS 핀이 `GPIO_MODE_IT_FALLING`으로 설정되었는가?
- [ ] `hspi.Init.NSS = SPI_NSS_SOFT`로 설정되었는가?
- [ ] EXTI 인터럽트가 활성화되었는가? (`HAL_NVIC_EnableIRQ`)

수정 후 테스트:
- [ ] 1단계 디버그 출력 추가
- [ ] 2단계 RAW 바이트 출력 추가
- [ ] 3단계 구조체 크기 확인 추가
- [ ] 빌드 및 플래시 완료
- [ ] `SPITEST BASIC 0` 실행
- [ ] Slave UART 출력 전체 복사

---

## 결과 보고 양식

테스트 후 다음 정보를 Master 팀에 전달:

```
=== Slave Debug Output ===
[여기에 Slave UART 출력 전체 복사]

=== 추가 정보 ===
sizeof(CommandPacket_t) = ?? bytes
NSS 핀: GPIO?? Pin ??
SPI Instance: SPI?
EXTI Line: EXTI?_IRQn
```

---

**이 가이드대로 수정하고 테스트한 후, 전체 출력을 Master 팀에 전달해주세요!**
