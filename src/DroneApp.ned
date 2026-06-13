package uavswarmta;

@namespace(uavswarmta);

//
// Drone application. Talks to the main node (and MBSs) via direct C++ calls
// rather than UDP -- the simulation models comms as ideal-when-in-range.
//
simple DroneApp like IMobileApp
{
    parameters:
        // Energy (Watts & Joules)
        double initialEnergy @unit(J) = default(100000J);
        double idlePower @unit(W) = default(5W);
        double travelPower @unit(W) = default(100W);
        double workingPower @unit(W) = default(50W);

        double speed @unit(mps) @mutable = default(25mps);

        double energyThreshold @unit(J) = default(10000J);
        double rechargeDuration @unit(s) = default(5s);

        double commTimeoutDuration @unit(s) @mutable = default(10s);
        double drone_comm_range @unit(m) @mutable = default(250m);

        @display("i=misc/drone,cyan");
}
