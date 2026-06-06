package uavswarmta;

@namespace(uavswarmta);

import inet.mobility.base.MovingMobilityBase;

//
// Per-node mobility model used by the UAV swarm study. Drones (and MBSs) stay
// stationary until the assignment logic calls C++ setTarget(Coord, speed);
// then they fly in a straight line to the target and stop on arrival.
//
simple DroneMobility extends MovingMobilityBase
{
    parameters:
        @class(DroneMobility);
}
