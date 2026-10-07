# Prompt Log

| # | Date | Tool | Prompt (summary) | How output was used / changed |
|---|------|------|------------------|-------------------------------|
| 1 | 3 Oct 2026 | Claude | Asked for step-by-step plan for the assignment | Used as a roadmap |
| 2 | 3 Oct 2026 | Claude | Gave reg no., asked for personalised values + setup + basic TCP code | Verified calculations by hand |
| 3 | 3 Oct 2026 | Claude | Asked for CentOS 10 setup steps, fixed dnf/SSH issues | Followed steps, skipped full system update |
| 4 | 3 Oct 2026 | Claude | Asked for basic TCP agent/controller code | Compiled and tested on CentOS 10; removed unused defines |
| 5 | 5 Oct 2026 | Claude | Asked for line framing (read_line) with per-connection buffer | Tested with nc (multi-line + partial line); understood memchr/memmove logic |
| 5 | 5 Oct 2026 | Claude | Asked for line framing (read_line) with per-connection buffer | Tested with nc (multi-line + partial line); understood memchr/memmove logic |
| 6 | 5 Oct 2026 | Claude | Asked how to test 5+ simultaneous clients | Used nc loop + ss to prove 6 ESTAB connections |
| 7 | 5 Oct 2026 | Claude | Asked for AUTH + SID-tagged responses | Tested with nc, checked every reply ends with SID:7421 |
| 8 | 5 Oct 2026 | Claude | Asked for SYSINFO, LISTPROC, EXEC | LISTPROC filter was wrong (empty output), fixed with awk; tested injection attempts on EXEC |
| 9 | 6 Oct 2026 | Claude | Asked for PUT/GET with exact byte counting | Tested with nc + random 100 KB file; verified with cmp/md5sum |
| 10 | 6 Oct 2026 | Claude | Asked for interactive controller with PUT/GET | Tested with cmp/md5sum |
| 11 | 7 Oct 2026 | Claude | Asked for UDP MONITOR (agent + controller) | First patch was messy, discarded; applied the clean one and tested |
| 12 | 7 Oct 2026 | Claude | Asked for timestamped logging | Checked the log output; token is masked |
