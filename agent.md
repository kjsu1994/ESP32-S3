# ESP32-S3 홈 IoT 프로젝트 에이전트 가이드
**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 목적

이 프로젝트의 목표는 `ESP32-S3 N16R8` 기반 홈 IoT 디바이스를 안정적으로 개발하고, 백엔드 서버와 연동해 자동화, 상태 보고, 명령 처리, OTA 업데이트까지 운영 가능한 구조로 만드는 것이다.

현재 펌웨어 기준으로 이미 포함된 요소는 다음과 같다.

- `PlatformIO`
- `Arduino framework`
- `ESP32-S3 DevKitC-1` 타깃
- `Wi-Fi` 연결
- `Arduino OTA`
- 보드 내장 `RGB LED` 상태 표시

## 핵심 목표

1. 일반적인 가정용 네트워크 환경에서 안정적으로 동작하도록 유지한다.
2. 부팅, 재연결, 명령 실패, OTA 중에도 안전한 상태를 유지한다.
3. 센서, 액추에이터, 네트워크, 백엔드 로직이 서로 강하게 엉키지 않도록 구조화한다.
4. REST와 MQTT 기반의 백엔드 연동 규칙을 명확하게 유지한다.

## 기본 가정

- 대상 보드는 `PSRAM`과 내장 `RGB LED`가 있는 `ESP32-S3 호환 보드`다.
- 펌웨어는 `ESP-IDF 우선`이 아니라 `PlatformIO + Arduino` 기준으로 개발한다.
- 백엔드 연동 기본값은 다음과 같이 둔다.
  - `MQTT`: 실시간 명령, 상태 동기화, 이벤트 전달
  - `REST`: 디바이스 등록, 설정 조회, 초기 프로비저닝, 관리 API
- 프로젝트는 향후 다음과 같은 홈 자동화 시나리오로 확장될 수 있다.
  - 센서 수집
  - 릴레이/조명 제어
  - 경보/알림
  - 스케줄 기반 제어
  - 규칙 기반 자동화

## 개발 우선순위

아래 순서대로 중요하게 판단한다.

1. 안정성 및 복구 가능성
2. 보안 및 비밀정보 관리
3. 안전한 상태 전이
4. 관측 가능성
5. 응답성과 성능
6. 기능 확장

## 펌웨어 설계 원칙

- `setup()`은 짧고 예측 가능하게 유지한다.
- `loop()`는 비차단 방식으로 유지한다. 긴 `delay()` 사용은 피한다.
- 거대한 단일 루프보다 `상태 머신`, `millis 기반 주기 처리`, `작은 모듈` 구조를 선호한다.
- 관심사를 분리한다.
  - 하드웨어 드라이버
  - 네트워크 연결 관리
  - 백엔드 프로토콜 처리
  - 자동화 로직
  - 디바이스 상태 보고
- 코드가 커지면 `src/main.cpp`에 계속 누적하지 말고 `lib/` 또는 별도 모듈로 분리한다.
- LED 상태 표시는 진단 수단으로 적극 활용한다.
  - 부팅 중
  - Wi-Fi 연결 중
  - 백엔드 연결 완료
  - OTA 진행 중
  - 오류 상태

## 백엔드 연동 원칙

- 기능마다 제각각 토픽명이나 payload 형식을 만들지 않는다.
- 백엔드 계약은 아래 개념을 기준으로 정리한다.
  - 디바이스 식별
  - desired state
  - reported state
  - telemetry
  - commands
  - health / last-seen
  - OTA 상태
- 필요한 경우 메시지에 다음 메타데이터를 안정적으로 포함한다.
  - `deviceId`
  - `deviceType`
  - `roomId` 또는 위치 식별자
  - `fwVersion`
  - `ip`
  - `rssi`
- 명령 수신과 실제 상태 반영은 구분한다.
- 재연결 상황을 항상 고려한다.
  - Wi-Fi 재연결
  - MQTT 재연결
  - 재연결 후 상태 재동기화
  - 가능한 경우 멱등성 있는 명령 처리

## 보안 원칙

- 실제 운영용 비밀번호나 토큰을 하드코딩하는 방식을 장기 표준으로 삼지 않는다.
- Wi-Fi, OTA, API, MQTT 브로커 자격정보는 프로젝트가 커지면 추적 파일 밖으로 분리하는 방향으로 간다.
- 실제 비밀값은 로컬 전용 설정 또는 무시되는 설정 파일로 분리하는 방식을 우선 고려한다.
- 개발 편의를 이유로 OTA 또는 백엔드 인증을 약화시키지 않는다.

## 코드 작성 규칙

- 현재 진입점은 `src/main.cpp`지만, 최종 구조는 모듈 분리를 전제로 설계한다.
- `include/config.h`는 현재 임시 설정 저장 위치로 간주한다.
- 센서나 액추에이터를 추가할 때는 거대한 루프에 직접 덧붙이기보다 작은 추상화 계층을 둔다.
- 백엔드 연동을 추가할 때는 다음 책임을 분리하는 쪽을 우선 고려한다.
  - 네트워크 세션 관리
  - MQTT 클라이언트 수명주기
  - REST 클라이언트 또는 프로비저닝 흐름
  - payload 직렬화/역직렬화

## 주석 작성 기준

- 사용자가 직접 유지보수할 수 있도록 코드 주석은 `한글 또는 쉬운 영어`로 작성한다.
- 복잡한 로직, 상태 전이, 재연결 처리, 하드웨어 제약은 주석으로 의도를 남긴다.
- 자명한 대입문 수준의 불필요한 주석은 피한다.
- 팀 내에서 별도 요청이 없다면 `짧고 명확한 한국어 주석`을 기본값으로 삼는다.

## 검증 기준

펌웨어 또는 프로토콜 변경 시 가능하면 아래를 확인한다.

- `env:esp32s3` 기준 빌드가 되는지
- Wi-Fi 연결 흐름이 유지되는지
- OTA 경로가 계속 살아있는지
- LED 상태가 유의미한 진단 신호를 주는지
- 재연결 이후 보드가 멈춘 상태로 빠지지 않는지
- 백엔드 payload가 내부적으로 일관적인지

## 피해야 할 것

- 로컬 디바이스 수명주기가 안정화되기 전에 과도하게 클라우드 규모 최적화부터 하지 않는다.
- 자동화 로직을 특정 센서 1개 또는 전송 방식 1개에 강하게 묶지 않는다.
- 네트워크 실패나 상태 오류를 숨기지 않는다. 시리얼 로그와 LED 상태로 드러나게 한다.

## 다음 진화 방향

다음 단계의 우선순위는 보통 아래 순서를 따른다.

1. 디바이스 식별자와 설정 모델 도입
2. MQTT 세션 처리 추가
3. REST 기반 초기 등록 및 설정 조회 추가
4. telemetry / command / state 보고 로직 분리
5. 그 위에 자동화 로직 추가


## 사용자가 확인할 명령어 모음
pio run -e esp32s3-ota -t upload  ota업로드

## External integration note
- For Home_IOT backend/frontend integration rules, read HOME_IOT_INTEGRATION.md first.
- Treat `C:\Users\honeybadger\Desktop\ctfmon_project` as reference-only. Do not edit it for Home_IOT work.
- Prefer HOME_IOT_INTEGRATION.md for REST, MQTT, payload, and web-stack alignment decisions.
