# 첫 번째 Waypoint 500m 검증 안전장치 구현 TODO

## 완료된 작업
- [x] mission.c/mission.h 분석 완료
- [x] gps_rescue.c 분석 완료 (구조 변경 불필요 확인)
- [x] osd_warnings 시스템 분석 완료
- [x] Home 위치 기반 검증 방식으로 단순화 (GPS 샘플링 불필요)

## 작업 단계

### Step 1: mission.h 수정
- [x] `MISSION_FIRST_WP_MAX_DISTANCE_CM` 상수 추가 (50000 = 500m)
- [x] `missionValidateFirstWaypoint()` 함수 선언 추가
- [x] `missionIsWp1TooFar()` getter 선언 추가

### Step 2: mission.c 수정 — 핵심 로직
- [x] `wp1TooFar` 정적 변수 추가
- [x] `missionStart()` 함수에 WP 거리 검증 조건부 분기 추가
- [x] `missionValidateFirstWaypoint()` 함수 구현 (GPS_home vs 첫 WP 거리 비교)
- [x] `missionIsWp1TooFar()` getter 구현
- [x] `missionStopAndGoHome()` 전방 선언 추가 (컴파일 오류 수정)
- [x] `bearingCd` 타입 `int16_t` → `int32_t` 수정 (컴파일 오류 수정)

### Step 3: osd.h 수정
- [x] warning enum에 `OSD_WARNING_GPS_RESCUE_WP1_TOO_FAR` 추가

### Step 4: osd_warnings.c 수정
- [x] `#include "flight/mission.h"` 추가
- [x] `#ifdef USE_FLIGHT_PLAN` 조건부 경고 메시지 "WP1>500m MISSION ABORT" 추가

## 검증
- [x] 빌드 테스트 성공 (STM32F405, 0 에러)
- [x] 로직 검토 완료 — 회귀 없음 확인
