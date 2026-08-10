# mypaas-statd

Linux-native runtime telemetry daemon for MyPaaS.

This repository is being bootstrapped. The first implementation target is a small C17 daemon that samples cgroup v2 metrics and exposes bounded snapshots to the MyPaaS Go control plane over a Unix domain socket.
