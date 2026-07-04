# `cliWaypoint()` 함수 분석

> 파일: `src/main/cli/cli.c`  
> 브랜치: `autopilot`

---

## 전체 함수

```c
#ifdef USE_FLIGHT_PLAN
static void cliWaypoint(const char *cmdName, char *cmdline)
{
    // ── list 분기 ──
    if (strcasecmp(cmdline, "list") == 0) {
        if (missionWpCount == 0) {
            cliPrintLine("No waypoints.");
            return;
        }
        cliPrintLinef("# Waypoint count: %d", missionWpCount);
        for (int i = 0; i < missionWpCount; i++) {
            missionWaypoint_t *wp = &missionWaypoints[i];
            const float latDeg = (float)wp->latitude / 1e7f;
            const float lonDeg = (float)wp->longitude / 1e7f;
            const float altFeet = wp->altitude / 30.48f;
            const float speedKnots = wp->speed / 51.4444f;
            const float durationMin = wp->duration / 600.0f;
            cliPrintLinef("waypoint insert %d %.7f %.7f %.0f %.0f %s %.1f %s",
                i,
                (double)latDeg, (double)lonDeg,
                (double)altFeet, (double)speedKnots,
                wpTypeToStr(wp->type),
                (double)durationMin,
                wpPatternToStr(wp->pattern));
        }

    // ── clear 분기 ──
    } else if (strcasecmp(cmdline, "clear") == 0) {
        if (ARMING_FLAG(ARMED)) {
            cliPrintErrorLinef(cmdName, "Cannot clear waypoints while armed.");
            return;
        }
        missionClear();
        cliPrintLine("All waypoints cleared.");

    // ── insert 분기 (인자 8개) ──
    } else if (strncasecmp(cmdline, "insert ", 7) == 0) {
        if (ARMING_FLAG(ARMED)) {
            cliPrintErrorLinef(cmdName, "Cannot modify waypoints while armed.");
            return;
        }
        char *args = cmdline + 6;
        while (*args == ' ') args++;

        char *saveptr;
        char *token = strtok_r(args, " ", &saveptr);

        // 인자 1: idx
        if (!token) { cliShowParseError(cmdName); return; }
        int idx = atoi(token);

        // 인자 2: lat
        if (!(token = strtok_r(NULL, " ", &saveptr))) { cliShowParseError(cmdName); return; }
        float latDeg = atof(token);

        // 인자 3: lon
        if (!(token = strtok_r(NULL, " ", &saveptr))) { cliShowParseError(cmdName); return; }
        float lonDeg = atof(token);

        // 인자 4: alt_ft
        if (!(token = strtok_r(NULL, " ", &saveptr))) { cliShowParseError(cmdName); return; }
        float altFt = atof(token);

        // 인자 5: speed_knots
        if (!(token = strtok_r(NULL, " ", &saveptr))) { cliShowParseError(cmdName); return; }
        float speedKnots = atof(token);

        // 인자 6: type
        if (!(token = strtok_r(NULL, " ", &saveptr))) { cliShowParseError(cmdName); return; }
        const char *typeStr = token;

        // 인자 7: duration_min
        if (!(token = strtok_r(NULL, " ", &saveptr))) { cliShowParseError(cmdName); return; }
        float durationMin = atof(token);

        // 인자 8: pattern
        if (!(token = strtok_r(NULL, " ", &saveptr))) { cliShowParseError(cmdName); return; }
        const char *patternStr = token;

        // ── missionInsert() 호출 ──
        missionWaypoint_t wp;
        wp.latitude  = (int32_t)(latDeg * 1e7f);
        wp.longitude = (int32_t)(lonDeg * 1e7f);
        wp.altitude  = altFt * 30.48f;
        wp.speed     = speedKnots * 51.4444f;
        wp.type      = strToWpType(typeStr);
        wp.duration  = durationMin * 600.0f;
        wp.pattern   = strToWpPattern(patternStr);

        if (missionInsert(idx, &wp)) {
            cliPrintLinef("Waypoint %d inserted (count=%d).", idx, missionWpCount);
        } else {
            cliPrintErrorLinef(cmdName, "Insert failed: index out of bounds or max waypoints reached.");
        }

    } else {
        cliPrintErrorLinef(cmdName, "Unknown command: %s", cmdline);
    }
}
#endif
```

---

## 항목별 요약

### 1. 인자 개수 검사 (`argc`)

- 명령어는 고정 8개 인자 (`idx lat lon alt_ft speed_knots type duration_min pattern`)
- 각 인자를 `strtok_r()`로 하나씩 파싱
- 인자가 부족하면 `cliShowParseError(cmdName)` 호출 후 `return`
- **명시적 argc 카운트는 없음** → strtok_r이 NULL 반환 시 부족으로 판단

### 2. insert 분기

- `strncasecmp(cmdline, "insert ", 7) == 0` — "insert" + 공백 포함 7자 매칭
- ARMED 상태면 차단
- `strtok_r()`로 8개 인자 순차 파싱
- 변환: degree → 1e-7, feet → cm, knots → cm/s, minutes → deciseconds

### 3. `missionInsert()` 호출

```c
missionWaypoint_t wp;
wp.latitude  = (int32_t)(latDeg * 1e7f);
wp.longitude = (int32_t)(lonDeg * 1e7f);
wp.altitude  = altFt * 30.48f;         // feet → cm
wp.speed     = speedKnots * 51.4444f;  // knots → cm/s
wp.type      = strToWpType(typeStr);   // 문자열 → enum
wp.duration  = durationMin * 600.0f;   // minutes → deciseconds
wp.pattern   = strToWpPattern(patternStr); // 문자열 → enum

if (missionInsert(idx, &wp)) {
    cliPrintLinef("Waypoint %d inserted (count=%d).", idx, missionWpCount);
} else {
    cliPrintErrorLinef(cmdName, "Insert failed: index out of bounds or max waypoints reached.");
}
```

### 4. `missionInsert()` 구현 (mission.c)

```c
bool missionInsert(int idx, const missionWaypoint_t *wp)
{
    if (idx < 0 || idx > missionWpCount || missionWpCount >= MAX_MISSION_WAYPOINTS) {
        return false;
    }

    // 삽입 위치 이후 Waypoint를 한 칸씩 뒤로 이동
    for (int i = missionWpCount; i > idx; i--) {
        memcpy(&missionWaypoints[i], &missionWaypoints[i - 1], sizeof(missionWaypoint_t));
    }

    memcpy(&missionWaypoints[idx], wp, sizeof(missionWaypoint_t));
    missionWpCount++;

    // PG에도 반영 (EEPROM 저장용)
    missionConfigMutable()->waypointCount = missionWpCount;
    for (int i = 0; i < missionWpCount; i++) {
        missionConfigMutable()->waypoints[i] = missionWaypoints[i];
    }

    return true;
}
```
