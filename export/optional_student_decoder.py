"""Optional diagnostics must not invalidate a policy bundle that passed policy parity."""
import warnings
from pathlib import Path

def add_optional_decoder(checkpoint, output):
    output=Path(output)
    paths=[output/"student_decoder.onnx",output/"student_decoder.json"]
    if any(p.exists() for p in paths):
        raise FileExistsError("optional export requires new decoder paths")
    try:
        from gast_student_decoder import export_decoder
        meta=export_decoder(checkpoint,output)
    except (Exception,SystemExit) as exc:
        # Only these two newly-created diagnostic files can be removed.
        for p in paths:
            if p.is_file():p.unlink()
        message=f"{type(exc).__name__}: {exc}"
        warnings.warn("Optional student decoder unavailable: "+message,RuntimeWarning)
        return {"student_decoder_status":"unavailable","student_decoder_error":message},[]
    return {"student_decoder_status":"verified",
            "student_decoder_contract":"gast.student_decoder.v1",
            "student_decoder_parity_max_abs_error":meta["parity_max_abs_error"]},paths
