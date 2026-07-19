# GPS Rescue / Mission Flight / Infinite Shuttle Analysis Report

## 1. Control Authority Return on Rescue Mode Release

### Execution Order (Scheduler Priorities)
- PID controller: REALTIME priority (highest) - runs before mode bits updated
- RX task: HIGH priority - updates mode bits
- GPS_RESCUE task: MEDIUM priority - reads updated mode bits

**Critical Timing Gap**: PID runs with mode bits from previous iteration, then mode bits update, then rescue state updates. This causes one-cycle delay.

### Results

| Control | Return Status | Issue |
|---------|--------------|-----|
| Yaw | ✅ Safe | `gpsRescueGetYawRate()` only called when mode active |
| Throttle | ✅ Safe | `gpsRescueGetThrottle()` only called when mode active |
| Roll/Pitch (Acro/Rate) | ✅ Safe | `pidLevel()` not called when GPS_RESCUE_MODE off |
| Roll/Pitch (Angle ON) | ⚠️ Delayed | Uses stale `gpsRescueAngle[]` for one loop cycle |

**Minor Issue**: `rescueYaw` not reset to 0 in RESCUE_IDLE handler. Does not cause immediate problems but incomplete cleanup.

---

## 2. User Scenario Verification

### Verified Correct Implementation

| Feature | Code Location | Status |
|---------|--------------|--------|
| 3-second initial ascent | gps_rescue.c:69, 777-792 | ✅ |
| A-point generation (even descentAlt) | gps_rescue.c:1114-1143 | ✅ |
| A-point generation (odd descentAlt) | gps_rescue.c:1452-1457 | ✅ |
| Fly Home target selection (A vs Home) | gps_rescue.c:848-856 | ✅ |
| Shuttle mode transition | gps_rescue.c:1508-1520 | ✅ |
| Infinite shuttle A/B points | gps_rescue.c:247-259 | ✅ |
| Mission waypoint targets | mission.c:144-200 | ✅ |

---

## 3. Root Cause of "Mission Flight Flickering"

### Primary Issue: Mode Bit Timing Mismatch

When AUX switches from mission range (1400-1600) to shuttle mode (<1400):

1. **Loop N**: Mission mode active - PID uses mission values
2. **Loop N+1**: PID still uses **previous** mode bits (mission) while:
   - RX updates mode to shuttle
   - GPS_RESCUE calls `missionStop()`, sets phase to `RESCUE_SHUTTLE_INFINITE`
3. **Loop N+2**: PID sees shuttle mode bits

**Result**: One loop cycle where control values mismatch, causing flickering.

### Secondary Issue: Mission Entry from IDLE Phase

In `gps_rescue.c:1281-1287`:
```c
missionStart();     // Sets isMissionActive=true, calls missionApplyWaypoint()
rescueState.phase = RESCUE_MISSION_FLY_WP;
rescueAttainPosition();  // Called immediately after missionStart()
```

After `missionStart()`, `rescueAttainPosition()` is called, but `handleMissionPhase()` hasn't been called yet. The `missionUpdateTargetOnly()` sets `targetAltitudeCm` and `targetVelocityCmS`, but this happens in the same function call chain.

**Question for user**: Is there a specific reason for calling `rescueAttainPosition()` immediately after `missionStart()` in the IDLE branch, rather than letting the next iteration handle it through the missionIsActive branch? The current code calls `missionUpdateTargetOnly()` twice - once in `missionStart()` → `missionApplyWaypoint()`, and once at gps_rescue.c:1305.

---

## 4. Questions Requiring User Clarification

### Q1: Mission Entry Flow
Current code at gps_rescue.c:1281-1287 calls `rescueAttainPosition()` immediately after `missionStart()` when entering from IDLE. Should the mission target update only happen in the main missionIsActive branch (line 1305-1313), or is the immediate call necessary?

### Q2: Post-Waypoint Target Altitude (mission.c:128)
Current: `rescueState.intent.targetAltitudeCm = rescueState.intent.returnAltitudeCm`

User's spec implies: "waypoint가 끝나면 고도나 높이는 레스큐 시작 초기 단계에서 지정한 값을 사용"

Should this use `returnAltitudeCm` or `initialClimbM` value from config? Please confirm.

### Q3: Mission Mode Transitions During Waypoint Navigation
Should infinite shuttle activation (AUX < 1400) be allowed during mission waypoint navigation, or should the mission be allowed to complete? Current code allows immediate transition (line 1299).

---

## 5. Overall Assessment

The code **correctly implements the user's multi-mode architecture**. The flickering issue is caused by:
1. **Normal scheduler behavior** (task priority ordering)
2. **Mode boundary transitions** causing one-cycle control value mismatch

The user's core requirements are implemented correctly. The control return on rescue mode release is safe in most cases, with minor edge cases when ANGLE_MODE is also active.