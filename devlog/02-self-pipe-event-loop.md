# self-pipe 기반 시그널 처리

## 처리기 안의 코드를 최소화한다

시그널 처리기는 임의의 지점에서 실행된다. `malloc`, `printf`, 일반 문자열
함수나 변경 중인 전역 자료구조를 만지면 재진입 문제와 교착이 생길 수 있다.
`src/server.c`의 처리기는 PID와 시그널 번호만 pipe에 쓴다.

```c
saved_errno = errno;
event.sender = 0;
if (info != NULL)
	event.sender = info->si_pid;
event.signal = signal;
if (MT_EVENT_WRITE(g_event_pipe[1], &event, sizeof(event))
	!= (ssize_t)sizeof(event))
	g_event_overflow = 1;
errno = saved_errno;
```

사건 구조체가 `PIPE_BUF` 이하인지 컴파일 시 배열 크기로 확인해 한 번의 쓰기가
다른 사건과 섞이지 않게 한다. 두 데이터 시그널과 종료 시그널은 처리기 실행 중
함께 막고, pipe 쓰기 끝에는 `O_NONBLOCK`과 `FD_CLOEXEC`을 설정한다. 처리기가
들어오기 전의 `errno`도 복원한다.

## 포화는 조용히 무시하지 않는다

nonblocking pipe가 가득 차거나 짧게 쓰면 해당 비트가 0인지 1인지 복구할 수
없다. `g_event_overflow`를 본 이벤트 루프는 `ENOBUFS`로 실패하고, 실패한 비트에
ACK를 보내지 않는다. 한 사건만 버리고 다음 비트를 처리하면 이후 모든 바이트
경계가 틀어진다.

단순 부하로 파이프 포화를 안정적으로 만들기 어렵다. fault 서버는
`MT_EVENT_WRITE`를 치환하고 `MT_TEST_EVENT_EAGAIN=1`로 첫 사건 기록 실패를
만든다. `tests/protocol_regressions.sh`는 서버와 클라이언트가 모두 실패하고
서버 소켓이 정리되는지 확인한다.

## 종료 처리도 같은 통로를 쓴다

`SIGHUP`, `SIGINT`, `SIGTERM`도 처리기에서 직접 `close`와 `unlink`를 수행하지
않는다. 사건으로 넘겨 메인 흐름에서 pipe, 응답 소켓과 자신이 bind한 경로를
정리한다. 종료 상태는 `128 + signal`로 돌려준다.

서버는 부모에게서 데이터 시그널이 차단된 상태를 물려받았을 때 이를 명시적으로
해제하지 않는다. 현재 프로세스 검사는 차단된 상태의 클라이언트만 실행한다.
서버를 그런 실행 환경에서도 지원하려면 시작 시 시그널 마스크를 정규화하고
별도 사례를 추가해야 한다.

```sh
make test
```
