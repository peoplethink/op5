#!/usr/bin/env python3
import os
import sys
import time
from typing import Any

from tqdm import tqdm

import cereal.messaging as messaging
from cereal.visionipc.visionipc_pyx import VisionIpcServer, VisionStreamType  # pylint: disable=no-name-in-module, import-error
from common.spinner import Spinner
from common.timeout import Timeout
from common.transformations.camera import get_view_frame_from_road_frame, eon_f_frame_size, tici_f_frame_size, \
                                                                        eon_d_frame_size, tici_d_frame_size
from selfdrive.hardware import PC, TICI
from selfdrive.manager.process_config import managed_processes
from selfdrive.test.openpilotci import BASE_URL, get_url
from selfdrive.test.process_replay.compare_logs import compare_logs, save_log
from selfdrive.test.process_replay.test_processes import format_diff
from selfdrive.version import get_commit
from tools.lib.framereader import FrameReader
from tools.lib.logreader import LogReader

if TICI:
  TEST_ROUTE = "4cf7a6ad03080c90|2021-09-29--13-46-36"
else:
  TEST_ROUTE = "303055c0002aefd1|2021-11-22--18-36-32"

CACHE_DIR = os.getenv("CACHE_DIR", None)

packet_from_camera = {"roadCameraState":"modelV2", "driverCameraState":"driverState"}

def get_log_fn(ref_commit):
  return "%s_%s_%s.bz2" % (TEST_ROUTE, "model_tici" if TICI else "model", ref_commit)

def replace_calib(msg, calib):
  msg = msg.as_builder()
  if calib is not None:
    msg.liveCalibration.extrinsicMatrix = get_view_frame_from_road_frame(*calib, 1.22).flatten().tolist()
  return msg

def process_frame(msg, pm, sm, log_msgs, vipc_server, spinner, frs, frame_idxs, last_desire):
  if msg.which() == "roadCameraState" and last_desire is not None:
    dat = messaging.new_message('lateralPlan')
    dat.lateralPlan.desire = last_desire
    pm.send('lateralPlan', dat)

  f = msg.as_builder()
  pm.send(msg.which(), f)

  img = frs[msg.which()].get(frame_idxs[msg.which()], pix_fmt="yuv420p")[0]
  if msg.which == "roadCameraState":
    vipc_server.send(VisionStreamType.VISION_STREAM_ROAD, img.flatten().tobytes(), f.roadCameraState.frameId,
                      f.roadCameraState.timestampSof, f.roadCameraState.timestampEof)
  else:
    vipc_server.send(VisionStreamType.VISION_STREAM_DRIVER, img.flatten().tobytes(), f.driverCameraState.frameId,
                      f.driverCameraState.timestampSof, f.driverCameraState.timestampEof)
  with Timeout(seconds=15):
    log_msgs.append(messaging.recv_one(sm.sock[packet_from_camera[msg.which()]]))

  frame_idxs[msg.which()] += 1
  if frame_idxs[msg.which()] >= frs[msg.which()].frame_count:
    return None
  update_spinner(spinner, frame_idxs['roadCameraState'], frs['roadCameraState'].frame_count,
                                         frame_idxs['driverCameraState'], frs['driverCameraState'].frame_count)
  return 0

def update_spinner(s, fidx, fcnt, didx, dcnt):
  s.update("replaying models:  road %d/%d,  driver %d/%d" % (fidx, fcnt, didx, dcnt))

def model_replay(lr_list, frs):
  spinner = Spinner()
  spinner.update("starting model replay")

  vipc_server = VisionIpcServer("camerad")
  vipc_server.create_buffers(VisionStreamType.VISION_STREAM_ROAD, 40, False, *(tici_f_frame_size if TICI else eon_f_frame_size))
  vipc_server.create_buffers(VisionStreamType.VISION_STREAM_DRIVER, 40, False, *(tici_d_frame_size if TICI else eon_d_frame_size))
  vipc_server.start_listener()

  pm = messaging.PubMaster(['roadCameraState', 'driverCameraState', 'liveCalibration', 'lateralPlan'])
  sm = messaging.SubMaster(['modelV2', 'driverState'])

  try:
    managed_processes['modeld'].start()
    managed_processes['dmonitoringmodeld'].start()
    time.sleep(5)
    sm.update(1000)

    last_desire = None
    log_msgs = []
    frame_idxs = dict.fromkeys(['roadCameraState','driverCameraState'], 0)

    cal = [msg for msg in lr if msg.which() == "liveCalibration"]
    for msg in cal[:5]:
      pm.send(msg.which(), replace_calib(msg, None))

    for msg in tqdm(lr_list):
      if msg.which() == "liveCalibration":
        last_calib = list(msg.liveCalibration.rpyCalib)
        pm.send(msg.which(), replace_calib(msg, last_calib))
      elif msg.which() == "lateralPlan":
        last_desire = msg.lateralPlan.desire
      elif msg.which() in ["roadCameraState", "driverCameraState"]:
        ret = process_frame(msg, pm, sm, log_msgs, vipc_server, spinner, frs, frame_idxs, last_desire)
        if ret is None:
          break

  except KeyboardInterrupt:
    pass
  finally:
    spinner.close()
    managed_processes['modeld'].stop()
    managed_processes['dmonitoringmodeld'].stop()

  return log_msgs

if __name__ == "__main__":

  update = "--update" in sys.argv

  if TICI:
    os.system('sudo mount -o remount,size=200M /tmp') # c3 hevcs are 75M each

  replay_dir = os.path.dirname(os.path.abspath(__file__))
  ref_commit_fn = os.path.join(replay_dir, "model_replay_ref_commit")

  segnum = 0
  frs = {}
  if CACHE_DIR:
    lr = LogReader(os.path.join(CACHE_DIR, '%s--%d--rlog.bz2' % (TEST_ROUTE.replace('|', '_'), segnum)))
    frs['roadCameraState'] = FrameReader(os.path.join(CACHE_DIR, '%s--%d--fcamera.hevc' % (TEST_ROUTE.replace('|', '_'), segnum)))
    frs['driverCameraState'] = FrameReader(os.path.join(CACHE_DIR, '%s--%d--dcamera.hevc' % (TEST_ROUTE.replace('|', '_'), segnum)))
  else:
    lr = LogReader(get_url(TEST_ROUTE, segnum))
    frs['roadCameraState'] = FrameReader(get_url(TEST_ROUTE, segnum, log_type="fcamera"))
    frs['driverCameraState'] = FrameReader(get_url(TEST_ROUTE, segnum, log_type="dcamera"))

  lr_list = list(lr)
  log_msgs = model_replay(lr_list, frs)

  failed = False
  if not update:
    ref_commit = open(ref_commit_fn).read().strip()
    log_fn = get_log_fn(ref_commit)
    cmp_log = LogReader(BASE_URL + log_fn)

    ignore = ['logMonoTime', 'valid',
              'modelV2.frameDropPerc',
              'modelV2.modelExecutionTime',
              'driverState.modelExecutionTime',
              'driverState.dspExecutionTime']
    tolerance = None if not PC else 1e-3
    results: Any = {TEST_ROUTE: {}}
    results[TEST_ROUTE]["models"] = compare_logs(cmp_log, log_msgs, tolerance=tolerance, ignore_fields=ignore)
    diff1, diff2, failed = format_diff(results, ref_commit)

    print(diff2)
    print('-------------')
    print('-------------')
    print('-------------')
    print('-------------')
    print('-------------')
    print(diff1)
    with open("model_diff.txt", "w") as f:
      f.write(diff2)

  if update or failed:
    from selfdrive.test.openpilotci import upload_file

    print("Uploading new refs")

    new_commit = get_commit()
    log_fn = get_log_fn(new_commit)
    save_log(log_fn, log_msgs)
    try:
      upload_file(log_fn, os.path.basename(log_fn))
    except Exception as e:
      print("failed to upload", e)

    with open(ref_commit_fn, 'w') as f:
      f.write(str(new_commit))

    print("\n\nNew ref commit: ", new_commit)

  sys.exit(int(failed))
