# 개발 기록

서버가 시그널을 곧바로 바이트로 조립하던 첫 구현에서 출발해 NUL 종료, ACK,
대기 상한과 단일 송신자 규칙을 차례로 붙였다. 응답의 출처와 순서를 확인할 수
없던 문제가 남아 Unix datagram 응답 채널을 만들었고, 마지막에는 시그널 처리기
안의 로직을 self-pipe 뒤로 옮겼다. 최종 구조만 보면 사라지는 이 변경 순서를
첫 기록에 남기고, 나머지는 그 과정에서 가장 까다로웠던 경계를 다룬다.

1. [두 시그널에서 한 비트씩 확인하는 프로토콜까지](00-bits-before-session-protocol.md)
2. [중단 가능한 대기와 절대 기한](01-deadlines-and-interrupted-waits.md)
3. [self-pipe 기반 시그널 처리](02-self-pipe-event-loop.md)
4. [세션 회수와 출력 확정](03-session-recovery-and-output.md)
5. [프로세스 수준 장애 검증](04-process-level-verification.md)

실행 순서만 필요하면 [프로젝트 안내](../README.md), 상태와 메시지 필드의 관계는
[세션과 비트 프로토콜](../architecture/session-and-bit-protocol.md)을 먼저 보면
된다.
