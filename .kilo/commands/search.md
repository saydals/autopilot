---
description: "파일 내용 검색시 ripgrep(rg) 사용 규칙"
---

# 파일 내용 검색 규칙

파일 내용 검색 시 기본 도구로 `grep` 대신 **ripgrep(`rg`)** 을 사용한다.

## 적용 범위

- 용도: 소스 코드, 텍스트 파일 내 패턴 검색
- 기본 명령: `rg -n "<pattern>" <path>`
- 옵션:
  - `-n`: 줄 번호 표시
  - `-i`: 대소문자 무시 (필요시)
  - `-C 3`: 컨텍스트 3줄 표시 (필요시)
  - `--type`: 파일 타입 필터 (필요시)

## 금지

- `grep` 명령 직접 사용 금지
- `find ... | xargs grep` 패턴 금지

## 예시

```bash
rg -n "MISSION_FIRST_WP_MAX_DISTANCE_CM" src/
rg -n "500" src/main/flight/mission.c
rg -n "gpsRescueUpdate" src/main/flight/gps_rescue.c
```
