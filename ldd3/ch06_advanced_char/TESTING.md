## TESTING
## SCULL SINGLE
```bash
> sudo ./scull_access_tester single /dev/scullsingle 1000 1001

=== TEST scullsingle on /dev/scullsingle ===
[proc 9751] now running as uid=1000
open(/dev/scullsingle, 0x0) -> 3 OK
[proc 9750] now running as uid=1001
open(/dev/scullsingle, 0x0) -> -1 (Device or resource busy)
OK: second open failed as expected.
^C
```

## SCULL UID
```bash
> sudo setcap cap_setuid,cap_setgid+ep ./scull_access_tester
> sudo ./scull_access_tester uid    /dev/sculluid    1000 1001

=== TEST sculluid on /dev/sculluid ===
[proc 20450] now running as ruid=0 euid=1000
open(/dev/sculluid, 0x0) -> 3 OK
[proc 20449] now running as ruid=0 euid=1000
open(/dev/sculluid, 0x0) -> 3 OK
OK: same-UID second open succeeded.
close(3) -> 0 OK
setegid: Operation not permitted


```

## SCULL WUID
```bash
sudo ./scull_access_tester wuid   /dev/scullwuid   1000 1001
```

## SCULL PRIV
```bash
sudo ./scull_access_tester priv   /dev/scullpriv
=== TEST scullpriv on /dev/scullpriv ===
[proc 14687] now running as ruid=0 euid=65534
open(/dev/scullpriv, 0x0) -> 3 OK
close(3) -> 0 OK
open(/dev/scullpriv, 0x0) -> 3 OK
OK: root open succeeded.
close(3) -> 0 OK

```
