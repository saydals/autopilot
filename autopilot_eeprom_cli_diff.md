# Waypoint EEPROM 저장(PG) + CLI dump 수정 Diff

> 커밋: `19dde7a20` (PG), `ece92042d` (CLI dump)

---

## 1. `pg/mission.h` (신규) — PG 타입 정의

```diff
--- /dev/null
+++ b/src/main/pg/mission.h
@@ -0,0 +1,35 @@
+#pragma once
+
+#include <stdint.h>
+#include "pg/pg.h"
+#include "flight/mission.h"
+
+typedef struct missionConfig_s {
+    uint8_t waypointCount;
+    missionWaypoint_t waypoints[MAX_MISSION_WAYPOINTS];
+} missionConfig_t;
+
+PG_DECLARE(missionConfig_t, missionConfig);
```

---

## 2. `pg/mission.c` (신규) — PG 등록

```diff
--- /dev/null
+++ b/src/main/pg/mission.c
@@ -0,0 +1,38 @@
+#include "platform.h"
+
+#ifdef USE_FLIGHT_PLAN
+
+#include "pg/pg.h"
+#include "pg/pg_ids.h"
+#include "pg/mission.h"
+
+PG_REGISTER_WITH_RESET_TEMPLATE(missionConfig_t, missionConfig, PG_MISSION_CONFIG, 0);
+
+PG_RESET_TEMPLATE(missionConfig_t, missionConfig,
+    .waypointCount = 0,
+    .waypoints = { { 0 } }
+);
+
+#endif // USE_FLIGHT_PLAN
```

---

## 3. `pg/pg_ids.h` — PG ID 추가

```diff
 #define PG_SOFTSERIAL_PIN_CONFIG    558
-#define PG_BETAFLIGHT_END           558
+#define PG_MISSION_CONFIG           559
+#define PG_BETAFLIGHT_END           559
```

---

## 4. `flight/mission.c` — PG 동기화

### 4.1 include + 저장소 명칭 변경

```diff
 #include "flight/gps_rescue.h"
 #include "mission.h"
+#include "pg/mission.h"

- * 미션 저장소 (RAM-only)
+ * 미션 저장소 (PG-backed RAM copy)
```

### 4.2 `missionInit()` 신규 — PG → RAM 복원

```diff
+void missionInit(void)
+{
+    const missionConfig_t *cfg = missionConfig();
+    missionWpCount = cfg->waypointCount;
+    for (int i = 0; i < missionWpCount; i++) {
+        missionWaypoints[i] = cfg->waypoints[i];
+    }
+    currentMissionWpIndex = 0;
+    isMissionActive = false;
+    wpEntryTime = 0;
+    prevDistCm = -1.0f;
+    wasClosing = false;
+}
```

### 4.3 `missionClear()` — PG 동기화 추가

```diff
 void missionClear(void)
 {
     memset(missionWaypoints, 0, sizeof(missionWaypoints));
     missionWpCount = 0;
+    // PG에도 반영
+    missionConfigMutable()->waypointCount = 0;
+    memset(missionConfigMutable()->waypoints, 0, sizeof(missionConfigMutable()->waypoints));
 }
```

### 4.4 `missionInsert()` — PG 동기화 추가

```diff
 bool missionInsert(int idx, const missionWaypoint_t *wp)
 {
     ...
     memcpy(&missionWaypoints[idx], wp, sizeof(missionWaypoint_t));
     missionWpCount++;
+    // PG에도 반영
+    missionConfigMutable()->waypointCount = missionWpCount;
+    for (int i = 0; i < missionWpCount; i++) {
+        missionConfigMutable()->waypoints[i] = missionWaypoints[i];
+    }
     return true;
 }
```

---

## 5. `flight/gps_rescue.c` — 부팅 시 PG 복원

```diff
 void gpsRescueInit(void)
 {
     updateRescueParams();
+#ifdef USE_FLIGHT_PLAN
+    // PG에서 waypoint 복원 (save 후 재부팅 시 유지)
+    missionInit();
+#endif
     ...
 }
```

---

## 6. `cli/cli.c` — dump/diff에 waypoint 출력

```diff
     }
 #endif

+#ifdef USE_FLIGHT_PLAN
+    // dump mission waypoints
+    if ((dumpMask & DUMP_MASTER) || (dumpMask & DUMP_ALL)) {
+        if (missionWpCount > 0) {
+            cliPrintHashLine("waypoints");
+            for (int i = 0; i < missionWpCount; i++) {
+                missionWaypoint_t *wp = &missionWaypoints[i];
+                const float latDeg = (float)wp->latitude / 1e7f;
+                const float lonDeg = (float)wp->longitude / 1e7f;
+                const float altFeet = wp->altitude / 30.48f;
+                const float speedKnots = wp->speed / 51.4444f;
+                const float durationMin = wp->duration / 600.0f;
+                cliPrintLinef("waypoint insert %d %.7f %.7f %.0f %.0f %s %.1f %s",
+                    i,
+                    (double)latDeg, (double)lonDeg,
+                    (double)altFeet, (double)speedKnots,
+                    wpTypeToStr(wp->type),
+                    (double)durationMin,
+                    wpPatternToStr(wp->pattern));
+            }
+        }
+    }
+#endif
+
     // restore configs from copies
     restoreConfigs(0);
 }
```

---

## 7개 파일 변경 요약

| 파일 | 변경 | 설명 |
|---|---|---|
| `pg/mission.h` | 🔵 신규 | `missionConfig_t` PG 타입 |
| `pg/mission.c` | 🔵 신규 | `PG_MISSION_CONFIG` (559) 등록 |
| `pg/pg_ids.h` | 🟡 수정 | PG_ID `559` 추가, `BETAFLIGHT_END=559` |
| `flight/mission.c` | 🟡 수정 | `missionInit()` 추가, `Clear/Insert` PG 동기화 |
| `flight/gps_rescue.c` | 🟡 수정 | `gpsRescueInit()` → `missionInit()` 호출 |
| `cli/cli.c` | 🟡 수정 | `printConfig()` → waypoint dump 출력 |
