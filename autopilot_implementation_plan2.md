# GPS Rescue 기반 Waypoint Mission 구현 계획서 v2 (검증 완료)

목적과 방법에 대한 자연어 기술

목적 : 최신 버전의 공식 베타플라이트에서 autopilot 기능을 만들어 컨피규레이터에서 waypoint를 입력 가능하게 하였다. https://master.app.betaflight.com/# 으로 실행하면 최신 버전의 앱이 실행된다.

나는 개인적으로 베타플라이트 구버전 4.5.3을 자체 발전시켜 멀티콥터를 배제한 윙 전용 펌웨어를 개발해

사용중이다.  최신 버전에도 없는 윙 레스큐를 개발해서 ( 대신 멀티콥터 레스큐는 사망) 아주 잘 사용중이다.

최신 버전의 멀티콥터용 autopilot 기능을 도입하려는 계획을 세웠다. 완전 도입은 아니고 최소 코드 수정으로 거의 비슷한 기능을 구현하는것이다. 내가 사용하는 레스큐는 발동지점에서 홈으로 바로 가는것이 아니고 A B 두 포인트를 거쳐서 홈으로 가는 로직이 있다.  최선 컨피규레이터에서 waypoint를 입력 받아 그 좌표를 모두 돌아서 온다면 ( A B 포인트 대신 ) 거의 비슷한 기능을 한다고 본다.

심지어 최신 버전은 윙에 대한 고려는 전혀없지만 내가 개발한 버전은 윙을 위한 다양한 옵션이 있기 때문에 큰 의미가 있다.

최신 버전의 컨피규레이터에서 flight plan 탭을 열어 비행기가 경유할 포인트를 받아온다.

각 포인트에는 위치 고도 속도 그 이외의 것이 존재하지만 위치 고도 속도만 사용한다.

1번에 설정된 위치 고도 속도는 즉시 레스큐 로직에서 타겟 목표로 설정한다. 

다만 고도를 조정하기 위한 상승률 하강률은 기존 레스큐로 제한된다.

속도 역시 기존 레스큐로 제한된다 . 레스큐에 사용하는 목표 속도가 최저 속도가 되며 쓰로들 역시

레스큐에서 사용하는 최대 최소값을 그대로 사용한다. 목표 속도는 컨피규레이터에서 받아와서 타겟으로 

삼지만 레스큐 로직에서 사용되는 여러가지 제한값을 적용 받는다.

GPS 가 있다면 flight plan은 계속 진행되며 종료시 레스큐로 넘긴다. 즉 홈 포인트로 이동한다.

GPS가 없으면 역시 종료하고 레스큐 로직으로 넘긴다. 한번 종료되면 자동으로 flight plan복귀는 안된다.

flight plan은 RX 신호가 붙었을때 발동되며 신호을 잃어도 flight plan은 유지 한다.

waypoint가 없을때 사용자가 aux 키로 Autopilot을 발동시키면 레스큐를 실행한다.

레스큐로 할당된 Aux키값 1000~1400 미만은 무한셔틀 모드  1400이상 1600미만은 Autopilot 을 실행하고 1600 값 이상은 레스큐로 실행한다.

어떤 비행 상태이던지 Autopilot이 실행되면 1번 point로 목적지를 고정해 비행하며 비행을 마치면 레스큐 첫 시작 단계부터 과정을 밟아 레스큐 규칙대로 실행된다.

Autopilot 어느 단계에서든지 Aux키 명령으로 다른 단계 ( 무한셔틀 또는 레스큐 )로 넘어갈수 있다.

상태 변화시 각 상태의 첫 단계부터 실행한다.

아래 코딩 계획 중 자연어 계획과 다른 경우 자연어 계획이 우선한다.

기타 자연어로 가정한것 이외의 예외 상황이 있으면 아래 코드를 따르고 그래도 예외 상황이 있으면 사용자에게 질문 후 확인한 다음 계획을 세우고 진행한다. 질문을 할때는 코딩에 대해 잘 모르는 사람으로 여기고 전문적인 이야기보다 최대한 자연어에 가깝게 질문한다.



( 계획대로 문서대로 시작했지만 Ram only 모드는 작동불가 - waypoint 저장시 리셋되기 때문에 작동 불가 그래서 EEPROME 저장 방식으로 바꿔야 했다.  대부분의 코드는 확인되었으며 버그 찾기만 남았다 )



> **전략:** 최신 Betaflight Configurator의 FlightPlan UI와 호환되게 사용하고,
> 이미 검증된 `gps_rescue.c`의 제어 루프를 100% 재사용한다.
> 새 모드, 새 제어기, position_estimator 이식 따위는 없다.
>
> **Mission 저장소(`mission.c/.h`)를 `gps_rescue.c`와 분리**하여 역할을 명확히 한다.
>
> **RAM-only 모드**를 기본 워크플로우로 사용하여 EEPROM 부담 없이 개발/디버깅한다.
>
> 참조 저장소:
>
> - 원격 펌웨어 소스 ( 대상아님 로컬저장소와 동일 ): `https://github.com/saydals/my-betaflight`
> - 최신 Configurator 원격저장소: `https://github.com/betaflight/betaflight-configurator`
> - 로컬 저장소 ( 코드 수정할 대상) :\\wsl.localhost\Ubuntu\home\betaflight\betaflight\src\main
> - 

---

## 1. Configurator FlightPlan 탭 동작 원리

### 1.1 탭 표시 조건

**Configurator** (`src/components/tabs/FlightPlanTab.vue`, 실제 경로 확인 완료):

```javascript
const fcHasFlightPlan = computed(() =>
    FC.CONFIG?.buildOptions?.includes("USE_FLIGHT_PLAN") ?? false
);
const canUseFC = computed(() => isConnected.value && fcHasFlightPlan.value);
```

→ 펌웨어의 `buildOptions`에 `"USE_FLIGHT_PLAN"` 문자열이 포함되어야 FlightPlan 탭이 활성화된다.

### 1.2 Configurator ↔ FC 통신 방식 (CLI 기반, MSP 불필요)

**Configurator** (`src/composables/useFlightPlan.js`)는 MSP를 전혀 사용하지 않고 **CLI 명령**으로만 통신한다:

| 동작               | CLI 명령                                                 |
| ------------------ | -------------------------------------------------------- |
| Waypoint 저장      | `waypoint clear` → `waypoint insert ...` (반복) → `save` |
| Waypoint 불러오기  | `waypoint list` (파싱)                                   |
| Waypoint 전체 삭제 | `waypoint clear` → `save`                                |

**MSP 명령어** (`MSPCodes.js`):

```javascript
MSP_WP: 118,      // Not used
MSP_SET_WP: 209,  // Not used
```

→ MSP_WP/MSP_SET_WP는 `// Not used` 상태이며, 실제 통신은 전적으로 CLI 기반이다.

### 1.3 Configurator가 전송하는 CLI 명령 형식

```
waypoint insert <index> <lat> <lon> <alt_ft> <speed_knots> <type> <duration_min> <pattern>
```

예시:

```
waypoint insert 0 37.1234567 127.1234567 400 10 FLYOVER 0 ORBIT
waypoint insert 1 37.2345678 127.2345678 300 15 FLYOVER 0 FIGURE8
waypoint clear
waypoint list
```

### 1.4 Configurator ↔ Firmware 타입 매핑 (검증 완료)

| Configurator 필드 | 단위        | CLI 예시                                                         |
| ----------------- | ----------- | ---------------------------------------------------------------- |
| latitude          | float (도)  | 37.1234567                                                       |
| longitude         | float (도)  | 127.1234567                                                      |
| altitude          | **feet**    | 400                                                              |
| speed             | **knots**   | 10                                                               |
| type              | 문자열      | FLYOVER, FLYBY, HOLD, LAND, TAKEOFF, ALT_CHANGE, DELAY, YAW_RATE |
| duration          | **minutes** | 0 (HOLD/DELAY용)                                                 |
| pattern           | 문자열      | ORBIT, FIGURE8                                                   |

**내부 변환 상수** (`useFlightPlan.js`):

```javascript
const FEET_TO_CM = 30.48;
const KNOTS_TO_CMS = 51.4444;
const MINUTES_TO_DECISECONDS = 600;
```

---

## 2. 전체 아키텍처

### 2.1 파일 구조

```
src/main/
├── flight/
│   ├── gps_rescue.c       ← 기존 파일, 최소 수정
│   ├── gps_rescue.h       ← 기존 파일, 최소 수정
│   ├── mission.c          ← [신규] 미션 저장소 + 제어 로직
│   └── mission.h          ← [신규] 미션 인터페이스
├── cli/
│   └── cli.c              ← CLI 명령 등록 (기존 파일 수정)
├── target/
│   └── common_fc_pre.h    ← USE_FLIGHT_PLAN 정의 추가
└── pg/
    └── mission.c          ← [신규, Appendix] EEPROM 저장용 PG
    └── mission.h          ← [신규, Appendix] PG 헤더
```

### 2.2 데이터 흐름

```
Configurator
    ↓ (CLI: "waypoint clear/insert/list")
CLI 핸들러 (cli.c)
    ↓
Mission 저장소 (mission.c)
    ├── missionWaypoints[]
    ├── missionWpCount
    └── currentMissionWpIndex
    ↓
MissionUpdate() + MissionAdvance()
    ↓ (타겟 좌표/고도 설정)
기존 gps_rescue.c 제어 루프 (gpsRescueUpdate → rescueAttainPosition)
    ↓
Servo / Motor 출력
```

### 2.3 호출 방향 (단방향, 상호 재귀 금지)

```
gpsRescueUpdate()
    ↓
    ├── [신규] mission mode 활성
    │       ├→ missionUpdateTargetOnly()  ← 타겟만 설정
    │       ├→ rescueState.phase = FLY_HOME
    │       ├→ performSanityChecks()
    │       ├→ rescueAttainPosition()     ← 기존 그대로
    │       └→ missionCheckAdvance()      ← 제어 후 CPA 체크 + WP 전환
    │
    └── [기존] else → 기존 rescue 상태머신 정상 동작
            ↓
    rescueAttainPosition()
```

## 3. 데이터 저장소 설계 (`mission.c/.h`)

### 3.1 `mission.h`

```c
// mission.h
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MAX_MISSION_WAYPOINTS 15

typedef enum {
    WP_TYPE_FLYOVER    = 0,
    WP_TYPE_FLYBY      = 1,
    WP_TYPE_HOLD       = 2,
    WP_TYPE_LAND       = 3,
    WP_TYPE_TAKEOFF    = 4,
    WP_TYPE_ALT_CHANGE = 5,
    WP_TYPE_DELAY      = 6,
    WP_TYPE_YAW_RATE   = 7,
} missionWpType_e;

typedef enum {
    WP_PATTERN_ORBIT    = 0,
    WP_PATTERN_FIGURE8  = 1,
} missionWpPattern_e;

typedef struct {
    int32_t  latitude;      // 1e-7 deg (Configurator float degree → 변환 저장)
    int32_t  longitude;     // 1e-7 deg (currentVCLat/Lon과 동일 포맷)
    float    altitude;      // cm (Configurator feet → * 30.48f 변환)
    float    speed;         // cm/s (Configurator knots → * 51.4444f 변환)
    missionWpType_e type;
    float    duration;      // deciseconds (Configurator minutes → * 600.0f 변환)
    missionWpPattern_e pattern;
} missionWaypoint_t;

extern missionWaypoint_t missionWaypoints[];
extern uint8_t missionWpCount;
extern uint8_t currentMissionWpIndex;

void missionInit(void);
void missionClear(void);
bool missionInsert(int idx, missionWaypoint_t *wp);
void missionList(void);
```

### 3.2 설계 결정

1. **RAM-only 기본**: 전역 배열만 사용. EEPROM 저장은 Appendix 참조
2. **cm 변환 저장**: Configurator feet → cm 변환 (gps_rescue.c가 cm 사용)
3. **gps_rescue.c와 분리**: mission 저장소는 CLI 핸들러와 mission 제어만 접근
4. **MAX_MISSION_WAYPOINTS = 15**: Configurator 제한과 일치

---

## 4. CLI 명령 핸들러

### 4.1 CLI 명령 등록

`cli.c`의 `cliCommandTable[]` 배열에 항목 추가:

```c
#ifdef USE_FLIGHT_PLAN
    { "waypoint",     cliWaypoint },
#endif
```

### 4.2 `cliWaypoint()` 핸들러

```c
static void cliWaypoint(cliCmdArgs_t *args)
{
    const char *cmd = cliArgAsString(args, 0);

    if (strcasecmp(cmd, "insert") == 0) {
        int idx = cliArgAsInt(args, 1);
        float latDeg = cliArgAsFloat(args, 2);   // float degrees (37.1234567)
        float lonDeg = cliArgAsFloat(args, 3);   // Configurator는 degree 전송
        float altFt = cliArgAsFloat(args, 4);
        float speedKnots = cliArgAsFloat(args, 5);
        const char *typeStr = cliArgAsString(args, 6);
        float durationMin = cliArgAsFloat(args, 7);
        const char *patternStr = cliArgAsString(args, 8);

        missionWaypoint_t wp;
        wp.latitude  = (int32_t)(latDeg * 1e7f);    // float deg → int32_t 1e-7 deg
        wp.longitude = (int32_t)(lonDeg * 1e7f);    // currentVCLat/Lon과 동일 포맷
        wp.altitude  = altFt * 30.48f;              // feet → cm
        wp.speed     = speedKnots * 51.4444f;       // knots → cm/s
        wp.type      = strToWpType(typeStr);
        wp.duration  = durationMin * 600.0f;        // min → ds
        wp.pattern   = strToWpPattern(patternStr);

        if (missionInsert(idx, &wp))
            cliPrintf("Waypoint %d inserted (count=%d)\r\n", idx, missionWpCount);
        else
            cliPrintError("Waypoint insert failed\r\n");

    } else if (strcasecmp(cmd, "list") == 0) {
        missionList();
    } else if (strcasecmp(cmd, "clear") == 0) {
        missionClear();
        cliPrintf("All waypoints cleared\r\n");
    } else {
        cliPrintError("Usage: waypoint insert|list|clear\r\n");
    }
}
```

---

## 5. `USE_FLIGHT_PLAN` 빌드 옵션

```c
// target.h 또는 common_fc_pre.h
#define USE_FLIGHT_PLAN
```

모든 mission 코드는 `#ifdef USE_FLIGHT_PLAN` / `#endif` 로 감싼다.

---

## 6. Mission 제어 루프 (`mission.c`)

```c
#ifdef USE_FLIGHT_PLAN

#include "mission.h"
#include "gps_rescue.h"
#include "io/gps.h"

static bool isMissionActive = false;
static int32_t missionTargetLat = 0;
static int32_t missionTargetLon = 0;
static float missionTargetAltCm = 0.0f;
static float missionTargetSpeedCmS = 0.0f;

bool missionIsActive(void) { return isMissionActive; }

void missionStart(void)
{
    if (missionWpCount == 0) { isMissionActive = false; return; }
    currentMissionWpIndex = 0;
    isMissionActive = true;
    missionApplyWaypoint();
}

void missionStop(void) { isMissionActive = false; }

static void missionApplyWaypoint(void)
{
    if (currentMissionWpIndex >= missionWpCount) { missionStop(); return; }
    missionWaypoint_t *wp = &missionWaypoints[currentMissionWpIndex];
    missionTargetLat   = wp->latitude;     // 이미 1e-7 deg로 저장됨
    missionTargetLon   = wp->longitude;    // currentVCLat/Lon과 동일 포맷
    missionTargetAltCm = wp->altitude;     // cm (실제 gps_rescue.c가 cm 사용 확인)
    missionTargetSpeedCmS = wp->speed;
}

void missionList(void)
{
    cliPrintf("Mission waypoints (%d):\r\n", missionWpCount);
    for (int i = 0; i < missionWpCount; i++) {
        missionWaypoint_t *wp = &missionWaypoints[i];
        float latDeg = (float)wp->latitude / 1e7f;
        float lonDeg = (float)wp->longitude / 1e7f;
        float altFeet = wp->altitude / 30.48f;
        float speedKnots = wp->speed / 51.4444f;
        float durationMin = wp->duration / 600.0f;
        cliPrintf("waypoint insert %d %.7f %.7f %.0f %.0f %s %.1f %s\r\n",
            i, latDeg, lonDeg, altFeet, speedKnots,
            wpTypeToStr(wp->type), durationMin, wpPatternToStr(wp->pattern));
    }
}

void missionAdvance(void)
{
    if (!isMissionActive) return;
    if (currentMissionWpIndex < missionWpCount) {
        currentMissionWpIndex++;
        missionApplyWaypoint();
    }
}

void missionUpdateTargetOnly(void)
{
    if (!isMissionActive || missionWpCount == 0) { missionStop(); return; }

    extern int32_t currentVCLat;
    extern int32_t currentVCLon;
    currentVCLat = missionTargetLat;
    currentVCLon = missionTargetLon;

    extern rescueState_t rescueState;
    GPS_distance_cm_bearing(&gpsSol.llh.lat, &gpsSol.llh.lon,
                            &currentVCLat, &currentVCLon,
                            &rescueState.intent.distanceToTargetCm,
                            &rescueState.intent.directionToTargetCd);
    rescueState.intent.targetAltitudeCm = missionTargetAltCm;
    rescueState.intent.targetVelocityCmS = MAX(
        missionTargetSpeedCmS,
        (float)gpsRescueConfig()->groundSpeedCmS
    );

    // ★ WP 전환은 여기서 하지 않음 → rescueAttainPosition() 후
}

bool missionCheckAdvance(void)
{
    if (!isMissionActive) return false;
    if (missionWpCount == 0) { missionStop(); return false; }

    extern rescueState_t rescueState;
    const float dCm = rescueState.intent.distanceToTargetCm;

    bool touchCPA = false;
    if (dCm < GPS_RESCUE_TOUCH_ACTIVATION_CM && dCm >= 0) {
        static float prevDistCm = -1.0f;
        static bool wasClosing = false;
        if (prevDistCm < 0) {
            prevDistCm = dCm; wasClosing = true;
        } else {
            bool isClosing = (dCm < prevDistCm - 20.0f);
            if (!isClosing && wasClosing) touchCPA = true;
            wasClosing = isClosing;
        }
        prevDistCm = dCm;
    }
    if (touchCPA || dCm < GPS_RESCUE_TOUCH_PROXIMITY_CM) {
        prevDistCm = -1.0f;
        missionAdvance();
        return true;
    }
    return false;
}

#endif
```

### 재사용 기존 함수/변수

| 이름                             | 위치         | 설명                     |
| -------------------------------- | ------------ | ------------------------ |
| `currentVCLat/Lon`               | gps_rescue.c | 타겟 좌표 (1e-7 도)      |
| `rescueState`                    | gps_rescue.c | 레스큐 상태              |
| `GPS_distance_cm_bearing()`      | io/gps.h     | 거리/방위각 계산         |
| `GPS_RESCUE_TOUCH_ACTIVATION_CM` | gps_rescue.c | CPA 활성화 거리 (2000cm) |
| `GPS_RESCUE_TOUCH_PROXIMITY_CM`  | gps_rescue.c | CPA 근접 폴백 (500cm)    |

---

## 7. 기존 `gps_rescue.c` 수정 사항

### 7.1 수정 원칙

- 기존 상태머신 건드리지 않음
- `rescuePhase_e` enum 값 변경 금지 (`RESCUE_DO_NOTHING` = 9)
- 미션 모드는 `gpsRescueUpdate()` 내에서 Aux 채널로 분기
- 미션 모드에서는 `RESCUE_FLY_HOME`으로 고정

### 7.2 `gpsRescueUpdate()` 변경

```c
void gpsRescueUpdate(void)
{
    sensorUpdate();

#ifdef USE_FLIGHT_PLAN
    if (missionIsActive()) {
        missionUpdateTargetOnly();        // 타겟만 설정 (WP 전환 X)
        rescueState.phase = RESCUE_FLY_HOME;
    } else {
#endif

    switch (rescueState.phase) { /* 기존 12단계 그대로 */ }

    performSanityChecks();
    rescueAttainPosition();              // ← 여기서 현재 타겟으로 제어 실행
    newGPSData = false;

#ifdef USE_FLIGHT_PLAN
    }

    // ★ 타겟 제어가 이루어진 후 CPA 체크 + WP 전환 (단방향 흐름 유지)
    if (missionIsActive()) {
        missionCheckAdvance();
    }
#endif
}
```

### 7.3 `rescueAttainPosition()` 수정 없음

**절대 금지: `rescueAttainPosition()` 내에서 `missionUpdate()` 호출 금지!**

### 7.4 `gps_rescue.h` 변경 불필요

getter가 이미 존재. mission.c에서 `extern` 참조.

---

## 8. 호출 구조 (단방향)

```
gpsRescueUpdate()
  ├→ sensorUpdate()
  │
  ├→ [MISSION]
  │    ├→ missionUpdateTargetOnly()   ← 타겟만 설정 (WP 전환 X)
  │    ├→ phase = RESCUE_FLY_HOME
  │    ├→ performSanityChecks()
  │    ├→ rescueAttainPosition()      ← 현재 타겟으로 제어 실행
  │    └→ missionCheckAdvance()       ← 제어 후 CPA 체크 + WP 전환
  │
  ├→ [NORMAL]
  │    ├→ switch(phase)               ← 기존 12단계
  │    ├→ performSanityChecks()
  │    └→ rescueAttainPosition()
  │
  └→ newGPSData = false
```

**금지: `rescueAttainPosition()` ↔ `missionUpdateTargetOnly()` 상호 재귀**

---

## 9. 예외 처리 정책

### 9.1 상황별 동작 표

| 상황          | 동작                                                                                      |
| ------------- | ----------------------------------------------------------------------------------------- |
| Waypoint 없음 | 미션 불가. GPS Rescue 정상 실행                                                           |
| 모든 WP 완료  | **[분기]** Home Fix O → Home 귀환 / No Home Fix → 마지막 WP 선회 유지                     |
| RX Loss       | Mission 계속. failsafe가 Rescue 트리거 시 중단                                            |
| GPS Loss      | 기존 Rescue 정책 (`performSanityChecks()`가 처리)                                         |
| Aux off       | 미션 중단. Rescue 상태머신으로 복귀                                                       |
| No Home Fix   | Mission 실행 가능. 완료 후 마지막 WP에서 선회 유지                                        |
| Home Fix O    | Mission 실행 가능. 완료 후 Home 귀환 (`RESCUE_FLY_HOME`)                                  |
| 배터리 부족   | Mission 중단 → Home 귀환. 배터리 센서 미장착 시 자동 패스. `mission_battery_min` CLI 설정 |

### 9.2 미션 활성화 조건

Aux 채널 값이 특정 범위(예: 1500~1700)일 때 미션 모드 진입.

```c
bool missionIsActivatedByAux(void)
{
    const uint16_t auxValue = getRescueAuxValue();
    return (auxValue >= missionAuxMin && auxValue <= missionAuxMax);
}
```

### 9.3 Home Fix 유무에 따른 Mission 완료 후 분기

```c
void missionStop(void)
{
    isMissionActive = false;

    if (STATE(GPS_FIX_HOME)) {
        // Home point 있음 → Home 귀환
        rescueState.phase = RESCUE_FLY_HOME;
    } else {
        // Home point 없음 → 마지막 WP에서 선회 유지
        // (기존 무한셔틀과 동일한 원리, GPS만 있으면 가능)
        rescueState.phase = RESCUE_SHUTTLE_INFINITE;
    }

    // 또는 CLI 설정으로 사용자 선택 가능:
    // mission_complete_action = 0 (FLY_HOME), 1 (SHUTTLE_INFINITE), 2 (DO_NOTHING)
}
```

### 9.4 Pre-arm 안전 체크

Mission 모드는 **기존 Rescue 시스템의 일부**이므로, Rescue가 arming을 차단하는 로직을 그대로 활용한다:

```
Aux → Rescue 모드 활성화 (RESCUE_IDLE 외) → arming 불가
```

즉, Mission/Autopilot용 Aux가 켜져 있는 상태에서는 이륙이 불가능하다. 별도 코드 불필요.

### 9.5 WP Skip Timer (WP 정체 시간 초과 시 강제 전환)

강한 바람이나 GPS drift로 현재 WP에 오래 머무르면 배터리만 소진된다.
기존 Rescue의 `ATTAIN_ALT_TIMEOUT_US` (3초) 패턴과 동일한 방식:

```c
// missionCheckAdvance() 내부 (mission.c)
#define MISSION_WP_TIMEOUT_US 300000000  // 5분

static timeUs_t wpEntryTime = 0;

// WP 인덱스 변경 시 시간 기록
if (wpEntryTime == 0) {
    wpEntryTime = micros();
}

// 5분 초과 시 강제 Skip (실패로 간주하고 다음 WP로)
if (cmpTimeUs(micros(), wpEntryTime) > MISSION_WP_TIMEOUT_US) {
    cliPrintf("Mission: WP %d timeout, skip to next\r\n", currentMissionWpIndex);
    missionAdvance();
    wpEntryTime = micros();  // 새 WP 타이머 시작
    return true;
}
```

### 9.6 배터리 저전압 귀환

장거리 미션에서 배터리 방전은 치명적이다. `gpsRescueUpdate()` 내에서 체크:

```c
// gpsRescueUpdate() 내부, missionCheckAdvance() 전에
#ifdef USE_FLIGHT_PLAN
if (missionIsActive()) {
    // 배터리 센서가 장착되어 있을 때만 체크
    if (featureIsEnabled(FEATURE_BATTERY) || featureIsEnabled(FEATURE_CURRENT_METER)) {
        if (getBatteryState() != BATTERY_OK) {
            missionStop();
            rescueState.phase = RESCUE_FLY_HOME;  // 즉시 Home 귀환
            return;  // 이번 사이클은 제어 루프로
        }
    }
}
#endif
```

배터리 센서가 아예 없는 기체(FEATURE 비활성화)는 자동으로 패스된다.

### 9.7 Waypoint 유효성 검사 (초기에는 미구현)

| 상황                        | 초기 동작                   | 향후 개선                        |
| --------------------------- | --------------------------- | -------------------------------- |
| 도달 불가능한 WP (100km)    | 그대로 비행                 | WP 간 최대 거리 검증             |
| 해상 WP (육상 기체)         | 그대로 비행                 | 지형 데이터 검증 불가 (MCU 한계) |
| 모든 WP 완료 후 Home 미설정 | 마지막 WP 선회              | -                                |
| WP 좌표가 (0,0)             | 위험. Configurator에서 방지 | 펌웨어에서 유효성 검증           |

---

## 10. 단위 변환

### gps_rescue.c 내부 단위 (cm 기준)

| 변수                 | 단위    |
| -------------------- | ------- |
| `targetAltitudeCm`   | cm      |
| `targetVelocityCmS`  | cm/s    |
| `distanceToTargetCm` | cm      |
| `currentVCLat/Lon`   | 1e-7 도 |

### Configurator ↔ 내부 변환 (양방향)

| 방향   | 대상                       | 변환식                                | 사용 위치         |
|:------ |:-------------------------- |:------------------------------------- |:----------------- |
| → 내부 | float 도 → int32_t 1e-7 도 | `(int32_t)(latDeg * 1e7f)`            | CLI insert 핸들러 |
| → 내부 | feet → cm                  | `altCm = altFeet * 30.48f`            | CLI insert 핸들러 |
| → 내부 | knots → cm/s               | `speedCmS = speedKnots * 51.4444f`    | CLI insert 핸들러 |
| → 내부 | minutes → ds               | `durationDs = durationMin * 600.0f`   | CLI insert 핸들러 |
| ← 출력 | int32_t 1e-7 도 → float 도 | `latDeg = (float)wp->latitude / 1e7f` | `missionList()`   |
| ← 출력 | cm → feet                  | `altFeet = wp->altitude / 30.48f`     | `missionList()`   |
| ← 출력 | cm/s → knots               | `speedKnots = wp->speed / 51.4444f`   | `missionList()`   |
| ← 출력 | ds → minutes               | `durationMin = wp->duration / 600.0f` | `missionList()`   |

### 고도 단위 검증 (필수 확인 사항)

**실제 `e:\task\gps_rescue.c` 코드 확인 결과:**

| 변수명                                       | 단위     | 결론               |
| -------------------------------------------- | -------- | ------------------ |
| `rescueState.intent.targetAltitudeCm`        | **cm** ✅ | 변수명 `Cm` 접미사 |
| `rescueState.sensor.currentAltitudeCm`       | **cm** ✅ | 변수명 `Cm` 접미사 |
| `rescueState.intent.targetLandingAltitudeCm` | **cm** ✅ | 변수명 `Cm` 접미사 |

→ **고도는 cm가 맞다.** `missionTargetAltCm`을 `rescueState.intent.targetAltitudeCm`에 직접 대입해도 단위 불일치 없음.

> ⚠️ **코딩 전 반드시 확인:** 사용 중인 `gps_rescue.c`의 `handleFlyHomePhase()` 또는 `calculateAltitudePitch()`(존재 시) 내부에서 `targetAltitudeCm` 변수를 **직접 읽는 코드**를 눈으로 확인할 것. cm를 m로 나누는 코드가 있다면 plan 수정 필요.

---

## 11. 개발 단계: RAM-only

- 전역 배열 (RAM)만 사용, EEPROM 저장 없음
- 전원 사이클 시 미션 소멸
- EEPROM 전환은 `missionWaypoints[]` → `missionConfig()->wp[]` 만 변경

---

## 12. 초기 구현 전략

### Waypoint Type: 무조건 FLYOVER

```c
missionWpType_e strToWpType(const char *str)
{
    (void)str;
    return WP_TYPE_FLYOVER;
}
```

LAND·HOLD 등은 추후 switch로 확장.

## 13. OSD 표시

### 13.1 기존 OSD 코드 분석

현재 `osdElementReadyMode()` (`OSD_READY_MODE` 요소)가 GPS Rescue 상태를 표시한다:

| Rescue 상태                       | OSD 표시     | 소스 위치                  |
| --------------------------------- | ------------ | -------------------------- |
| RESCUE_INITIALIZE                 | "READY"      | `osd_elements.c:1098`      |
| RESCUE_ATTAIN_ALT                 | "CLIMB"      | `osd_elements.c:1101`      |
| RESCUE_FLY_HOME                   | "FLY HOME"   | `osd_elements.c:1104`      |
| RESCUE_SHUTTLE / SHUTTLE_INFINITE | "SHUT-%03u"  | `osd_elements.c:1107-1112` |
| RESCUE_SHUTTLE_DESCENT            | "S-DESC"     | `osd_elements.c:1114`      |
| RESCUE_DESCENT                    | "DESCEND"    | `osd_elements.c:1117`      |
| RESCUE_LANDING                    | "LANDING"    | `osd_elements.c:1120`      |
| RESCUE_ABORT                      | "ABORT"      | `osd_elements.c:1123`      |
| RESCUE_COMPLETE                   | "COMPLETE"   | `osd_elements.c:1126`      |
| RESCUE_DO_NOTHING                 | "DO NOTHING" | `osd_elements.c:1129`      |
| GPS 미고정                        | "SAT x/y"    | `osd_elements.c:1139`      |
| 홈 미설정                         | "NO HOME"    | `osd_elements.c:1144`      |
| Rescue 사용 불가                  | "RES UNV"    | `osd_elements.c:1147`      |
| Rescue 준비 완료                  | "RES OK"     | `osd_elements.c:1149`      |

### 13.2 Mission 모드 OSD 표시 설계

Mission(autopilot) 실행 중에는 기존 Rescue 상태 표시 대신 **"AUTO-1/4"** 형식으로 표시한다:

```
AUTO-{현재WP}/{전체WP}
```

예:

- `AUTO-1/4` — 전체 4개 웨이포인트 중 1번째로 비행 중
- `AUTO-3/4` — 전체 4개 웨이포인트 중 3번째로 비행 중
- `AUTO-4/4` — 마지막 웨이포인트로 비행 중

### 13.3 구현 방식

`osdElementReadyMode()` 내에서 **Rescue 체크보다 먼저** Mission 모드를 검사한다:

```c
static void osdElementReadyMode(osdElementParms_t *element)
{
    // 1. BOXREADY 체크 (기존)
    if (IS_RC_MODE_ACTIVE(BOXREADY) && !ARMING_FLAG(ARMED)) {
        strcpy(element->buff, "READY");
        return;
    }

#ifdef USE_FLIGHT_PLAN
    // 2. [신규] Mission 모드 → Rescue보다 먼저 체크
    if (missionIsActive()) {
        tfp_sprintf(element->buff, "AUTO-%d/%d",
            currentMissionWpIndex + 1,   // 0-based → 1-based
            missionWpCount);
        return;
    }
#endif

#ifdef USE_GPS_RESCUE
    // 3. 기존 Rescue 상태 표시 (변경 없음)
    const rescuePhase_e phase = gpsRescueGetPhase();
    if (phase != RESCUE_IDLE) {
        switch (phase) {
        case RESCUE_INITIALIZE:
            strcpy(element->buff, "READY"); break;
        case RESCUE_ATTAIN_ALT:
            strcpy(element->buff, "CLIMB"); break;
        case RESCUE_FLY_HOME:
            strcpy(element->buff, "FLY HOME"); break;
        case RESCUE_SHUTTLE:
        case RESCUE_SHUTTLE_INFINITE:
            tfp_sprintf(element->buff, "SHUT-%03u",
                gpsRescueGetCurrentShuttleTrips());
            break;
        // ... 나머지는 기존과 동일 ...
        }
    } else {
        // GPS 상태 표시 (기존)
    }
#endif
}
```

### 13.4 핵심 설계 원칙

| 원칙                   | 설명                                                                         |
| ---------------------- | ---------------------------------------------------------------------------- |
| **Mission 우선**       | Mission 모드는 Rescue보다 먼저 체크하여 "AUTO-1/4" 표시                      |
| **Fallback**           | Mission 종료 시 자동으로 기존 Rescue 상태("FLY HOME") 표시                   |
| **인덱스 변환**        | `currentMissionWpIndex`(0-based) → `+ 1`로 1-based 표시                      |
| **새 OSD 요소 불필요** | 기존 `OSD_READY_MODE` 요소에 조건 분기만 추가                                |
| **버퍼 안전**          | "AUTO-12/12" 최대 11자. 기존 "SHUT-%03u"(9자)보다 약간 크지만 buffer 범위 내 |

### 13.5 표시 예시

| 비행 상태                       | OSD 표시   |
| ------------------------------- | ---------- |
| Mission 비활성 + Rescue idle    | "RES OK"   |
| Mission 비활성 + Rescue 상승 중 | "CLIMB"    |
| Mission 활성, WP 0/4 비행 중    | "AUTO-1/4" |
| Mission 활성, WP 3/4 비행 중    | "AUTO-4/4" |
| Mission 완료, Home 귀환 중      | "FLY HOME" |

---

## 14. Pre-arm Wiggle 강화 (Waypoint/GPS 상태 시각화)

### 14.1 기존 코드 분석

현재 위글 시스템은 `main/flight/servos.c`에 구현되어 있다:

| 파일                   | 위치     | 내용                                                            |
| ---------------------- | -------- | --------------------------------------------------------------- |
| `main/flight/servos.c` | L748-794 | `updateReadyToArmWiggle()` — 위글 활성화/비활성화 상태 관리     |
| `main/flight/servos.c` | L796-803 | `getReadyToArmWiggleOffset()` — 사인파 오프셋 계산              |
| `main/flight/servos.c` | L857-863 | `servoMixer()`에서 `input[INPUT_STABILIZED_ROLL]`에 오프셋 주입 |
| `main/cli/settings.c`  | L981     | `ready_to_arm_wiggle_hz` CLI 설정 (0=OFF, 1~6Hz)                |
| `main/flight/servos.h` | L139     | `ready_to_arm_wiggle_hz` 구조체 필드                            |

**현재 동작:**

- Arming 가능 상태에서 10초 주기로 1초간 위글
- **에일러론(Roll)에만** 오프셋 주입
- Arming 불가 / 스틱 조작 시 중단

### 14.2 강화된 Wiggle 패턴 ( 아밍 중에는 발동 안하는것을 꼭 확인해야 )

| 상태                                  | 에일러론 (Roll) | 엘리베이터 (Pitch)                  | 의미                              |
| ------------------------------------- | --------------- | ----------------------------------- | --------------------------------- |
| **①** Arming 준비만 (기존)            | ✅ 위글          | ❌                                   | "기본 Rescue 준비 상태"           |
| **②** Arming 준비 + GPS Fix + WP 없음 | ✅ 위글          | ✅ 위글                              | "GPS OK, 미션 없음 (Rescue 모드)" |
| **③** Arming 준비 + GPS Fix + WP 있음 | ✅ 위글          | ✅ 위글 → 1초 후 엘리베이터만 재위글 | "모든 준비 완료"                  |

아밍 준비 - 엘리베이터만 위글 ( 기존 )

아밍 준비 + GPS Fix ( 이건 레스큐 조건에서 사용자가 입력한 GPS 숫자 이상으로 잡혔을때를 말함 )

아밍 준비 +  Waypoint값 존재 ( 기존 위글 작동 시간 이후 바로 1초간 엘리베이터를 위글 )

아밍 준비 Waypoint값 존재하는 데 GPS갯수 불만족시 에일러론 단독 위글 후 엘리베이터 위글

마지막 조건은 따로 설정을 안해도 자연스럽게 자동으로 작동함.

## 부록: 실제 소스 코드 증거

### A. `gps_rescue.h` rescuePhase_e (실제)

```c
RESCUE_IDLE=0, RESCUE_INITIALIZE=1, RESCUE_ATTAIN_ALT=2,
RESCUE_FLY_HOME=3, RESCUE_SHUTTLE=4, RESCUE_SHUTTLE_INFINITE=5,
RESCUE_SHUTTLE_DESCENT=6, RESCUE_DESCENT=7, RESCUE_LANDING=8,
RESCUE_DO_NOTHING=9, RESCUE_ABORT=10, RESCUE_COMPLETE=11
```

### B. `gpsRescueUpdate()` 실제 구조

```
gpsRescueUpdate() (line ~1393)
  ├── sensorUpdate()
  ├── switch(rescueState.phase) { 12단계 }
  ├── performSanityChecks()
  ├── rescueAttainPosition()
  └── newGPSData = false
```

### C. `rescueAttainPosition()` 실제 (line 933)

```c
static void rescueAttainPosition(void) {
    switch (rescueState.phase) {
        case RESCUE_IDLE: ... return;
        case RESCUE_INITIALIZE: ... return;
        case RESCUE_DO_NOTHING: handleDoNothingPhase(); break;
        case RESCUE_ATTAIN_ALT: handleAttainAltPhase(); break;
        case RESCUE_FLY_HOME:   handleFlyHomePhase();   break;
        case RESCUE_SHUTTLE:
        case RESCUE_SHUTTLE_INFINITE: handleShuttlePhase(); break;
        case RESCUE_SHUTTLE_DESCENT:  handleShuttleDescentPhase(); break;
        case RESCUE_DESCENT:    handleDescentPhase();   break;
        case RESCUE_LANDING:    handleLandingPhase();   break;
        default: break;
    }
    rescueYaw = lastRescueYaw * 0.7f + rescueYaw * 0.3f;
}
```

### D. Configurator `useFlightPlan.js`

```javascript
const TYPE_TO_CLI = { flyover:"FLYOVER", flyby:"FLYBY", hold:"HOLD",
    land:"LAND", takeoff:"TAKEOFF", alt_change:"ALT_CHANGE",
    delay:"DELAY", yaw_rate:"YAW_RATE" };
const FEET_TO_CM = 30.48;
const KNOTS_TO_CMS = 51.4444;
const MINUTES_TO_DECISECONDS = 600;
```

### E. MSPCodes.js

```javascript
MSP_WP: 118,      // Not used
MSP_SET_WP: 209,  // Not used
```

---

## 변경 이력

| 버전 | 날짜       | 변경 내용                                                                            |
| ---- | ---------- | ------------------------------------------------------------------------------------ |
| v1   | -          | 초안 (검증 전)                                                                       |
| v2   | 2026-07-03 | 검증 완료 후 재작성. enum 수정, mission 분리, 단방향 호출, 단위 검증, 실제 증거 추가 |

### 추가 확인 사항 ( 특정 AI가 이 계획서 검토 후 지적 사항이며 맞는지 확인 해 봐야함 )

클로드 sonet 5 의견.

### 🔴 반드시 코딩 전 해결해야 할 문제

**1. RESCUE_ABORT 덮어쓰기 문제 (문서 자체가 지적했지만 아직 코드에 반영 안 됨)**

866줄 이후에 스스로 지적한 문제가 맞습니다. 다만 정확히 짚자면: `performSanityChecks()`가 매 루프 재평가되는 무상태(stateless) 함수라면 같은 루프 안에서는 문제 없이 ABORT가 즉시 반영됩니다. 진짜 위험은:

- `performSanityChecks()`가 **디바운스/래칭(latching) 로직**을 갖고 있어서 "연속 N회 이상 이상 감지 시에만 ABORT"하는 방식이라면, 매 루프 `phase = RESCUE_FLY_HOME`으로 강제 리셋하는 게 그 디바운스 카운터/상태 전이 감지 로직을 방해할 수 있습니다.
- 제안된 수정(`if (phase == RESCUE_ABORT) missionStop()`)은 ABORT만 체크하는데, 실제 `performSanityChecks()`가 ABORT 외에 다른 phase(예: DO_NOTHING, LANDING 강제 전환 등)로도 보낼 수 있는지 **실제 소스를 열어서 확인**해야 합니다. ABORT만 막고 다른 강제 전환 케이스를 놓치면 같은 버그가 다른 이름으로 재발합니다.

→ 코딩 전에 실제 `performSanityChecks()` 함수 본문을 보고 "이 함수가 phase를 몇 가지 값으로, 어떤 조건에서 바꾸는지" 전부 나열하고, 그 각각에 대해 mission 강제 리셋이 안전한지 하나씩 확인하세요.

**2. 9.3(missionStop 분기)과 9.6(배터리 저전압) 코드가 서로 충돌**

- 9.3: Home Fix 없으면 `RESCUE_SHUTTLE_INFINITE`(선회 유지), 있으면 `RESCUE_FLY_HOME`
- 9.6: 배터리 부족 시 Home Fix 여부와 무관하게 무조건 `RESCUE_FLY_HOME`으로 강제

Home Fix가 없는 상태에서 배터리까지 부족하면 9.6이 이겨서 **유효하지 않은 Home 좌표(0,0 근처일 수 있음)로 직진**하게 됩니다. 9.6은 `missionStop()`을 호출하되 phase는 `missionStop()`이 정하도록 위임해야 합니다(직접 `rescueState.phase = RESCUE_FLY_HOME` 줄 삭제).

또한 9.6의 `return;`이 `gpsRescueUpdate()` 최상위에서 바로 리턴하는데, 이 경우 `newGPSData = false` 리셋을 건너뜁니다. GPS 패킷이 "처리 안 됨" 상태로 남아 다음 루프 로직에 영향 줄 수 있으니, return 전에 `newGPSData = false` 처리 확인 필요.

**3. `missionCheckAdvance()`의 static 변수 미초기화 — 체크리스트에만 있고 코드엔 없음**

897줄 체크리스트 2번(필수)이 맞는 지적인데, **6장의 실제 `missionStart()` 코드(286~292줄)에는 아직 반영이 안 되어 있습니다.** 문서 자체가 "적용 안 됨"을 인지하고 있으니, 코딩 착수 전에 `missionStart()`에 `prevDistCm`/`wasClosing`/`wpEntryTime` 리셋을 실제로 추가하는 걸 잊지 마세요. (지금 초안 그대로 옮기면 두 번째 미션부터 첫 WP를 건너뛰는 버그가 그대로 재현됩니다.)

**4. WP Skip Timer가 정상 전환 시 리셋 안 됨**

9.5 코드는 타임아웃으로 skip할 때만 `wpEntryTime = micros()`를 갱신합니다. 정상적으로 CPA 터치해서 `missionAdvance()`가 호출되는 경로(6장 `missionCheckAdvance()` 375~379줄)에서는 `wpEntryTime`이 리셋되지 않습니다. 즉 WP1→WP2가 30초 만에 끝나도 타이머는 계속 흐르고 있어서, WP2가 실제로는 4분 30초만 머물러도 "5분 경과"로 잘못 판정될 수 있습니다. `missionAdvance()` 내부(또는 정상 전환 분기)에도 `wpEntryTime = micros()` 리셋을 추가해야 합니다.

### 🟡 코딩 전 확인이 필요한 부분

- **`missionInsert()`의 bounds 검증**: 헤더에만 선언되어 있고 구현이 안 보입니다. `idx`가 `MAX_MISSION_WAYPOINTS(15)`를 벗어나는 값이 CLI로 들어올 때 배열 밖 쓰기가 발생하지 않도록 반드시 구현부에서 범위 체크하세요.
- **비행 중 CLI로 waypoint insert/clear 가능 여부**: 9.4는 "Aux 켜져 있으면 arming 불가"만 다루는데, Aux가 꺼진 채로 비행 중(수동 조종 중) CLI 명령이 들어와 미션 테이블이 바뀌는 걸 막는 로직이 없습니다. `ARMING_FLAG(ARMED)` 체크를 `cliWaypoint()` insert/clear 핸들러에 추가하는 걸 권장합니다. ( 사용자 요구 사항 : 비행중 CLI 입력으로 Autopilot 관련 사항 변경은 불가능 하다 )
- **missionIsActivatedByAux() → missionStart()/Stop() 연결 코드 누락**: 9.2에 조건 판별 함수는 있는데, 이걸 실제로 `gpsRescueUpdate()` 어디서 호출해서 `missionStart()`를 트리거하는지 6~7장 어디에도 없습니다. Aux on/off 엣지 감지(rising/falling edge) 로직을 추가로 설계해야 합니다.
- **14장 Pre-arm Wiggle**: 다른 장에 비해 논리가 자연어 서술 위주로 느슨합니다("마지막 조건은 자연스럽게 자동으로 작동함" 등). 실제 상태 전이 표(우선순위, 타이머 리셋 조건)를 6~9장 수준으로 명시적 pseudocode화하지 않으면 구현 단계에서 애매한 판단이 생길 가능성이 높습니다.

### 정리 — 코딩 착수 전 우선순위

2. `performSanityChecks()` 실제 소스 열어서 phase 전이 케이스 전부 나열 → mission 강제 리셋과 충돌 여부 확인
3. 9.3/9.6 배터리 로직 통합(missionStop에 위임, phase 직접 대입 제거) + newGPSData 리셋 위치 확인
4. missionStart()에 static 변수 초기화 실제 반영
5. missionAdvance() 정상 경로에도 wpEntryTime 리셋 추가
6. missionInsert() bounds 체크 + ARMED 상태에서 CLI 편집 차단
7. Aux 엣지 감지 → missionStart/Stop 연결 코드 작성

핵심 아키텍처(분리, 단방향 호출, RAM-only 우선)는 그대로 가져가도 좋고, 위 6가지는 "설계 변경"이 아니라 "누락된 배선(wiring)과 경계 조건" 보완이라 큰 재작업 없이 v3로 반영 가능해 보입니다.

---

glm 5.2 의견

### Sanity Check(안전성 검사) 강제 덮어쓰기 문제

**"GPS가 해제되면 비행 정지가 되니까 문제없어 보인다"**

**절반은 맞고, 절반은 위험합니다.**

* **맞는 점:** GPS 신호가 아예 끊어져서 `STATE(GPS_FIX)`가 완전히 꺼지면, `performSanityChecks()`가 이를 감지하고 결국 비행을 멈추거나 이전 상태로 떨어뜨릴 것입니다.

* **위험한 점 (오작동 시나리오):** 실제 비행 중 GPS가 '완전히 끊기는' 경우보다 **'노이즈를 뿜거나 잠깐 불안정해지는(Glitch)' 경우가 훨씬 많습니다.** 
  계획서의 코드 흐름을 보면:

  ```c
  missionUpdateTargetOnly();        // 1. 무조건 FLY_HOME으로 세팅
  rescueState.phase = RESCUE_FLY_HOME;
  // ...
  performSanityChecks();            // 2. 여기서 문제 감지해도...
  rescueAttainPosition();           // 3. 이번 루프는 그냥 지나감
  ```

  만약 GPS 노이즈로 인해 `performSanityChecks()` 내부에서 "위험하다, ABORT 혹은 IDLE로 돌아가라"라고 플래그를 세웠다고 해도, **1ms 단위로 도는 다음 루프의 첫 줄에서 다시 `RESCUE_FLY_HOME`으로 강제 복구**됩니다.
  즉, 기체가 미세하게 흔들리면서 계속 목적지를 향해 돌진하는 **"상태머신 무시 폭주"** 상태가 될 수 있습니다.

**💡 권장 수정:** `missionUpdateTargetOnly()` 맨 앞에 `if (rescueState.phase == RESCUE_ABORT) { missionStop(); return; }` 같은 최소한의 안전장치를 두는 것이 좋습니다.

### 계획서 v2를 코드로 옮기기 전 최종 체크리스트

1. **[필수]** `missionStop()` 시 원래 Home 좌표 복원 로직 추가.
2. **[필수]** `missionCheckAdvance()` 내부의 `static` 변수들(`prevDistCm` 등), `missionStart()`에서 초기화 추가 (안 하면 두 번째 비행부터 첫 WP를 씹어버림).
3. **[권장]** `missionUpdateTargetOnly()` 진입 시 `rescueState.phase == RESCUE_ABORT` 인지 체크하여 미션 자동 중단 로직 1줄 추가.

## 아래 사항은 나중을 위한 아이디어 차원이며 이번 코딩에는 적용하지 않는다.

고도를 고려하지 않은 사용자의 비행 계획에 따른 추락은 사용자 책임이다.

추가 계획.. 포인트간 높이차가 너무 커서 비행기가  직진해서 고도 도달하기 어려울 경우

현재 내가 사용하는 셔틀 하강을 이용한다. 추가로 셔틀 상승도 필요하다.

셔틀 하강은 원래 너무 높은 고도로 홈 근처 도달시 임의로 두 지점 AB를 왕복하면서 천천히 고도를 낮춘후

목표 고도에 도달하면 홈으로 가는 코드다.

 **"언제 B포인트가 필요한지 판단하는 함수"**의 핵심 수학 로직

### 🧮 향후 구현을 위한 "B포인트 판단 함수" 로직

비행기가 안전하게 올라갈 수 있는 **최대 상승 각도(예: 15도)**를 기준

```c
#define MAX_SAFE_CLIMB_ANGLE_DEG 15.0f

bool isClimbShuttleRequired(int32_t wp3_lat, int32_t wp3_lon, float wp3_alt, 
                            int32_t wp4_lat, int32_t wp4_lon, float wp4_alt) 
{
    // 1. 두 WP 간의 고도 차이와 수평 거리 계산
    float altDiff = wp4_alt - wp3_alt; 
    if (altDiff <= 0) return false; // 고도를 내리거나 같으면 셔틀 불필요 (직행)

    uint32_t distCm;
    int32_t bearing;
    GPS_distance_cm_bearing(&wp3_lat, &wp3_lon, &wp4_lat, &wp4_lon, &distCm, &bearing);

    float distM = distCm / 100.0f;

    // 2. 직행 시 필요한 상승 각도 계산 (atan 사용)
    float requiredAngleRad = atan2f(altDiff / 100.0f, distM);
    float requiredAngleDeg = requiredAngleRad * (180.0f / M_PI);

    // 3. 판단
    if (requiredAngleDeg > MAX_SAFE_CLIMB_ANGLE_DEG) {
        return true;  // 각도가 너무 가파름! -> B포인트 생성 후 셔틀 상승 필요
    }
    return false;     // 안전한 각도임 -> 3번에서 4번으로 직행
}
```

### 💡 B포인트 자동 생성 팁 (나중을 위해)

만약 `true`가 나와서 B포인트를 생성해야 한다면, 

* **B포인트 위치:** 3번 WP에서 4번 WP 방향(Bearing)으로, `안전거리(고도차 / tan(15도))` 만큼만 떨어진 지점.
* **동작:** 3번 -> B포인트(직행) -> B포인트 도착 후 상승 셔틀 -> 고도 도달 시 4번으로 직행.
