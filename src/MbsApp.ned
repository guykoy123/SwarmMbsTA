package uavswarmta;
@namespace(uavswarmta); // This tells NED to look inside your C++ namespace
import inet.applications.contract.IApp;

simple MbsApp like IApp
{
    parameters:
        double speed @unit(mps) = default(15mps); // MBSs are typically slower than the drones

        // Antenna tower tinted blue
        @display("i=custom/van-an-antenna,blue");
    gates:
        input socketIn;
        output socketOut;
}
