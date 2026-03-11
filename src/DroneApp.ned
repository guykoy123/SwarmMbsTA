package uavswarmta;

@namespace(uavswarmta); // This tells NED to look inside your C++ namespace
import inet.applications.contract.IApp;

simple DroneApp like IApp
{
    parameters:
        int localPort = default(5000);
        int destPort = default(5000);

        // Energy Parameters (Watts & Joules)
        double initialEnergy @unit(J) = default(100000J);
        double idlePower @unit(W) = default(5W);
        double travelPower @unit(W) = default(100W);
        double workingPower @unit(W) = default(50W);

        double speed @unit(mps) = default(25mps);

        double energyThreshold @unit(J) = default(10000J); // 10% of 100kJ
        double rechargeDuration @unit(s) = default(5s);

        double commTimeoutDuration @unit(s) = default(10s);

        double drone_comm_range @unit(m) = default(250m);

        @display("i=misc/drone,cyan");
    gates:
        input socketIn;
        output socketOut;
}
