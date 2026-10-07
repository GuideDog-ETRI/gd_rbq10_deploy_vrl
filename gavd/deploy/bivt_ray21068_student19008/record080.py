import subprocess as sp
import signal
import json
from pathlib import Path
from datetime import datetime

here = Path(__file__).resolve().parent
repo = here.parents[2]
out = here / ('record080_' + datetime.now().strftime('%Y%m%d_%H%M%S'))
out.mkdir()
print('OUTPUT', out, flush=True)
rec = probe = None
try:
    with open(out/'record.log','w') as rf, open(out/'state.jsonl','w') as sf, open(out/'state.err','w') as se:
        rec = sp.Popen(['gst-launch-1.0','-e','ximagesrc','display-name=:2','xid=2097159','use-damage=false','!','video/x-raw,framerate=15/1','!','videoconvert','!','videoscale','!','video/x-raw,width=1258,height=480','!','vp8enc','deadline=1','target-bitrate=3000000','!','webmmux','!','filesink','location='+str(out/'walk.webm')],stdout=rf,stderr=sp.STDOUT)
        probe = sp.Popen([str(repo/'build/tools/sim-state-probe'),'600'],stdout=sf,stderr=se)
        for cmd,seconds in [('start',10),('stand',20),('walk',550)]:
            if rec.poll() is not None:
                raise RuntimeError('Recorder exited: '+(out/'record.log').read_text())
            args = ['python3',str(here/'trial080.py'),'--command',cmd,'--seconds',str(seconds),'--output',str(out/(cmd+'.json'))]
            if cmd == 'walk':
                args += ['--vx','0.8','--state-file',str(out/'state.jsonl'),'--stop-x','40.5']
            print('RUN', cmd, flush=True)
            with open(out/(cmd+'.log'),'w') as log:
                result = sp.run(args, stdout=log, stderr=sp.STDOUT)
            if result.returncode:
                raise RuntimeError((out/(cmd+'.log')).read_text()[-2000:])
            data = json.loads((out/(cmd+'.json')).read_text())
            print(cmd, 'aborted=',data['aborted'],'goal=',data['goal_reached'],flush=True)
            if data['aborted']:
                break
finally:
    if rec and rec.poll() is None:
        rec.send_signal(signal.SIGINT)
        rec.wait(timeout=30)
    if probe and probe.poll() is None:
        probe.terminate()
        probe.wait(timeout=10)
ffmpeg = '/home/user/workspace/venv_apptainer/lib/python3.11/site-packages/imageio_ffmpeg/binaries/ffmpeg-linux-x86_64-v7.0.2'
mp4 = out/'gavd19008_walk_080.mp4'
sp.run([ffmpeg,'-nostdin','-i',str(out/'walk.webm'),'-c:v','libx264','-threads','2','-preset','fast','-crf','21','-pix_fmt','yuv420p','-movflags','+faststart',str(mp4)],check=True)
sp.run([ffmpeg,'-v','error','-i',str(mp4),'-f','null','-'],check=True)
print('MP4',mp4,flush=True)
