#!/usr/bin/env python3
"""Exact argv/proc inspection: reject competing senders; stop only a review folder's groups."""
import argparse
import json
import os
from pathlib import Path
import signal
import sys
import time

def option(argv, name, default=None):
    for i,a in enumerate(argv):
        if a==name and i+1<len(argv):return argv[i+1]
        if a.startswith(name+'='):return a.split('=',1)[1]
    return default

def processes():
    for d in Path('/proc').iterdir():
        if not d.name.isdigit():continue
        try:
            argv=d.joinpath('cmdline').read_bytes().decode().strip('\0').split('\0')
            if not argv or not argv[0]:continue
            # Do not match shells containing a command string, pgrep, or this inspector itself.
            executable=Path(argv[0]).name
            side=executable.startswith('python') and any(Path(a).name=='sidecar.py' for a in argv[1:3])
            force=executable.startswith('python') and any(Path(a).name=='stair_push_trial.py' for a in argv[1:3])
            pilot=executable=='CAMEL-Pilot'
            if not (side or force or pilot):continue
            env=dict(x.split('=',1) for x in d.joinpath('environ').read_bytes().decode().split('\0') if '=' in x)
            log=option(argv,'--log' if side else '--state-file','')
            if log and not os.path.isabs(log):log=str(Path(os.readlink(d/'cwd'))/log)
            yield dict(pid=int(d.name),pgid=os.getpgid(int(d.name)),kind='sidecar' if side else 'force' if force else 'pilot',
                port=option(argv,'--port',env.get('RBQ_GAST_SCAN_UDP','47610')),log=os.path.normpath(log) if log else '',argv=argv)
        except (OSError,ValueError,UnicodeError):continue

def main():
    ap=argparse.ArgumentParser();sub=ap.add_subparsers(dest='cmd',required=True)
    a=sub.add_parser('check');a.add_argument('--port',required=True)
    a=sub.add_parser('stop-folder');a.add_argument('--log-dir',required=True);a.add_argument('--dry-run',action='store_true')
    a=ap.parse_args();rows=list(processes())
    if a.cmd=='check':
        blocked=[r for r in rows if r['kind']=='pilot' or r['kind']=='sidecar' and r['port']==a.port]
        for r in blocked:print('REFUSE existing '+json.dumps(r),file=sys.stderr)
        return int(bool(blocked))
    folder=Path(a.log_dir).resolve()
    selected=[r for r in rows if (r['kind']=='sidecar' and r['log']==str(folder/'sidecar.jsonl')) or
              (r['kind']=='force' and r['log']==str(folder/'force_state.jsonl'))]
    # Keep pid-file cleanup too: a failed sidecar may leave its setsid probe wrapper alive.
    # Verify the leader's exact folder command; never trust a bare (possibly reused) PID.
    for pid_name,script_name,log_name in [('sidecar.pid','sidecar.py','sidecar.jsonl'),('force.pid','stair_push_trial.py','force_state.jsonl')]:
        try:
            pid=int((folder/pid_name).read_text().strip())
            argv=Path('/proc',str(pid),'cmdline').read_bytes().decode().strip('\0').split('\0')
            if os.getpgid(pid)==pid and script_name in ' '.join(argv) and str(folder/log_name) in ' '.join(argv):
                if not any(r['pid']==pid for r in selected):
                    selected.append(dict(pid=pid,pgid=pid,kind='verified-pidfile-wrapper',log=str(folder/log_name),argv=argv))
        except (OSError,ValueError,UnicodeError):pass
    groups={r['pgid'] for r in selected}
    forbidden={os.getpgrp(),os.getpgid(os.getppid())}
    for r in selected:print('matched folder owner '+json.dumps(r))
    if groups & forbidden:
        print('REFUSE shared caller process group',file=sys.stderr);return 2
    if a.dry_run:return 0
    for group in groups:
        try:os.killpg(group,signal.SIGTERM)
        except ProcessLookupError:pass
    deadline=time.monotonic()+5
    while time.monotonic()<deadline:
        remaining=[r for r in processes() if r['pgid'] in groups]
        if not remaining:return 0
        time.sleep(.1)
    for group in groups:
        # Recheck exact folder owner before escalation, avoiding a reused group.
        if any(r['pgid']==group and r['log'] in (str(folder/'sidecar.jsonl'),str(folder/'force_state.jsonl')) for r in processes()):
            try:os.killpg(group,signal.SIGKILL)
            except ProcessLookupError:pass
    return 0
if __name__=='__main__':sys.exit(main())
