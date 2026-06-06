/*
 * DroneMobility.cc
 */
#include "DroneMobility.h"

using namespace omnetpp;

namespace uavswarmta {

Define_Module(DroneMobility);

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
