# TODO: velocityToTargetCmS 추가 및 performSanityChecks 수정

## 완료된 항목
- [x] 1. gps_rescue.h - rescueSensorData_s 구조체에 velocityToTargetCmS 필드 추가
- [x] 2. gps_rescue.c - prevDistanceToTargetCm 전역 변수 추가
- [x] 3. gps_rescue.c - sensorUpdate()에 velocityToTargetCmS 계산 추가
- [x] 4. gps_rescue.c - performSanityChecks() 수정 (MISSION_FLY_WP용 velocityToTargetCmS 체크)
- [x] 5. gps_rescue.c - Phase 전환 시 prevDistanceToTargetCm 초기화
- [x] 6. gps_rescue.c - MISSION_FLY_WP 진입 시 prevDistanceToTargetCm 초기화

## 진행 방법
1. 각 파일을 순차적으로 수정 ✅
2. 수정 후 컴파일 확인 (필요시)
