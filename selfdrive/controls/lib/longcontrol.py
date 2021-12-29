from cereal import car
from common.numpy_fast import clip, interp
from common.realtime import DT_CTRL
from selfdrive.controls.lib.pid import PIDController
from selfdrive.controls.lib.drive_helpers import CONTROL_N
from selfdrive.modeld.constants import T_IDXS
from selfdrive.ntune import ntune_scc_get

LongCtrlState = car.CarControl.Actuators.LongControlState

# As per ISO 15622:2018 for all speeds
ACCEL_MIN_ISO = -3.5  # m/s^2
ACCEL_MAX_ISO = 2.0  # m/s^2


def long_control_state_trans(CP, active, long_control_state, v_ego, v_target_future,
                             v_target, output_accel, brake_pressed, cruise_standstill, radarState):
  """Update longitudinal control state machine"""
  accelerating = v_target_future > v_target
  stopping_condition = (v_ego < 2.0 and cruise_standstill) or \
                       (v_ego < CP.vEgoStopping and
                        ((v_target_future < CP.vEgoStopping and not accelerating) or brake_pressed))
  
  starting_condition = v_target_future > CP.vEgoStarting and accelerating and not cruise_standstill

  # neokii
  if radarState is not None and radarState.leadOne is not None and radarState.leadOne.status:
    starting_condition = starting_condition and radarState.leadOne.vLead > CP.vEgoStarting

  if not active:
    long_control_state = LongCtrlState.off

  else:
    if long_control_state == LongCtrlState.off:
      if active:
        long_control_state = LongCtrlState.pid

    elif long_control_state == LongCtrlState.pid:
      if stopping_condition:
        long_control_state = LongCtrlState.stopping

    elif long_control_state == LongCtrlState.stopping:
      if starting_condition:
        long_control_state = LongCtrlState.starting

    elif long_control_state == LongCtrlState.starting:
      if stopping_condition:
        long_control_state = LongCtrlState.stopping
      elif output_accel >= CP.startAccel:
        long_control_state = LongCtrlState.pid

  return long_control_stat


class LongControl():
  def __init__(self, CP):
    self.long_control_state = LongCtrlState.off  # initialized to off
    self.pid = PIDController((CP.longitudinalTuning.kpBP, CP.longitudinalTuning.kpV),
                            (CP.longitudinalTuning.kiBP, CP.longitudinalTuning.kiV),
                            (CP.longitudinalTuning.kdBP, CP.longitudinalTuning.kdV),
                            rate=1 / DT_CTRL,
                            derivative_period=0.5)
    self.v_pid = 0.0
    self.last_output_accel = 0.0

  def reset(self, v_pid):
    """Reset PID controller and change setpoint"""
    self.pid.reset()
    self.v_pid = v_pid

  def update(self, active, CS, CP, long_plan, accel_limits, radarState):
    """Update longitudinal control. This updates the state machine and runs a PID loop"""
    # Interp control trajectory
    # TODO estimate car specific lag, use .15s for now
    if len(long_plan.speeds) == CONTROL_N:

      longitudinalActuatorDelayLowerBound = ntune_scc_get('longitudinalActuatorDelayLowerBound')
      longitudinalActuatorDelayUpperBound = ntune_scc_get('longitudinalActuatorDelayUpperBound')

      v_target_lower = interp(longitudinalActuatorDelayLowerBound, T_IDXS[:CONTROL_N], long_plan.speeds)
      a_target_lower = 2 * (v_target_lower - long_plan.speeds[0])/longitudinalActuatorDelayLowerBound - long_plan.accels[0]

      v_target_upper = interp(longitudinalActuatorDelayUpperBound, T_IDXS[:CONTROL_N], long_plan.speeds)
      a_target_upper = 2 * (v_target_upper - long_plan.speeds[0])/longitudinalActuatorDelayUpperBound - long_plan.accels[0]
     
      v_target = long_plan.speeds[0]
      v_target_future = long_plan.speeds[-1]
      a_target = min(a_target_lower, a_target_upper)
    else:
      v_target = 0.0
      v_target_future = 0.0
      a_target = 0.0

    if a_target > 0.:
      a_target *= interp(CS.vEgo, [0., 3.], [1.2, 1.0])

    # TODO: This check is not complete and needs to be enforced by MPC
    a_target = clip(a_target, ACCEL_MIN_ISO, ACCEL_MAX_ISO)

    self.pid.neg_limit = accel_limits[0]
    self.pid.pos_limit = accel_limits[1]

    # Update state machine
    output_accel = self.last_output_accel
    self.long_control_state = long_control_state_trans(CP, active, self.long_control_state, CS.vEgo,
                                                       v_target_future, v_target, output_accel,
                                                       CS.brakePressed, CS.cruiseState.standstill, radarState)

    if self.long_control_state == LongCtrlState.off or CS.gasPressed:
      self.reset(CS.vEgo)
      output_accel = 0.

    # tracking objects and driving
    elif self.long_control_state == LongCtrlState.pid:
      self.v_pid = v_target

      # Toyota starts braking more when it thinks you want to stop
      # Freeze the integrator so we don't accelerate to compensate, and don't allow positive acceleration
      prevent_overshoot = not CP.stoppingControl and CS.vEgo < 1.5 and v_target_future < 0.7 and v_target_future < self.v_pid
      deadzone = interp(CS.vEgo, CP.longitudinalTuning.deadzoneBP, CP.longitudinalTuning.deadzoneV)
      freeze_integrator = prevent_overshoot

      output_accel = self.pid.update(self.v_pid, CS.vEgo, speed=CS.vEgo, deadzone=deadzone, feedforward=a_target, freeze_integrator=freeze_integrator)

      if prevent_overshoot:
        output_accel = min(output_accel, 0.0)

    # Intention is to stop, switch to a different brake control until we stop
    elif self.long_control_state == LongCtrlState.stopping:
      # Keep applying brakes until the car is stopped
      if not CS.standstill or output_accel > CP.stopAccel:
        output_accel -= CP.stoppingDecelRate * DT_CTRL * \
                        interp(output_accel, [CP.stopAccel, CP.stopAccel/2., 0], [0.3, 0.65, 1.2])
      output_accel = clip(output_accel, accel_limits[0], accel_limits[1])

      self.reset(CS.vEgo)

    # Intention is to move again, release brake fast before handing control to PID
    elif self.long_control_state == LongCtrlState.starting:
      if output_accel < CP.startAccel:
        output_accel += CP.startingAccelRate * DT_CTRL
      self.reset(CS.vEgo)

    self.last_output_accel = output_accel
    final_accel = clip(output_accel, accel_limits[0], accel_limits[1])

    return final_accel
