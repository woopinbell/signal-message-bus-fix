# signal-message-bus

`signal-message-bus`는 두 프로세스 사이에서 짧은 문자열을 전달하는 POSIX C 프로그램입니다. 서버와 클라이언트는 `SIGUSR1`, `SIGUSR2`를 각각 비트 값 0과 1로 사용합니다.

공용 헤더에는 프로토콜에서 함께 사용할 상수와 함수 선언을 모아 두었습니다.

