#!/bin/bash
# Run a command as root in the Parallels VM, retrying when Parallels does not open the session.
# `prlctl exec` fails now and then with "PrlJob_GetResult: Invalid argument" or "Unable to open new
# session" before anything runs in the guest (always while another exec is still running, and
# sometimes without one); this tries again, up to 10 times, and otherwise passes the output through.
# Commands run with umask 022: prlctl exec has 077, which made every file of the system image
# readable by root alone (SpringBoard, as mobile, then cannot open "/" and launchd restarts it forever).
#
# usage: devices/renoir/vmx.sh 'command'      (VM name: $VM, default "Ubuntu 26.04 (with Rosetta)")
VM=${VM:-Ubuntu 26.04 (with Rosetta)}
for try in 1 2 3 4 5 6 7 8 9 10; do
	out=$(prlctl exec "$VM" "umask 022; $1" 2>&1); rc=$?
	case "$out" in
	*"PrlJob_GetResult: Invalid argument"*|*"Unable to open new session"*|*"PrlJob_GetRetCode: Invalid argument"*)
		[ "$(printf '%s' "$out" | grep -cv 'Prl\|Unable to open')" -gt 0 ] && break   # it did run after all
		sleep 3; continue ;;
	esac
	break
done
printf '%s\n' "$out" | tr -d '\r'
exit $rc
