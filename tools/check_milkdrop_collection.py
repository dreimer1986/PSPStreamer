#!/usr/bin/env python3
"""Repeatable host parse/evaluation audit; not a PSP rendering test."""
import argparse
import ctypes
import json
import math
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))

class Vertex(ctypes.Structure):
    _fields_=[('u',ctypes.c_float),('v',ctypes.c_float),('color',ctypes.c_uint),
              ('x',ctypes.c_float),('y',ctypes.c_float),('z',ctypes.c_float)]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('collection', type=Path)
    parser.add_argument('--frames', type=int, default=0)
    parser.add_argument('--expanded-shapes',action='store_true',help='Exercise the batched renderer shape evaluator')
    parser.add_argument('--source-root', type=Path, default=ROOT,
                        help='Compile another source snapshot for a like-for-like baseline')
    args = parser.parse_args()
    if args.frames<0:
        parser.error('--frames must be nonnegative')
    os.environ['PSP_MILKDROP_SOURCE_ROOT']=str(args.source_root.resolve())
    from test_milkdrop_preset import (PresetTests, Preset, PresetState, Error,
                                     Signal, Warp, Decor, ShapeFrame, GRID_POINTS, CUSTOM_POINTS)
    class Geometry(ctypes.Structure):
        _fields_=[('count',ctypes.c_int),('vertices',Vertex*CUSTOM_POINTS)]
    PresetTests.setUpClass()
    test = PresetTests()
    reason=getattr(test.library,'pm_audit_reason',None)
    reset_reason=getattr(test.library,'pm_audit_reset',None)
    if reason:reason.restype=ctypes.c_char_p
    records = []
    pixel=test.library.md_eval_pixel_grid
    pixel.argtypes=[ctypes.POINTER(Preset),ctypes.POINTER(Warp),ctypes.c_float,ctypes.POINTER(Signal),
                    ctypes.POINTER(PresetState),ctypes.POINTER(Warp),ctypes.POINTER(Error)]
    waves=test.library.md_eval_custom_waves
    waves.argtypes=[ctypes.POINTER(Preset),ctypes.c_float,ctypes.POINTER(Signal),
                    ctypes.POINTER(ctypes.c_short),ctypes.POINTER(ctypes.c_short),
                    ctypes.POINTER(ctypes.c_float),ctypes.POINTER(ctypes.c_float),
                    ctypes.POINTER(PresetState),ctypes.POINTER(Geometry),ctypes.POINTER(Error)]
    right=(ctypes.c_short*576)(*[int(12000*math.sin(i*.09)) for i in range(576)])
    left=(ctypes.c_short*576)(*[int(16000*math.sin(i*.07)) for i in range(576)])
    spectrum=(ctypes.c_float*512)(*[.2/(1+i*.05) for i in range(512)])
    points=(Warp*GRID_POINTS)();geometry=(Geometry*4)()
    expanded=None
    if args.expanded_shapes:
        expanded=test.library.md_eval_preset_shapes
        expanded.argtypes=[ctypes.POINTER(Preset),ctypes.c_float,ctypes.POINTER(Signal),ctypes.POINTER(PresetState),
            ctypes.POINTER(Warp),ctypes.POINTER(ctypes.c_uint),ctypes.POINTER(Decor),ctypes.POINTER(Error),ctypes.POINTER(ShapeFrame)]
    shape_frame=ShapeFrame();decor=Decor();color=ctypes.c_uint()
    try:
        for path in sorted(args.collection.rglob('*.milk')):
            if reset_reason:reset_reason()
            preset, error = Preset(), Error()
            code = test.load(str(path).encode(), ctypes.byref(preset), ctypes.byref(error))
            record = {'file': str(path.relative_to(args.collection)), 'parse': code,
                      'line': error.line, 'key': error.key.decode(errors='replace')}
            if code and reason:record['failure_reason']=reason().decode()
            if code == 0 and args.frames:
                test.library.pm_reset_globals()
                state = PresetState()
                for frame in range(args.frames):
                    signal = Signal((ctypes.c_float*13)(*([.5]*7+
                        [1+.35*math.sin(frame*.11+i) for i in range(6)])))
                    test.library.pm_begin_frame()
                    stage='frame/shape'
                    if expanded:
                        warp=Warp()
                        code=expanded(ctypes.byref(preset),frame/30,ctypes.byref(signal),ctypes.byref(state),
                            ctypes.byref(warp),ctypes.byref(color),ctypes.byref(decor),ctypes.byref(error),ctypes.byref(shape_frame))
                    else:
                        code, warp, error = test.evaluate_state(preset, state, frame/30, signal)
                    if not code and preset.pixel_program.count:
                        stage='pixel'
                        code=pixel(ctypes.byref(preset),ctypes.byref(warp),frame/30,ctypes.byref(signal),
                                   ctypes.byref(state),points,ctypes.byref(error))
                    if not code and any(w.enabled for w in preset.waves):
                        stage='custom wave'
                        code=waves(ctypes.byref(preset),frame/30,ctypes.byref(signal),right,left,spectrum,spectrum,
                                   ctypes.byref(state),geometry,ctypes.byref(error))
                    if code:
                        break
                record.update(frame_eval=code, frame=frame, stage=stage, eval_line=error.line,
                              eval_key=error.key.decode(errors='replace'))
                if code:
                    record['frame_fuel_remaining']=test.library.pm_frame_remaining()
                    if reason:record['failure_reason']=reason().decode()
                if expanded:record['shape_instances']=list(shape_frame.count)
            records.append(record)
        print(json.dumps({'total': len(records), 'parse_ok': sum(r['parse']==0 for r in records),
                          'frames_tested': args.frames, 'records': records}, ensure_ascii=False, indent=2))
    finally:
        PresetTests.tearDownClass()

if __name__ == '__main__':
    main()
