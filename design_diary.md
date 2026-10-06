# Design Diary

## 3 Oct 2026
- Set up CentOS 10 environment (gcc, make, git).
- Created GitHub repo and SSH authentication.
- Calculated personalised values from IT24101247 (see README).

- Implemented basic threaded TCP agent (port 9410) and controller with echo; verified with ss -tlnp.

## 5 Oct 2026
- Implemented read_line() with a per-connection buffer. TCP is a byte stream, so one recv() may hold a partial line or several lines; the buffer keeps leftover bytes between calls.
- Tested with multiple lines in one send and a line split across two sends (2s apart). Both worked.

## 5 Oct 2026
- Implemented read_line() with a per-connection buffer. TCP is a byte stream, so one recv() may hold a partial line or several lines; the buffer keeps leftover bytes between calls.
- Tested multiple lines in one send and a line split across two sends (2s apart). Both worked.
- Verified 6 simultaneous clients (12 ESTAB lines in ss). Model: one detached pthread per connection.
- Added AUTH with personalised token and send_response() helper so every reply ends with SID:7421.
- Added SYSINFO (/proc/loadavg, /proc/meminfo, /proc/uptime), LISTPROC (ps, kernel threads filtered with awk) and EXEC.
- Obstacle: first LISTPROC attempt returned an empty list because of a wrong ps filter; fixed with an awk filter.
- EXEC uses a fixed whitelist lookup (exec_lookup): user input never reaches the shell, so "DATE; rm -rf /" is rejected.

## 6 Oct 2026
- Added PUT/GET. PUT reads <filesize> bytes, using bytes already in the line buffer first, then recv(). GET sends the header line then the file with a send_all() loop.
- Filenames validated (letters, digits, . _ - only, no leading dot) to block path traversal. Max upload 10 MB (ERR 004).
- Verified a 100000-byte random file with cmp and md5sum: identical.
