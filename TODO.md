# 코드 수정 TODO

## 🔴 Critical (즉시 수정)

- [x] **N4**: `mission.c` - `missionUpdateTargetOnly()` 안전 phase 덮어쓰기 수정
- [x] **N1**: `mission.c` - `missionStart()` WP=0 무한 루프 수정
- [x] **CRIT-3**: `gps_rescue.c` - 랜딩 타이머 변수 분리

## 🟠 High

- [x] **CRIT-5**: `gps_rescue.c` - SHUTTLE_DESCENT/DESCENT → SHUTTLE_INFINITE 전환 시 `initShuttlePoints()` 추가
- [x] **N3**: `gps_rescue.c` - Phase 전환 시 `shuttleInfinite` 조건부 리셋
- [x] **CRIT-6**: `mission.c` - `wasClosing` 초기화
- [x] **CRIT-2**: `gps_rescue.c` - SHUTTLE_INFINITE → INITIALIZE 전환 시 `gpsRescueResetState()` 추가
- [x] **N5**: `mission.c` - `missionRemove()` 인덱스 보정
- [x] **N6**: `mission.c` - `missionStop()`에 `wpGlideInitialized = false` 추가
- [x] **N7**: `mission.c` - `missionClear()` 인덱스 초기화
- [x] **HIGH-2**: `mission.c` - `missionStart()` FLY_HOME/ATTAIN_ALT 차단
- [x] **HIGH-5**: `gps_rescue.c` - `performSanityChecks()` RESCUE_MISSION_FLY_WP 포함
- [x] **HIGH-4**: `gps_rescue.c` - 급하강 종료 후 `targetAltitudeCm` 재설정
- [x] **HIGH-7**: `gps_rescue.c` - `initialVelocityLow` static 변수화

## 🟡 Medium
- [x] **N2**: `mission.c` - `missionStop()` 호출 컨텍스트 주석 추가

## 최종 확인
- [x] 모든 15개 수정 사항 코드 적용 완료
- [x] **STM32F405 빌드 성공** — FLASH 58.97%, RAM 77.39% 사용, 에러/경고 없음
