# RemoteOps (IE3090 Network Programming)

**Registration number:** IT24101247

| Item | Value |
|---|---|
| Agent port | 9410 (7000 + 2410) |
| Source files | agent_247.c, controller_247.c, Makefile_247 |
| SID tag | SID:7421 |
| Auth token | OPS-1247 |
| Log file | remoteops_IT24101247.log |
| Storage path | ./agentfiles/IT24101247/<filename> |
| ZIP archive | IE3090_IT24101247.zip |

## Build
    make -f Makefile_247

## Concurrency model
One detached pthread per accepted connection. Each thread owns its own receive buffer (conn_t), so no shared state between clients.

## Features
AUTH, SYSINFO, LISTPROC, EXEC (whitelist), PUT, GET, MONITOR START/STOP (UDP, every 3 s), QUIT, timestamped logging to remoteops_IT24101247.log.

## Run
    make -f Makefile_247
    ./agent_247
    ./controller_247 [agent-ip]
