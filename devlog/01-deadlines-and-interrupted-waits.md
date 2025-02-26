# 중단 가능한 대기와 절대 기한

## `EINTR` 뒤 timeout을 초기화하지 않는다

`pselect`는 신호 때문에 `EINTR`로 끝날 수 있다. 다시 기다릴 때 처음 timeout을
그대로 넘기면 신호가 올 때마다 허용 시간이 늘어난다. `src/client.c`는 예약과
각 비트 전송 전에 `CLOCK_MONOTONIC` 기준 절대 기한을 만들고, 반복마다 남은
시간을 다시 계산한다.

```c
remaining = time_until(deadline);
if (remaining.tv_sec == 0 && remaining.tv_nsec == 0)
	return (SEND_TIMEOUT);
status = pselect(g_response_socket + 1, &read_set, NULL, NULL,
		&remaining, NULL);
if (status == -1 && errno != EINTR)
	return (SEND_ERROR);
```

잘못된 datagram도 같은 반복 안에서 버린다. 패킷을 하나 받을 때마다 상대
timeout을 다시 시작하면 오래된 응답을 계속 보내는 것만으로 클라이언트를 붙잡을
수 있다. `tests/protocol_regressions.sh`는 잘못된 응답이 이어지는 서버에서도
클라이언트가 2초보다 이르지 않고 6초 안에는 timeout으로 끝나는지 확인한다.

## 응답 토큰을 용도별로 구분한다

예약 응답은 무작위 nonce, 비트 ACK는 증가하는 32비트 순번을 사용한다.
`read_response`는 크기, 소켓 경로, magic, 서버 PID, 응답 종류와 token이 모두
맞아야 성공으로 처리한다.

```c
if (!valid_source(&source, server_path)
	|| response.magic != MT_RESPONSE_MAGIC
	|| response.server_pid != server_pid || response.kind != kind
	|| response.token != token)
	return (0);
```

nonce를 0이 아닌 값으로 만들기 위해 `/dev/urandom`을 끝까지 읽고, 짧은 읽기와
`EINTR`를 처리한다. 무작위 값은 암호 인증이 아니라 늦거나 우연히 섞인 응답을
구분하는 상관관계 값이다. 비트 순번은 세션마다 0에서 시작하므로 세션 경계를
넘는 재전송 식별자로 사용할 수 없다.

## 서버 경로 확인만으로 인증이 끝나지는 않는다

응답 디렉터리는 현재 UID가 소유하고 권한이 0700이어야 한다. 클라이언트는 서버
경로가 같은 UID의 Unix socket인지도 확인한다. 이 검사는 일반 파일 덮어쓰기와
많은 오래된 응답을 막지만, 같은 UID의 다른 프로세스가 경로를 만들거나 PID를
흉내 내는 위협까지 해결하지는 않는다.

그 수준의 신뢰가 필요하면 `SO_PEERCRED` 계열의 플랫폼별 자격 증명이나 연결 기반
채널을 사용해야 한다. 현재 datagram 응답은 기밀성이나 메시지 인증을 제공하지
않는다.

```sh
make test
```
