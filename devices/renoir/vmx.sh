#!/bin/bash
# Run a command as root in the Parallels VM, retrying when Parallels does not open the session.
# `prlctl exec` fails now and then with "PrlJob_GetResult: Invalid argument" or "Unable to open new
# session" before anything runs in the guest (always while another exec is still running, and
# sometimes without one); this tries again, up to 10 times, and otherwise passes the output through.
#
# usage: devices/renoir/vmx.sh 'command'      (VM name: $VM, default "Ubuntu 26.04 (with Rosetta)")
VM=${VM:-Ubuntu 26.04 (with Rosetta)}
for try in 1 2 3 4 5 6 7 8 9 10; do
	out=$(prlctl exec "$VM" "$1" 2>&1); rc=$?
	case "$out" in
	*"PrlJob_GetResult: Invalid argument"*|*"Unable to open new session"*|*"PrlJob_GetRetCode: Invalid argument"*)
		[ "$(printf '%s' "$out" | grep -cv 'Prl\|Unable to open')" -gt 0 ] && break   # it did run after all
		sleep 3; continue ;;
	esac
	break
done
printf '%s\n' "$out" | tr -d '\r'
exit $rc
