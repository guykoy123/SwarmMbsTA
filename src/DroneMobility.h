/*
 * DroneMobility.h
 *
 * Simple "go to target then stop" mobility for the UAV swarm task-allocation
 * study. Subclasses INET's MovingMobilityBase so the standard tooling
 * (visualizer, IMobility queries from the apps) keeps working.
 */
#ifndef __UAVSWARMTA_DRONEMOBILITY_H_
#define __UAVSWARMTA_DRONEMOBILITY_H_

#include "inet/mobility/base/MovingMobilityBase.h"

namespace uavswarmta {

class DroneMobility : public inet::MovingMobilityBase
{
  protected:
    inet::Coord targetPos;
    double moveSpeed = 0;
    bool moving = false;

    virtual void initialize(int stage) override;
    virtual void move() override;

  public:
    /** Command the module to fly in a straight line toward @p target at @p speed
     *  m/s, then stop on arrival. Calling this again replaces the current goal. */
    void setTarget(const inet::Coord& target, double speed);

    /** True while the module is still en route to its current target. */
    bool isMoving() const { return moving; }
};

} // namespace uavswarmta

#endif
