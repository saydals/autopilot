# Waypoint 고도 전환 방식 수정 지시서 — 즉시 변경 → 점진적 글라이드 슬로프

## 0. 목적 (반드시 먼저 읽을 것)

**현재 동작(버그로 취급할 문제)**
- Waypoint가 새로 활성화되는 순간, 목표 고도(`rescueState.intent.targetAltitudeCm`)가 그 waypoint의 최종 고도값(`wp->altitude`)으로 **즉시** 설정된다.
- 이로 인해 고도 오차(`altErrM`)가 한 번에 크게 벌어지고, 기존 고도 PID(`calculateAltitudePitch()`)가 즉시 포화(saturate)되어 **`maxRescueAngle`(또는 `MAX_PITCH_FLYHOME_DEG`) 한계치의 피치각**으로 상승/하강을 시도한다.
- 결과: waypoint 사이 거리와 무관하게 무조건 "최대 각도로 급상승/급하강 후 수평 비행" 패턴이 나온다.

**원하는 동작(이번 수정의 목표)**
- 새 waypoint가 활성화된 "그 순간의 현재 위치/고도"와 "그 waypoint의 목표 위치/고도"를 잇는 가상의 직선(빗변)을 그린다.
- 비행기가 그 waypoint를 향해 수평 거리를 좁혀가는 것에 **비례**해서 목표 고도도 선형으로 보간(interpolate)되도록 만든다.
- 즉, `(수평거리, 고도차)`를 직각삼각형의 밑변·높이로 보고, 빗변(직선 경로)을 따라 고도가 점진적으로 변하게 한다.
- 기존 고도 PID(`calculateAltitudePitch`)는 손대지 않는다. PID에 매 루프 들어가는 "목표 고도"만 즉시값 → 보간값으로 바꾸면, PID는 항상 작은 오차만 쫓아가게 되어 자연히 완만한 피치각을 유지하게 된다.

**수정 범위**
- 오직 **미션 웨이포인트 비행 중 목표 고도 계산 로직**만 수정한다 (`src/main/flight/mission.c`).
- `gps_rescue.c`의 PID/피치 계산 함수(`calculateAltitudePitch`)는 **절대 수정하지 않는다.**
- GPS Rescue의 홈 귀환(FLY_HOME), 하강(DESCENT), 셔틀(SHUTTLE) 로직도 이번 수정 대상이 아니다. 순수하게 "미션 웨이포인트 사이 이동 중" 로직만 건드린다.

---

## 1. 수정 대상 파일

```
src/main/flight/mission.c
```

이 파일 외에는 어떤 파일도 수정하지 않는다. (`mission.h`, `gps_rescue.c`, `gps_rescue.h` 등은 건드리지 않음)

---

## 2. 사전 확인 사항 (수정 전 코드 상태 검증)

수정을 시작하기 전에, `src/main/flight/mission.c` 안에 아래 두 함수가 **정확히 이 형태로** 존재하는지 먼저 확인한다. 만약 코드가 아래와 다르면(즉, 이미 누군가 손을 댄 상태라면) 수정을 멈추고 보고할 것.

### 2-1. 확인할 함수 A: `missionApplyWaypoint`

```c
static void missionApplyWaypoint(void)
{
    if (currentMissionWpIndex >= missionWpCount) {
        missionStop();
        return;
    }
    missionUpdateTargetOnly();  // 즉시 타겟 좌표/고도/속도 업데이트
}
```

### 2-2. 확인할 함수 B: `missionUpdateTargetOnly`

```c
void missionUpdateTargetOnly(void)
{
    rescuePhase_e phase = gpsRescueGetPhase();
    if (phase == RESCUE_ABORT ||
        phase == RESCUE_DO_NOTHING ||
        phase == RESCUE_LANDING) {
        missionStop();
        return;
    }
    if (!missionIsActive()) {
        return;
    }

    missionWaypoint_t *wp = &missionWaypoints[currentMissionWpIndex];
    currentVCLat = wp->latitude;
    currentVCLon = wp->longitude;
    rescueState.intent.targetAltitudeCm = wp->altitude;

    // 목표 속도: waypoint 속도와 Rescue 최소 속도 중 큰 값
    rescueState.intent.targetVelocityCmS = MAX(
        wp->speed,
        (float)gpsRescueConfig()->groundSpeedCmS
    );

    GPS_distance_cm_bearing(&gpsSol.llh.lat, &gpsSol.llh.lon,
                           &currentVCLat, &currentVCLon,
                           &rescueState.intent.distanceToTargetCm,
                           &rescueState.intent.directionToTargetCd);
}
```

**중요**: `missionUpdateTargetOnly()`는 웨이포인트가 바뀔 때 한 번만 호출되는 게 아니라, **미션이 활성 상태인 동안 매 GPS Rescue 스케줄러 루프(약 100Hz)마다 계속 호출된다** (`gps_rescue.c`의 `gpsRescueUpdate()` 안, `if (missionIsActive())` 블록에서 매 루프 호출됨). 이 사실이 이번 수정의 핵심 전제이므로 반드시 기억할 것: "웨이포인트가 바뀐 첫 순간의 값"과 "그 이후 매 루프 갱신되는 값"을 구분해서 다뤄야 한다.

### 2-3. 확인할 함수 C: `missionInit` (뒷부분에 나오는 리셋 로직 추가용)

```c
void missionInit(void)
{
    // PG에서 waypoint 복원 (save/reboot 시 유지됨)
    const missionConfig_t *cfg = missionConfig();
    missionWpCount = cfg->waypointCount;
    for (int i = 0; i < missionWpCount; i++) {
        missionWaypoints[i] = cfg->waypoints[i];
    }
    currentMissionWpIndex = 0;
    isMissionActive = false;
    wpEntryTime = 0;
    prevDistCm = -1.0f;
    wasClosing = false;
}
```

---

## 3. 수정 내용 — 단계별 지시

### 3-1. 새 static 변수 3개 추가

파일 상단, 기존 static 변수들이 선언된 곳(아래 블록)을 찾는다:

```c
static bool isMissionActive = false;

// 미션 타임아웃 5분
#define MISSION_WP_TIMEOUT_US 300000000

static timeUs_t wpEntryTime = 0;
static float prevDistCm = -1.0f;
static bool wasClosing = false;
```

이 블록 바로 아래에 다음 3개 변수를 **새로 추가**한다:

```c
// --- Waypoint 간 점진적 고도 전환(글라이드 슬로프)을 위한 상태 변수 ---
// 새 waypoint가 활성화된 "그 순간"의 현재 고도를 저장 (보간의 시작점)
static float wpGlideStartAltitudeCm = 0.0f;
// 새 waypoint가 활성화된 "그 순간"의 목표까지 남은 수평거리를 저장 (보간의 분모)
static float wpGlideStartDistanceCm = 0.0f;
// 현재 활성 waypoint에 대해 위 두 값이 이미 캡처되었는지 여부
static bool  wpGlideInitialized = false;
```

또한 `#define MISSION_WP_TIMEOUT_US 300000000` 아래(또는 근처)에 새 상수 하나를 추가한다:

```c
// 시작 거리가 이 값보다 짧으면 보간하지 않고 즉시 목표고도 사용 (0으로 나누기/불안정 보간 방지)
#define WP_ALT_GLIDE_MIN_DISTANCE_CM 500.0f   // 5m
```

---

### 3-2. `missionApplyWaypoint()` 수정

**목적**: 새 waypoint로 전환되는 매 순간(=이 함수가 호출되는 순간) `wpGlideInitialized`를 `false`로 내려서, 다음 `missionUpdateTargetOnly()` 호출 시 "이 waypoint에 대한 새 기준값"을 다시 캡처하도록 예약한다.

**변경 전:**

```c
static void missionApplyWaypoint(void)
{
    if (currentMissionWpIndex >= missionWpCount) {
        missionStop();
        return;
    }
    missionUpdateTargetOnly();  // 즉시 타겟 좌표/고도/속도 업데이트
}
```

**변경 후:**

```c
static void missionApplyWaypoint(void)
{
    if (currentMissionWpIndex >= missionWpCount) {
        missionStop();
        return;
    }
    wpGlideInitialized = false;  // 새 WP 활성화 → 글라이드 기준값 재캡처 예약
    missionUpdateTargetOnly();   // 즉시 타겟 좌표/고도/속도 업데이트
}
```

변경점은 딱 한 줄(`wpGlideInitialized = false;`) 추가뿐이다. 다른 코드는 그대로 둔다.

---

### 3-3. `missionUpdateTargetOnly()` 수정 (핵심 변경)

**변경 전:**

```c
void missionUpdateTargetOnly(void)
{
    rescuePhase_e phase = gpsRescueGetPhase();
    if (phase == RESCUE_ABORT ||
        phase == RESCUE_DO_NOTHING ||
        phase == RESCUE_LANDING) {
        missionStop();
        return;
    }
    if (!missionIsActive()) {
        return;
    }

    missionWaypoint_t *wp = &missionWaypoints[currentMissionWpIndex];
    currentVCLat = wp->latitude;
    currentVCLon = wp->longitude;
    rescueState.intent.targetAltitudeCm = wp->altitude;

    // 목표 속도: waypoint 속도와 Rescue 최소 속도 중 큰 값
    rescueState.intent.targetVelocityCmS = MAX(
        wp->speed,
        (float)gpsRescueConfig()->groundSpeedCmS
    );

    GPS_distance_cm_bearing(&gpsSol.llh.lat, &gpsSol.llh.lon,
                           &currentVCLat, &currentVCLon,
                           &rescueState.intent.distanceToTargetCm,
                           &rescueState.intent.directionToTargetCd);
}
```

**변경 후 (전체를 이 코드로 완전히 교체한다):**

```c
void missionUpdateTargetOnly(void)
{
    rescuePhase_e phase = gpsRescueGetPhase();
    if (phase == RESCUE_ABORT ||
        phase == RESCUE_DO_NOTHING ||
        phase == RESCUE_LANDING) {
        missionStop();
        return;
    }
    if (!missionIsActive()) {
        return;
    }

    missionWaypoint_t *wp = &missionWaypoints[currentMissionWpIndex];
    currentVCLat = wp->latitude;
    currentVCLon = wp->longitude;

    // 1. 먼저 현재 위치 → 목표 waypoint까지의 수평거리/방위를 계산한다.
    //    (고도 보간 계산에 이 거리값이 필요하므로, 반드시 고도 계산보다 먼저 수행한다.)
    GPS_distance_cm_bearing(&gpsSol.llh.lat, &gpsSol.llh.lon,
                           &currentVCLat, &currentVCLon,
                           &rescueState.intent.distanceToTargetCm,
                           &rescueState.intent.directionToTargetCd);

    // 2. 이 waypoint가 활성화된 이후 "첫 번째 루프"에서만 글라이드 슬로프의
    //    기준값(시작 고도, 시작 거리)을 캡처한다. 이후 루프에서는 이 값을 고정 기준으로 사용.
    if (!wpGlideInitialized) {
        wpGlideStartAltitudeCm = rescueState.sensor.currentAltitudeCm;
        wpGlideStartDistanceCm = (float)rescueState.intent.distanceToTargetCm;
        wpGlideInitialized = true;
    }

    // 3. 직선(빗변) 보간으로 목표 고도를 계산한다.
    //    - 밑변: 시작 시점의 수평거리 (wpGlideStartDistanceCm)
    //    - 높이: 목표 고도 - 시작 고도 (wp->altitude - wpGlideStartAltitudeCm)
    //    - progress = 0.0(출발 직후) ~ 1.0(waypoint 도착) 사이를 남은 거리 비율로 계산
    float targetAltitudeCm;
    if (wpGlideStartDistanceCm < WP_ALT_GLIDE_MIN_DISTANCE_CM) {
        // 시작 거리가 너무 짧아 보간이 의미 없거나 0으로 나누기 위험이 있는 경우:
        // 기존 동작(즉시 목표고도 적용)으로 폴백한다.
        targetAltitudeCm = wp->altitude;
    } else {
        float currentDistanceCm = (float)rescueState.intent.distanceToTargetCm;
        float progress = 1.0f - (currentDistanceCm / wpGlideStartDistanceCm);
        progress = constrainf(progress, 0.0f, 1.0f);  // 역주행/오버슈트 시에도 0~1 범위 강제
        targetAltitudeCm = wpGlideStartAltitudeCm
                          + (wp->altitude - wpGlideStartAltitudeCm) * progress;
    }
    rescueState.intent.targetAltitudeCm = targetAltitudeCm;

    // 목표 속도: waypoint 속도와 Rescue 최소 속도 중 큰 값
    rescueState.intent.targetVelocityCmS = MAX(
        wp->speed,
        (float)gpsRescueConfig()->groundSpeedCmS
    );
}
```

**주의 — 순서를 반드시 지킬 것:**
`GPS_distance_cm_bearing()` 호출이 기존 코드에서는 함수 맨 마지막에 있었지만, 수정 후에는 **맨 앞으로 옮겨야 한다.** 이유: 새 고도 보간 로직이 `rescueState.intent.distanceToTargetCm` 값을 필요로 하는데, 이 값은 `GPS_distance_cm_bearing()`이 계산해주기 때문이다. 순서를 바꾸지 않으면 항상 "한 루프 전"의 낡은 거리값으로 보간하게 되어 오차가 누적된다.

---

### 3-4. `missionInit()`에 리셋 로직 추가 (안전장치)

**변경 전:**

```c
void missionInit(void)
{
    // PG에서 waypoint 복원 (save/reboot 시 유지됨)
    const missionConfig_t *cfg = missionConfig();
    missionWpCount = cfg->waypointCount;
    for (int i = 0; i < missionWpCount; i++) {
        missionWaypoints[i] = cfg->waypoints[i];
    }
    currentMissionWpIndex = 0;
    isMissionActive = false;
    wpEntryTime = 0;
    prevDistCm = -1.0f;
    wasClosing = false;
}
```

**변경 후:**

```c
void missionInit(void)
{
    // PG에서 waypoint 복원 (save/reboot 시 유지됨)
    const missionConfig_t *cfg = missionConfig();
    missionWpCount = cfg->waypointCount;
    for (int i = 0; i < missionWpCount; i++) {
        missionWaypoints[i] = cfg->waypoints[i];
    }
    currentMissionWpIndex = 0;
    isMissionActive = false;
    wpEntryTime = 0;
    prevDistCm = -1.0f;
    wasClosing = false;
    wpGlideInitialized = false;   // 추가: 재부팅/재초기화 시 글라이드 상태도 초기화
}
```

---

## 4. 최종 결과물 요약 (변경 사항 총정리)

| 항목 | 내용 |
|---|---|
| 파일 | `src/main/flight/mission.c` 1개만 수정 |
| 추가된 상수 | `WP_ALT_GLIDE_MIN_DISTANCE_CM` (500.0f) |
| 추가된 static 변수 | `wpGlideStartAltitudeCm`, `wpGlideStartDistanceCm`, `wpGlideInitialized` |
| 수정된 함수 | `missionApplyWaypoint()` (1줄 추가), `missionUpdateTargetOnly()` (전체 로직 교체), `missionInit()` (1줄 추가) |
| 건드리지 않은 것 | `gps_rescue.c`의 `calculateAltitudePitch()`, PID 게인, `maxRescueAngle` 등 기존 피치 제어 전부 |

---

## 5. 동작 원리 검증 (왜 이게 맞는지)

- 예시: waypoint 활성화 시점 현재 고도 100m, 목표 waypoint 고도 150m, 시작 거리 1000m.
  - `wpGlideStartAltitudeCm = 10000`(cm), `wpGlideStartDistanceCm = 100000`(cm)
  - 비행기가 500m(=거리 절반)를 남겨두면: `progress = 1.0 - (50000/100000) = 0.5`
    → `targetAltitudeCm = 10000 + (15000-10000)*0.5 = 12500` (125m, 정확히 절반 고도)
  - 즉 남은 거리가 줄어드는 정도에 정비례해서 목표 고도가 선형으로 올라간다 — 직각삼각형의 빗변을 따라가는 모양이 된다.
- 매 루프 `altErrM`(현재고도 - 목표고도)은 이제 "다음 순간의 작은 목표치"와의 차이만 발생하므로, 기존 `calculateAltitudePitch()`의 P/I 항이 포화되지 않고 완만한 피치각을 유지하게 된다.
- 거리가 `WP_ALT_GLIDE_MIN_DISTANCE_CM`(5m) 미만인 채로 새 waypoint가 활성화되는 극단적 경우(waypoint끼리 매우 가까운 경우)는 보간 없이 기존처럼 즉시 목표고도를 사용 — 이런 짧은 구간에서는 어차피 보간할 여유가 없으므로 안전한 폴백이다.
- 비행 중 바람 등으로 인해 목표 waypoint에서 일시적으로 멀어지는 경우(`currentDistanceCm > wpGlideStartDistanceCm`), `progress`가 음수가 나올 수 있는데 `constrainf(progress, 0.0f, 1.0f)`로 0에 고정되므로, 목표 고도가 시작 고도 아래/위로 튀는 이상 동작 없이 "출발 고도 유지"로 안전하게 수렴한다.

---

## 6. 빌드 후 검증 방법 (테스트 절차)

1. 정상 빌드되는지 확인 (`make TARGET=<보드타겟>`).
2. CLI로 waypoint 2개를 서로 다른 고도로 등록:
   ```
   waypoint insert 0 37.500000 127.000000 5000 800 FLYOVER 0 ORBIT
   waypoint insert 1 37.501000 127.000000 15000 800 FLYOVER 0 ORBIT
   ```
   (0번: 50m 고도, 1번: 150m 고도, 두 지점 간 거리 약 111m — 필요시 실제 테스트 환경에 맞게 거리를 더 벌릴 것)
3. 시뮬레이터 또는 실제 비행에서 `waypoint status`, OSD의 `GPS_RESCUE_HEADING`/고도 디버그 필드를 관찰하며, waypoint 전환 직후 피치각이 즉시 `maxRescueAngle`까지 튀지 않고, 거리 감소에 비례해 완만하게 변하는지 확인한다.
4. Blackbox 로그에서 `gpsRescueAngle[AI_PITCH]`와 `rescueState.intent.targetAltitudeCm` 값을 시간축으로 그려서, 목표 고도가 계단식이 아니라 직선(램프) 형태로 변하는지 확인한다.
5. Waypoint 사이 거리가 5m 미만인 극단 케이스도 별도로 테스트해서, 이 경우 기존처럼 즉시 목표고도가 적용되는지(폴백 동작) 확인한다.

---

## 7. 하지 말아야 할 것 (금지 사항)

- `calculateAltitudePitch()` 내부의 게인, 리미트, 필터 로직을 수정하지 말 것.
- `gps_rescue.c`의 `RESCUE_FLY_HOME`, `RESCUE_DESCENT`, `RESCUE_SHUTTLE` 등 홈 귀환 관련 고도 로직은 이번 수정 범위가 아니므로 손대지 말 것.
- `missionCheckAdvance()` (CPA 도착 판정 로직)는 이번 수정과 무관하므로 손대지 말 것.
- 새 변수명을 임의로 바꾸지 말 것 (`wpGlideStartAltitudeCm`, `wpGlideStartDistanceCm`, `wpGlideInitialized`, `WP_ALT_GLIDE_MIN_DISTANCE_CM` 이름 그대로 사용).
