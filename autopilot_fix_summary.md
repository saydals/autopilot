# Betaflight Autopilot - 수정 작업 요약

> **작업 기간:** 2026-07-05
> **대상 브랜치:** `autopilot` (`origin: saydals/my-betaflight.git`)
> **빌드 대상:** STM32F405

---

## 목차

1. [문제 1: waypoint 데이터 EEPROM 복원 실패](#문제-1-waypoint-데이터-eeprom-복원-실패)
2. [문제 2: waypoint list 명령 시 FC 크래시](#문제-2-waypoint-list-명령-시-fc-크래시)
3. [문제 3: YAW_RATE speed 단위 오류](#문제-3-yaw_rate-speed-단위-오류)
4. [커밋 내역](#커밋-내역)
5. [단위 규정](#단위-규정)

---

## 문제 1: waypoint 데이터 EEPROM 복원 실패

### 증상
- CLI에서 `waypoint insert` → `save` 정상 동작
- 재부팅 후 `waypoint list` → "No waypoints." (데이터 소실)

### 원인
`missionInit()`이 `gpsRescueInit()` 내부에서만 호출되었고, `gpsRescueInit()`은 `featureIsEnabled(FEATURE_GPS)` 조건부로만 실행됨.

```
init.c
  └── #ifdef USE_GPS
        └── if (featureIsEnabled(FEATURE_GPS))  ← GPS 꺼져 있으면 skip
              └── gpsInit()
              └── gpsRescueInit()
                    └── missionInit()           ← 여기서만 복원
```

GPS 기능이 비활성화된 상태에서는 `missionInit()`이 실행되지 않아 EEPROM에는 데이터가 있지만 RAM(`missionWpCount = 0`)으로 복원되지 않음.

### 수정

**파일:** `src/main/fc/init.c`

**① include 추가 (101~103번 줄):**
```c
#ifdef USE_FLIGHT_PLAN
#include "flight/mission.h"
#endif
```

### 수정

**파일:** `src/main/cli/cli.c`

**① `formatCoordinate()` 헬퍼 함수 추가 (4941~4959번 줄):**
```c
static void formatCoordinate(char *buf, int32_t coord)
{
    char *p = buf;
    if (coord < 0) { *p++ = '-'; coord = -coord; }
    uint32_t deg = (uint32_t)(coord / 10000000);
    tfp_sprintf(p, "%u.", deg);
    while (*p) p++;
    uint32_t frac = (uint32_t)(coord % 10000000);
    uint32_t div = 1000000;
    while (div) {
        *p++ = '0' + (frac / div);
        frac %= div;
        div /= 10;
    }
    *p = '\0';
}
```

**② list 출력을 `%d`와 `%s`만 사용하도록 변경 (4971~4994번 줄):**
```c
// ✅ 수정 후: %d + %s 만 사용
char latStr[20], lonStr[20];
formatCoordinate(latStr, wp->latitude);
formatCoordinate(lonStr, wp->longitude);

int altFt    = (int)(wp->altitude / 30.48f + 0.5f);
int speedVal = (wp->type == WP_TYPE_YAW_RATE)
    ? (int)(wp->speed + 0.5f)
    : (int)(wp->speed / 51.4444f + 0.5f);
int durInt   = (int)(wp->duration / 600.0f);
int durFrac  = (int)((wp->duration * 10.0f) / 600.0f) % 10;

cliPrintLinef("waypoint insert %d %s %s %d %d %s %d.%d %s",
    i, latStr, lonStr, altFt, speedVal,
    wpTypeToStr(wp->type), durInt, durFrac,
    wpPatternToStr(wp->pattern));
```

### 사용 포맷 비교

| 항목 | 수정 전 (크래시) | 수정 후 (정상) |
|------|-----------------|---------------|
| lat | `%.7f` + `(double)` | `formatCoordinate()` → `%s` |
| lon | `%.7f` + `(double)` | `formatCoordinate()` → `%s` |
| alt | `%.0f` + `(double)` | `%d` (반올림 정수) |
| speed | `%.0f` + `(double)` | `%d` (반올림 정수) |
| duration | `%.1f` + `(double)` | `%d.%d` (정수.소수1자리) |

---

## 문제 3: YAW_RATE speed 단위 오류

### 증상
- `WP_TYPE_YAW_RATE` 타입 waypoint의 speed 필드가 잘못 변환됨
- knots→cm/s 변환 계수(51.4444)가 YAW_RATE의 deg/s 값에도 적용됨

### 원인
CLI 파서가 모든 waypoint 타입에 대해 speed를 knots로 간주하고 `* 51.4444f`를 적용

```c
// 🔴 수정 전: 모든 타입에 동일 변환 적용
wp.speed = speedKnots * 51.4444f;
```


### 수정

**파일:** `src/main/cli/cli.c`

```c
// ✅ 수정 후: 타입에 따라 변환 분기
wp.type = strToWpType(typeStr);
if (wp.type == WP_TYPE_YAW_RATE) {
    wp.speed = speedKnots;  // deg/s, 변환 없이 그대로 저장
} else {
    wp.speed = speedKnots * 51.4444f;  // knots → cm/s
}
```

**CLI 도움말 업데이트:** speed 단위 설명 추가
```
insert <idx> <lat> <lon> <alt_ft> <speed> <type> <duration_min> <pattern>
    FLYOVER/FLYBY/HOLD/LAND/TAKEOFF/ALT_CHANGE/DELAY: speed=knots, duration=min
    YAW_RATE: speed=deg/s, duration=min
```

---

## 커밋 내역

```
e48fca09b  Refactor waypoint list: replace %d.%07d with formatCoordinate() helper
           src/main/cli/cli.c  (+27 -8)

10bc42ee3  Fix waypoint list crash: remove %f format not supported by tfp_printf
           src/main/cli/cli.c  (+20 -11)

7cd99aac7  Fix autopilot waypoint unit conversion and EEPROM restore
           src/main/cli/cli.c        (+17 -5)
           src/main/fc/init.c         (+7 -0)
           src/main/flight/mission.h  (+2 -1)
```

---

## 단위 규정

### missionWaypoint_t 구조체 (`src/main/flight/mission.h`)

| 필드 | 타입 | 저장 단위 | Configurator 입력 | 변환 계수 |
|------|------|----------|-------------------|----------|
| `latitude` | `int32_t` | 1e-7 deg | 도 (소수점) | `* 1e7f` |
| `longitude` | `int32_t` | 1e-7 deg | 도 (소수점) | `* 1e7f` |
| `altitude` | `float` | cm | feet | `* 30.48f` |
| `speed` | `float` | cm/s (일반) / deg/s (YAW_RATE) | knots (일반) / deg/s (YAW_RATE) | `* 51.4444f` (일반) / 변환 없음 (YAW_RATE) |
| `type` | `missionWpType_e` | enum | 문자열 | `strToWpType()` |
| `duration` | `float` | deciseconds (ds) | minutes | `* 600.0f` |
| `pattern` | `missionWpPattern_e` | enum | 문자열 | `strToWpPattern()` |

### CLI 명령 형식

```
waypoint insert <idx> <lat> <lon> <alt_ft> <speed> <type> <duration_min> <pattern>
waypoint list
waypoint clear
```


### 타입/패턴 enum

| 타입 | 값 | 문자열 |
|------|-----|--------|
| FLYOVER | 0 | "FLYOVER" |
| FLYBY | 1 | "FLYBY" |
| HOLD | 2 | "HOLD" |
| LAND | 3 | "LAND" |
| TAKEOFF | 4 | "TAKEOFF" |
| ALT_CHANGE | 5 | "ALT_CHANGE" |
| DELAY | 6 | "DELAY" |
| YAW_RATE | 7 | "YAW_RATE" |

| 패턴 | 값 | 문자열 |
|------|-----|--------|
| ORBIT | 0 | "ORBIT" |
| FIGURE8 | 1 | "FIGURE8" |

---

## 빌드 확인

```
Target: STM32F405
FLASH1: 597377 B / 992 KB  (58.81%)
RAM:    101424 B / 128 KB  (77.38%)
CCM:    15148 B  / 64 KB   (23.11%)
Build:  SUCCESS
```

