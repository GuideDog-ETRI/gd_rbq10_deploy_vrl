import subprocess as sp
import signal
import json
from pathlib import Path
from datetime import datetime

repo=Path('/home/user/gd_project/gd_rbq10_deploy_vrl')
out=Path(__file__).parent/('codex_course_'+datetime.now().strftime('%Y%m%d_%H%M%S'))
out.mkdir()
print('OUTPUT',out,flush=True)
runner=repo/'gast/runtime/experiments/runners/sim_trial.py'
rec=probe=None
try:
    with open(out/'record.log','w') as rf, open(out/'state.jsonl','w') as sf, open(out/'state.err','w') as se:
        rec=sp.Popen(['gst-launch-1.0','-e','ximagesrc','display-name=:2','xid=2097159','use-damage=false','!','video/x-raw,framerate=15/1','!','videoconvert','!','videoscale','!','video/x-raw,width=1258,height=480','!','vp8enc','deadline=1','target-bitrate=3000000','!','webmmux','!','filesink','location='+str(out/'walk.webm')],stdout=rf,stderr=sp.STDOUT)
        probe=sp.Popen([str(repo/'gast/runtime/build/tools/sim-state-probe'),'600'],stdout=sf,stderr=se)
        for cmd,seconds in [('start',10),('stand',20),('walk',550)]:
            args=['python3',str(runner),'--command',cmd,'--seconds',str(seconds),'--output',str(out/(cmd+'.json'))]
            if cmd=='walk': args+=['--vx','0.20','--state-file',str(out/'state.jsonl'),'--stop-x','28.5']
            print('RUN',cmd,flush=True)
            with open(out/(cmd+'.log'),'w') as log:
                result=sp.run(args,stdout=log,stderr=sp.STDOUT)
            if result.returncode: raise RuntimeError(cmd+' failed: '+(out/(cmd+'.log')).read_text()[-2000:])
            data=json.loads((out/(cmd+'.json')).read_text())
            print(cmd,'aborted=',data['aborted'],'goal=',data['goal_reached'],flush=True)
finally:
    if rec and rec.poll() is None:
        rec.send_signal(signal.SIGINT)
        rec.wait(timeout=30)
    if probe and probe.poll() is None:
        probe.terminate()
        probe.wait(timeout=10)
print('DONE',out,flush=True)
