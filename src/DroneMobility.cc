/*
 * DroneMobility.cc
 *
 * Implementation of the simple "fly to target, then stop" mobility shared
 * by both drones and MBSs. We piggyback on INET's MovingMobilityBase to
 * inherit periodic position updates and the IMobility query surface used
 * by the apps (DroneApp::hasConnection, MainNodeApp's distance math).
 */
#include "DroneMobility.h"

using namespace omnetpp;

namespace uavswarmta {

Define_Module(DroneMobility);

// Two-stage init. We only need stage LOCAL: start the module in a stopped
// state so it doesn't spin the update timer until setTarget() switches it
// on. MovingMobilityBase::initialize handles the rest (parses initial
// position, etc.).
void DroneMobility::initialize(int stage)
{
    MovingMobilityBase::initialize(stage);
    if (stage == inet::INITSTAGE_LOCAL) {
        // Start idle. setTarget() will switch us on when the app issues a move.
        stationary = true;
        moving = false;
        lastVelocity = inet::Coord::ZERO;
    }
}

// MovingMobilityBase callback: advance position based on velocity. Called
// at each scheduled update tick. We snap to the target (and stop the
// periodic ticker) once we reach -- or just past -- the planned arrival
// time, otherwise integrate linearly.
void DroneMobility::move()
{
    if (!moving) {
        lastVelocity = inet::Coord::ZERO;
        return;
    }

    simtime_t now = simTime();
    double dt = (now - lastUpdate).dbl();

    // Have we arrived (or overshot, due to floating point)?
    if (now >= nextChange) {
        lastPosition = targetPos;
        lastVelocity = inet::Coord::ZERO;
        moving = false;
        stationary = true;       // stop the periodic self-timer
        nextChange = -1;
        return;
    }

    // Continue linear interpolation toward target.
    lastPosition += lastVelocity * dt;
}

// Re-aim the module at a new (target, speed). Settles the current position
// up to "now" first so a mid-flight redirect doesn't lose the partial
// progress already made on the old leg.
//   * dist < 1e-6 or speed <= 0  => snap to target and stay put
//   * otherwise                  => set velocity = direction*speed and
//                                   schedule arrival at simTime()+dist/speed
void DroneMobility::setTarget(const inet::Coord& target, double speed)
{
    Enter_Method("setTarget");
    // Bring lastPosition up to "now" before we change direction.
    moveAndUpdate();

    targetPos = target;
    moveSpeed = speed;

    inet::Coord delta = targetPos - lastPosition;
    double dist = delta.length();

    if (dist < 1e-6 || speed <= 0.0) {
        // Already there (or no speed). Snap and stay put.
        lastPosition = targetPos;
        lastVelocity = inet::Coord::ZERO;
        moving = false;
        stationary = true;
        nextChange = -1;
        emitMobilityStateChangedSignal();
        scheduleUpdate();
        return;
    }

    inet::Coord direction = delta / dist;
    lastVelocity = direction * speed;
    nextChange = simTime() + dist / speed;

    moving = true;
    stationary = false;   // re-enable periodic updates

    emitMobilityStateChangedSignal();
    scheduleUpdate();
}

} // namespace uavswarmta
