# GPS Rescue 상수값을 CLI에 등록하고 컨피규레이터에서 읽기 위해 펌웨어에서 해야 할 작업

> GPS Rescue 기능에서 하드코딩된 상수값을 CLI 파라미터로 노출하고,
> Betaflight Configurator(Configurator)에서 MSP를 통해 읽고 쓸 수 있도록
> 펌웨어 단계에서 수행하는 작업의 전체 프로세스입니다.

---

## 0. 배경: 왜 필요한가?

`gps_rescue.c`에는 비행 제어 로직에 직접 삽입된 **하드코딩된 상수값**들이 있습니다.

예를 들어 하강 허용 여부를 판단하는 뱅크 각도 제한값이 4곳에 각각 `15` 또는 `10` 도로 하드코딩되어 있었습니다:

```c
// 하드코딩 전 (gps_rescue.c, 기존)
bool descentAllowed = (absError < 45.0f) || (currentRollDeg < 15.0f);  // handleShuttlePhase
bool descentAllowed = (absError < 45.0f) || (currentRollDeg < 10.0f);  // handleShuttleDescentPhase  ← 값이 다름!
bool descentAllowed = (absError < 45.0f) || (currentRollDeg < 15.0f);  // handleMissionPhase
bool descentAllowed = (absError < 45.0f) || (currentRollDeg < 15.0f);  // handleFlyHomePhase
```

이런 상태에서는:
- 값을 바꾸려면 **소스코드를 직접 수정**해야 함
- Configurator(UI)에서 **설정 변경 불가**
- 서로 다른 Phase에서 **값이 불일치** (`15°` vs `10°`)

**해결 목표:** 이 상수값을 CLI 파라미터화하고, MSP 프로토콜을 통해 Configurator에서도 제어할 수 있게 만드는 것.

---

## 1. 전체 워크플로우 (5단계)

```
┌─────────────────────────────────────────────────────────┐
│  Step 1: 하드코딩된 상수값을 구조체로 이동               │
│  → gpsRescueConfig_t에 필드 추가                        │
│  → PG reset template에 기본값 설정                     │
├─────────────────────────────────────────────────────────┤
│  Step 2: CLI 매개변수 등록                             │
│  → parameter_names.h에 이름 매크로 정의                 │
│  → settings.c의 valueTable[]에 항목 추가               │
├─────────────────────────────────────────────────────────┤
│  Step 3: 비행 로직에서 하드코딩 → 구조체 필드로 교체     │
│  → gps_rescue.c의 4곳 하드코딩 값 → config 필드 참조   │
├─────────────────────────────────────────────────────────┤
│  Step 4: 블랙박스 로그에 추가 (선택)                    │
│  → blackbox.c에 파라미터 기록 추가                      │
├─────────────────────────────────────────────────────────┤
│  Step 5: MSP 프로토콜에 GET/SET 추가 (Configurator 연동)│
│  → msp.c에 sbufWrite/get 추가                          │
│  → msp_protocol.h에 API_VERSION_MINOR 올림             │
│  → bytes-remaining guard로 하위 호환 보장              │
└─────────────────────────────────────────────────────────┘
```

---

## 2. Step 1: 구조체에 필드 추가

### 2-1. `src/main/pg/gps_rescue.h` — `gpsRescueConfig_t` 구조체에 필드 추가

```c
typedef struct gpsRescue_s {
    // ... 기존 필드들 ...
    uint8_t  imuYawGain;
    // ▼ 여기에 새 필드 추가 (구조체 정의 순서대로)
    uint8_t  descentBankLimit;  // degrees - bank angle threshold for descent allowed during turn (15-45)
} gpsRescueConfig_t;
```

**주의:** 구조체 필드 순서가 Serialization(직렬화) 순서와 1:1로 대응되므로, MSP에서 읽는 순서와 동일해야 합니다.

### 2-2. `src/main/pg/gps_rescue.c` — PG Reset Template에 기본값 설정

```c
PG_RESET_TEMPLATE(gpsRescueConfig_t, gpsRescueConfig,
    // ... 기존 필드들 ...
    .imuYawGain = 10,
    // ▼ 새 필드 기본값 추가
    .descentBankLimit = 15,       // 단위: 도, 범위: 15~45
);
```

---

## 3. Step 2: CLI 매개변수 등록

### 3-1. `src/main/fc/parameter_names.h` — 파라미터 이름 매크로 정의

```c
// GPS Rescue 관련 기존 매크로들 옆에 추가
#define PARAM_NAME_GPS_RESCUE_DESCENT_BANK "gps_rescue_descent_bank"
```

### 3-2. `src/main/cli/settings.c` — CLI valueTable에 항목 추가

```c
// 기존 GPS Rescue 파라미터들 옆에 추가
{ PARAM_NAME_GPS_RESCUE_DESCENT_BANK,
  VAR_UINT8 | MASTER_VALUE,
  .config.minmaxUnsigned = { 15, 45 },   // 최솟값, 최댓값 (단위: 도)
  PG_GPS_RESCUE,
  offsetof(gpsRescueConfig_t, descentBankLimit)
},
```

| 필드 | 의미 |
|------|------|
| `VAR_UINT8` | 자료형 (8비트 unsigned 정수) |
| `MASTER_VALUE` | profile이 아닌 global setting |
| `{ 15, 45 }` | CLI에서도, Configurator에서도 적용되는 유효 범위 |
| `PG_GPS_RESCUE` | 이 setting이 어느 PG(Parameter Group)에 속하는지 |
| `offsetof(...)` | 구조체 내에서 이 필드가 몇 바이트 offset에 있는지 |

> **CLI에서 사용:** `set gps_rescue_descent_bank = 20`
> **Configurator에서 확인/변경:** MSP를 통해 `descentBankLimit` 값을 읽어 UI에 표시

---

## 4. Step 3: 비행 로직에서 하드코딩 → 구조체 필드로 교체

### `src/main/flight/gps_rescue.c`

4개 Phase에서 하드코딩 값을 `gpsRescueConfig()->descentBankLimit`로 교체:

```c
// handleShuttlePhase()      (~line 596)
-    bool descentAllowed = (absError < 45.0f) || (currentRollDeg < 15.0f);
+    bool descentAllowed = (absError < 45.0f) || (currentRollDeg < gpsRescueConfig()->descentBankLimit);

// handleShuttleDescentPhase() (~line 613)
-    bool descentAllowed = (absError < 45.0f) || (currentRollDeg < 10.0f);
+    bool descentAllowed = (absError < 45.0f) || (currentRollDeg < gpsRescueConfig()->descentBankLimit);

// handleMissionPhase()      (~line 832)
-    bool descentAllowed = (absError < 45.0f) || (currentRollDeg < 15.0f);
+    bool descentAllowed = (absError < 45.0f) || (currentRollDeg < gpsRescueConfig()->descentBankLimit);

// handleFlyHomePhase()      (~line 889)
-    bool descentAllowed = (absError < 45.0f) || (currentRollDeg < 15.0f);
+    bool descentAllowed = (absError < 45.0f) || (currentRollDeg < gpsRescueConfig()->descentBankLimit);
```

> **포인트:** 기존에는 Phase마다 값이 달랐음 (`15` vs `10`). 하나의 구조체 필드로 **통일**하고, 범위 제한(`15~45`)을 CLI/Configurator에서 강제함.

---

## 5. Step 4: 블랙박스 로그에 추가 (선택)

### `src/main/blackbox/blackbox.c`

블랙박스 기록에도 새 파라미터를 포함:

```c
BLACKBOX_PRINT_HEADER_LINE(PARAM_NAME_GPS_RESCUE_DESCENT_BANK, "%d",
    gpsRescueConfig()->descentBankLimit)
```

---

## 6. Step 5: MSP 프로토콜에 GET/SET 추가 (Configurator 연동 핵심)

이 단계가 **Configurator에서 읽고 쓸 수 있게 하는 핵심**입니다.

### 6-1. GET — Configurator가 값을 읽을 때

`src/main/msp/msp.c`의 `case MSP_GPS_RESCUE:` 블록에 추가:

```c
case MSP_GPS_RESCUE:
    // ... 기존 sbufWrite* 호출들 ...
    // API version 1.46까지만 전송됨
    sbufWriteU16(dst, gpsRescueConfig()->initialClimbM);

    // ============ ADD: API version 1.49 ============
    // Added in API version 1.49
    sbufWriteU16(dst, gpsRescueConfig()->descentBankLimit);
    break;
```

**직렬화 순서가 중요합니다.** Configurator는 수신한 바이트를 구조체 필드 순서대로 역직렬화하므로, GET에서 써주는 순서와 SET에서 읽는 순서가 **동일**해야 합니다.

### 6-2. SET — Configurator가 값을 설정할 때

`src/main/msp/msp.c`의 `case MSP_SET_GPS_RESCUE:` 블록에 추가:

```c
    // ... 기존 SET 핸들러 ...
    if (sbufBytesRemaining(src) >= 2) {
        // Added in API version 1.46
        gpsRescueConfigMutable()->initialClimbM = sbufReadU16(src);
    }

    // ============ ADD: API version 1.49 ============
    if (sbufBytesRemaining(src) >= 2) {
        // Added in API version 1.49
        gpsRescueConfigMutable()->descentBankLimit = sbufReadU16(src);
    }
    break;
```

#### ⚠️ 하위 호환을 위한 `sbufBytesRemaining` 가드

```c
if (sbufBytesRemaining(src) >= 2) { ... }
```

이 가드가 **반드시 필요**합니다. 이유:

- Configurator가 MSP_SET_GPS_RESCUE 메시지를 보낼 때,
- **구버전 Configurator**(API 1.48 이하)는 `descentBankLimit` 바이트를 보내지 않음
- 가드가 없으면: 잔여 바이트를 잘못 해석하여 **다른 필드가 손상**될 수 있음
- 가드가 있으면: 바이트가 없으면 **건너뛰고 다음으로** → 안전

### 6-3. `src/main/msp/msp_protocol.h` — API 버전 올리기

```c
#define API_VERSION_MAJOR 1
-#define API_VERSION_MINOR 46  // 이전
+#define API_VERSION_MINOR 49  // 변경사항 반영 후
```

> **API_VERSION_MINOR를 올리는 이유:** Configurator가 연결 시 서버의 API 버전을 확인하고,
> 지원하는 기능만 전송합니다. 이 숫자가 변경되지 않으면 구버전 Configurator는 새 필드를 인식하지 못합니다.

### 6-4. MSP 직렬화 순서 매핑 (핵심 참조표)

GET과 SET에서 필드들이 **동일한 순서**로 처리되어야 합니다:

| 순서 | MSP 바이트 수 | 필드 | 버전 |
|------|-------------|------|------|
| 1 | U16 (2 byte) | `maxRescueAngle` | 기본 |
| 2 | U16 | `returnAltitudeM` | 기본 |
| 3 | U16 | `descentDistanceM` | 기본 |
| ... | ... | (기존 필드들) | ... |
| N | U16 | `initialClimbM` | API 1.46 |
| N+1 | U16 | **`descentBankLimit`** | **API 1.49** ← 새 필드 |

> Configurator가 GET 수신 → 구조체 역직렬화할 때 이 순서대로 읽습니다.
> SET 송신 → 같은 순서대로 씁니다. 순서가 다르면 값이 꼬입니다.

---

## 7. 커밋 전략 권장

위 5단계는 하나의 커밋으로 할 수도 있고, 논리 단위로 나눌 수도 있습니다:

| 커밋 | 내용 |
|------|------|
| Commit A | Step 1~3: 구조체 추가 + CLI 등록 + gps_rescue.c 교체 |
| Commit B | Step 5만: MSP GET/SET 추가 + API 버전 업 |
| Commit C | Step 4: 블랙박스 로그 추가 |

→ 기존 PR 패턴에 맞게 나누되, **MSP 변경(Step 5)은 반드시 하나의 커밋**으로 묶는 것이 깔끔합니다.

---

## 8. 변경 파일 체크리스트

| 파일 | 필수 여부 | 변경 내용 |
|------|----------|----------|
| `src/main/pg/gps_rescue.h` | ✅ 필수 | 구조체 필드 추가 |
| `src/main/pg/gps_rescue.c` | ✅ 필수 | PG reset template에 기본값 |
| `src/main/fc/parameter_names.h` | ✅ 필수 | 매크로 정의 |
| `src/main/cli/settings.c` | ✅ 필수 | CLI valueTable 등록 |
| `src/main/flight/gps_rescue.c` | ✅ 필수 | 하드코딩 → 구조체 필드로 교체 |
| `src/main/blackbox/blackbox.c` | 🔲 선택 | 블랙박스 로그 기록 |
| `src/main/msp/msp.c` | ✅ 필수 (Configurator 연동) | GET/SET handlers에 추가 |
| `src/main/msp/msp_protocol.h` | ✅ 필수 (Configurator 연동) | API_VERSION_MINOR 업 |

---

## 9. 검증 방법

```bash
# 1. 빌드 확인
make TARGET=STM32F411

# 2. diff로 변경 범위 확인
git diff --stat HEAD~1

# 3. MSP GET 응답 바이트 수 확인 (필요시)
#    Configurator가 수신하는 바이트 수가 직렬화 순서와 일치하는지 수동 검증

# 4. CLI에서 값 확인/변경 테스트
#    CLI: set gps_rescue_descent_bank
#    → 출력값이 정상적(15~45 범위)인지 확인
```

---

*작성 기반 커밋: `f7349f1` (CLI 등록) / `5fe0220` (MSP 추가)*