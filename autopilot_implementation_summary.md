# GPS Rescue 기반 Waypoint Mission 구현 요약

> **계획서**: `autopilot_implementation_plan2.md`  
> **작업일**: 2026-07-04  
> **커밋**: `0872fe042` - "Add GPS Rescue-based Waypoint Mission feature (autopilot)"

---

## 1. 목적

최신 Betaflight Configurator의 FlightPlan 탭과 호환되는 Waypoint Mission 기능을 구현한다.
기존 `gps_rescue.c`의 검증된 제어 루프를 100% 재사용하고, 최소 코드 수정으로 동작한다.

---

## 2. 신규 파일

### `src/main/flight/mission.h`

- `missionWpType_e` (8종): FLYOVER, FLYBY, HOLD, LAND, TAKEOFF, ALT_CHANGE, DELAY, YAW_RATE
- `missionWpPattern_e` (2종): ORBIT, FIGURE8
- `missionWaypoint_t` struct: latitude, longitude, altitude, speed, type, duration, pattern
- `extern missionWaypoints[]`, `missionWpCount`, `currentMissionWpIndex`
- 함수 선언: `missionInit()`, `missionStart()`, `missionStop()`, `missionIsActive()`, `missionUpdateTargetOnly()`, `missionCheckAdvance()`, `missionClear()`, `missionInsert()`, `wpTypeToStr()`, `strToWpType()`, `wpPatternToStr()`, `strToWpPattern()`

### `src/main/flight/mission.c`

| 함수 | 설명 |
|---|---|
| `missionInit()` | 미션 저장소 초기화 |
| `missionStart()` | waypoint 존재 시 WP#1부터 미션 시작, 없으면 Rescue 실행 |
| `missionStop()` | Home Fix 유무에 따라 Home 귀환 / SHUTTLE_INFINITE 분기 |
| `missionIsActive()` | 미션 활성 상태 반환 |
| `missionUpdateTargetOnly()` | 현재 WP 좌표/고도/속도를 rescueState에 설정, 속도는 MAX(wp.speed, groundSpeedCmS) |
| `missionCheckAdvance()` | CPA(`wasClosing`) + 타임아웃(5분) 기반 WP 전환 |
| `missionClear()` | 모든 waypoint 삭제 |
| `missionInsert(idx, wp)` | 지정된 인덱스에 waypoint 삽입 (이후 WP shift) |
| `wpTypeToStr()` / `strToWpType()` | 타입 문자열 ↔ enum 변환 |
| `wpPatternToStr()` / `strToWpPattern()` | 패턴 문자열 ↔ enum 변환 |

**저장소**: RAM-only (전역 배열), 최대 15개 waypoint

---

## 3. 수정 파일

### `mk/source.mk`

`COMMON_SRC`에 `flight/mission.c \` 추가 → 빌드 포함

### `src/main/cli/cli.c`

`cliWaypoint()` 핸들러 구현:

| 명령 | 형식 |
|---|---|
| `waypoint list` | Configurator 파싱 가능한 `waypoint insert ...` 형식 출력 |
| `waypoint clear` | 모든 waypoint 삭제 (ARMED 시 차단) |
| `waypoint insert` | `<idx> <lat> <lon> <alt_ft> <speed_knots> <type> <duration_min> <pattern>` (ARMED 시 차단) |

### `src/main/flight/gps_rescue.c`

- `rescueState_s`, `rescueIntent_s`, `rescueSensorData_s`, `rescueFailureState_e` → 헤더로 이동
- `currentVCLat`, `currentVCLon` → static → extern (mission.c 접근용)
- `gpsRescueStart()` / `gpsRescueStop()` → static → public
- `gpsRescueUpdate()`: 3-way AUX 분기 추가
  - `<1400` : 무한셔틀
  - `1400~1600` : Autopilot (missionStart)
  - `>1600` : Rescue
- Mission 활성 시 `missionUpdateTargetOnly()` → `FLY_HOME` → `rescueAttainPosition()` → `missionCheckAdvance()` 단방향 호출
- `disarmOnImpact()`: `rescueStop()` → `gpsRescueStop()` 수정

### `src/main/flight/gps_rescue.h`

- `GPS_RESCUE_TOUCH_ACTIVATION_CM`, `GPS_RESCUE_TOUCH_PROXIMITY_CM` 상수 헤더 공개
- `rescuePhase_e`, `rescueFailureState_e`, `rescueIntent_s`, `rescueSensorData_s`, `rescueState_s` 구조체 공개
- `extern rescueState`, `currentVCLat`, `currentVCLon` 선언
- `gpsRescueStart()`, `gpsRescueStop()` 선언

---

## 4. 아키텍처

### 호출 흐름 (단방향)

```
gpsRescueUpdate()
  ├→ sensorUpdate()
  │
  ├→ [MISSION 활성]
  │    ├→ missionUpdateTargetOnly()   ← 타겟 설정
  │    ├→ phase = RESCUE_FLY_HOME
  │    ├→ performSanityChecks()
  │    ├→ rescueAttainPosition()      ← 기존 제어 루프 재사용
  │    └→ missionCheckAdvance()       ← CPA 체크 + WP 전환
  │
  └→ [MISSION 비활성] → 기존 Rescue 상태머신 (12단계)
```

### 3-way AUX 스위치

```
AUX < 1400    → 무한셔틀
AUX 1400-1600 → Autopilot (Waypoint Mission)
AUX > 1600    → Rescue (Home 귀환)
```

---

## 5. Configurator 연동

Configurator FlightPlan 탭은 CLI 명령으로만 통신 (MSP 미사용):

| 동작 | CLI 명령 |
|---|---|
| Waypoint 저장 | `waypoint clear` → `waypoint insert <idx> <lat> <lon> <alt_ft> <speed_knots> <type> <duration_min> <pattern>` (반복) → `save` |
| Waypoint 불러오기 | `waypoint list` (파싱) |
| Waypoint 전체 삭제 | `waypoint clear` → `save` |

### 단위 변환

| 방향 | 변환 |
|---|---|
| Configurator → 내부 | float deg → int32_t 1e-7 deg, feet → cm (×30.48), knots → cm/s (×51.4444), minutes → ds (×600) |
| 내부 → 출력 | 1e-7 deg → float deg, cm → feet (÷30.48), cm/s → knots (÷51.4444), ds → minutes (÷600) |

---

## 6. CPA (Closest Point of Approach) WP 전환

```
1. 거리가 GPS_RESCUE_TOUCH_ACTIVATION_CM(2000cm) 이하로 진입 → CPA 감시 시작
2. 거리가 감소하다가 20cm 이상 증가 → CPA 도달 판정 (wasClosing 로직)
3. GPS_RESCUE_TOUCH_PROXIMITY_CM(500cm) 이하 즉시 도달 판정
4. 5분 타임아웃 초과 시 강제 WP Skip
5. 모든 WP 완료 → Home Fix O: Home 귀환 / Home Fix X: SHUTTLE_INFINITE
```

---

## 7. 제외된 기능

- 배터리 저전압 체크 (계획 제외)
- EEPROM/PG 저장 (RAM-only)
- Pre-arm Wiggle 강화 (별도 계획)
