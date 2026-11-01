"""
Minimal ctypes wrapper around ngspice's shared library (ships with KiCad).

    res = run(netlist_text)          # runs whatever analysis the netlist contains
    res["v(p3v3)"] -> numpy array, res["time"] -> numpy array

Vectors are returned lower-case, keyed both as ngspice names them ("p3v3",
"vbat#branch") and as v(node) / i(source) for readability.
"""
import ctypes as C
import os
import threading

import numpy as np

KICAD_BIN = os.environ.get("KICAD_BIN", r"C:\Program Files\KiCad\10.0\bin")

_lock = threading.Lock()
_ng = None
_log = []


class _VecInfo(C.Structure):
    _fields_ = [("v_name", C.c_char_p), ("v_type", C.c_int), ("v_flags", C.c_short),
                ("v_realdata", C.POINTER(C.c_double)), ("v_compdata", C.c_void_p), ("v_length", C.c_int)]


_SendChar = C.CFUNCTYPE(C.c_int, C.c_char_p, C.c_int, C.c_void_p)
_SendStat = C.CFUNCTYPE(C.c_int, C.c_char_p, C.c_int, C.c_void_p)
_Exit = C.CFUNCTYPE(C.c_int, C.c_int, C.c_bool, C.c_bool, C.c_int, C.c_void_p)
_BG = C.CFUNCTYPE(C.c_int, C.c_bool, C.c_int, C.c_void_p)
_callbacks = (
    _SendChar(lambda s, i, u: _log.append(s.decode(errors="replace")) or 0),
    _SendStat(lambda s, i, u: 0),
    _Exit(lambda *a: 0),
    _BG(lambda *a: 0),
)


class SpiceError(RuntimeError):
    pass


def _lib():
    global _ng
    if _ng is None:
        os.add_dll_directory(KICAD_BIN)
        _ng = C.CDLL(os.path.join(KICAD_BIN, "ngspice.dll"))
        _ng.ngGet_Vec_Info.restype = C.POINTER(_VecInfo)
        _ng.ngSpice_CurPlot.restype = C.c_char_p
        _ng.ngSpice_AllVecs.restype = C.POINTER(C.c_char_p)
        cb = _callbacks
        if _ng.ngSpice_Init(cb[0], cb[1], cb[2], None, None, cb[3], None) != 0:
            raise SpiceError("ngSpice_Init failed")
    return _ng


def run(netlist: str):
    """Run a complete netlist (title line first, .end last). Returns dict name -> ndarray."""
    ng = _lib()
    with _lock:
        _log.clear()
        ng.ngSpice_Command(b"destroy all")
        ng.ngSpice_Command(b"reset")
        lines = [l for l in netlist.splitlines() if l.strip()]
        arr = (C.c_char_p * (len(lines) + 1))(*[l.encode() for l in lines], None)
        if ng.ngSpice_Circ(arr) != 0:
            raise SpiceError("circuit load failed:\n" + "\n".join(_log[-30:]))
        if ng.ngSpice_Command(b"run") != 0:
            raise SpiceError("run failed:\n" + "\n".join(_log[-30:]))
        errs = [l for l in _log if "rror" in l and "no error" not in l.lower()]
        if errs:
            raise SpiceError("\n".join(errs[-15:]))
        plot = ng.ngSpice_CurPlot().decode()
        vecs = ng.ngSpice_AllVecs(plot.encode())
        out = {}
        i = 0
        while vecs[i]:
            name = vecs[i].decode()
            i += 1
            info = ng.ngGet_Vec_Info(f"{plot}.{name}".encode())
            if not info or not info.contents.v_realdata:
                continue
            vi = info.contents
            data = np.ctypeslib.as_array(vi.v_realdata, shape=(vi.v_length,)).copy()
            key = name.lower()
            out[key] = data
            if key.endswith("#branch"):
                out[f"i({key[:-7]})"] = data
            elif key not in ("time", "v-sweep", "frequency"):
                out[f"v({key})"] = data
        return out


def log():
    return list(_log)
