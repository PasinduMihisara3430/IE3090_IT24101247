# Design Diary

## 3 Oct 2026
- Set up CentOS 10 environment (gcc, make, git).
- Created GitHub repo and SSH authentication.
- Calculated personalised values from IT24101247 (see README).

- Implemented basic threaded TCP agent (port 9410) and controller with echo; verified with ss -tlnp.

## 5 Oct 2026
- Implemented read_line() with a per-connection buffer. TCP is a byte stream, so one recv() may hold a partial line or several lines; the buffer keeps leftover bytes between calls.
- Tested with multiple lines in one send and a line split across two sends (2s apart). Both worked.
