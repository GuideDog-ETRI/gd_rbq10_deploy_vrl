"""CPU-only decoder recording validation, masked metrics and static offline playback."""
import argparse
import html
import json
from pathlib import Path
import numpy as np

SHAPES = {"frames": (4,2,45,80), "hidden": (6116,), "latent": (32,), "decoded": (187,6)}


def display_cell(row, col):
    if not 0 <= row < 11 or not 0 <= col < 17:
        raise ValueError("invalid grid cell")
    return 16-col, 10-row


def read_frame(path):
    record = json.loads(Path(path).read_text(), parse_constant=lambda x: (_ for _ in ()).throw(ValueError(x)))
    if record.get("schema") != "gast.decoder.frame.v1" or record.get("replica") != 1:
        raise ValueError("unsupported schema or non-replica record")
    for key, shape in SHAPES.items():
        value=np.asarray(record[key], dtype=np.float32)
        if value.size != int(np.prod(shape)) or not np.isfinite(value).all():
            raise ValueError(f"invalid {key}")
        record[key]=value.reshape(shape)
    for key in ("sequence","input_stamp_ms","observer_ms"):
        record[key]=int(record[key])
    return record


def metrics(decoded, targets, mask):
    """Masks MUST be from gd_lab.gast.geometry.targets at matching capture pose/time.

    This function cannot establish temporal alignment or infer semantic gaps from depth.
    """
    p,y,m=map(np.asarray,(decoded,targets,mask))
    if p.shape != (187,6) or y.shape != p.shape or m.shape != p.shape:
        raise ValueError("expected three [187,6] arrays")
    if not np.isfinite(p).all() or not np.isfinite(y[m.astype(bool)]).all():
        raise ValueError("non-finite supervised values")
    out={}
    for ch,name in enumerate(("height","visibility","gap","step_up","step_down","support")):
        valid=m[:,ch].astype(bool);n=int(valid.sum())
        if ch==0:
            out[name]={"cells":n,"mae_m":float(np.abs(p[valid,ch]-y[valid,ch]).mean()) if n else None}
        else:
            pred=p[valid,ch]>=.5;truth=y[valid,ch]>=.5
            union=int((pred|truth).sum());intersection=int((pred&truth).sum())
            out[name]={"cells":n,"intersection":intersection,"union":union,
                       "iou":intersection/union if union else None}
    return out


def render(records, output):
    """One standalone HTML; no DDS, GPU, server, original frame or Pilot access."""
    data=[{k:(v.tolist() if isinstance(v,np.ndarray) else v) for k,v in r.items()
           if k in ("sequence","input_stamp_ms","observer_ms","frames","decoded","latent")} for r in records]
    payload=json.dumps(data,allow_nan=False,separators=(",",":")).replace("<","\\u003c")
    page='''<!doctype html><meta charset="utf-8"><title>Student decoder replica</title>
<h2>Read-only replica — NOT Pilot internal state</h2><p>Oracle and capture pose unavailable. Predictions are auxiliary outputs, not measured ground truth.</p>
<p>Top: depth / IR, BT0..3. Grid top row: height, visibility, gap; bottom: step up, step down, support.
Grid forward=up, left=+y. Height range -0.25..+0.35m (blue high to red low); others probability 0..1.
Low visibility cells are dimmed. No measured foot positions.</p>
<input id="step" type="range" min="0" value="0"><span id="label"></span><br>
<canvas id="camera" width="640" height="180"></canvas><br><canvas id="grid" width="660" height="510"></canvas>
<script>const rows=DATA; const slider=document.getElementById('step');slider.max=rows.length-1;
function draw(){const r=rows[+slider.value];document.getElementById('label').textContent='seq '+r.sequence+' input '+r.input_stamp_ms+' ms (observer clock)';
const c=document.getElementById('camera').getContext('2d');for(let cam=0;cam<4;cam++)for(let ch=0;ch<2;ch++)for(let y=0;y<45;y++)for(let x=0;x<80;x++){
let v=Math.max(0,Math.min(1,r.frames[cam][ch][y][x]));c.fillStyle='rgb('+[v*255,v*255,v*255].join(',')+')';c.fillRect(cam*160+x*2,ch*90+y*2,2,2)}
const g=document.getElementById('grid').getContext('2d');g.clearRect(0,0,660,510);
for(let ch=0;ch<6;ch++)for(let row=0;row<11;row++)for(let col=0;col<17;col++){
const d=r.decoded[row*17+col];let v=ch===0?(d[0]+.25)/.60:d[ch];v=Math.max(0,Math.min(1,v));
g.globalAlpha=ch!==1&&d[1]<.5?.25:1;g.fillStyle='hsl('+((1-v)*240)+',75%,50%)';
g.fillRect((ch%3)*220+(10-row)*20,Math.floor(ch/3)*255+(16-col)*15,19,14)}g.globalAlpha=1;}
slider.oninput=draw;draw();</script>'''.replace("DATA",payload)
    with Path(output).open("x") as f:f.write(page)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory");p.add_argument("--output",required=True)
    p.add_argument("--start",type=int,default=0);p.add_argument("--limit",type=int,default=128)
    a=p.parse_args()
    if a.start<0 or not 1<=a.limit<=256:p.error("start>=0, limit in [1,256] required")
    files=sorted(Path(a.directory).glob("frame_*.json"))[a.start:a.start+a.limit]
    if not files: p.error("no frames")
    # Explicit cap avoids unexpectedly loading a multi-hour recording into RAM.
    records=[read_frame(f) for f in files]
    gaps=[(a["sequence"],b["sequence"]) for a,b in zip(records,records[1:]) if b["sequence"]!=a["sequence"]+1]
    render(records,a.output)
    print(json.dumps({"frames":len(records),"sequence_gaps":gaps,"output":a.output,"oracle":"unavailable"}))


if __name__=="__main__":main()
