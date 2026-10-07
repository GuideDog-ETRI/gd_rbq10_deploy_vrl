"""Make a NEW copy of a legacy bundle with a verified decoder. Never edit the source."""
import argparse
import json
from pathlib import Path
import shutil
from gast_student_decoder import export_decoder
from check_gast_bundle import sha, check_decoder


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    source,out=a.source.resolve(),a.output.resolve()
    if out.exists() or out.is_relative_to(source):
        p.error("output must be NEW and outside source")
    manifest=json.loads((source/"manifest.json").read_text())
    for name,digest in manifest["deployment_hashes"].items():
        f=(source/name).resolve()
        if f.parent!=source or sha(f)!=digest:
            p.error("source manifest mismatch: "+name)
    # Ignore old prototype decoder only in the NEW copy, never remove source files.
    shutil.copytree(source,out,ignore=shutil.ignore_patterns("student_decoder.onnx","student_decoder.json"))
    result=export_decoder(out/"student_checkpoint.pt",out)
    manifest["student_decoder_contract"]="gast.student_decoder.v1"
    manifest["student_decoder_parity_max_abs_error"]=result["parity_max_abs_error"]
    for name in ("student_decoder.onnx","student_decoder.json"):
        manifest["deployment_hashes"][name]=sha(out/name)
    check_decoder(out,manifest)
    (out/"manifest.json").write_text(json.dumps(manifest,indent=2,allow_nan=False)+"\n")
    print(json.dumps({"new_bundle":str(out),"source_unchanged":str(source),"decoder":result["parity_max_abs_error"]}))


if __name__=="__main__":main()
