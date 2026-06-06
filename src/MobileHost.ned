package uavswarmta;

@namespace(uavswarmta);

import inet.mobility.contract.IMobility;

//
// Empty module interface implemented by every app that can live inside a
// MobileHost. We do not need socket gates -- the apps talk to each other via
// direct C++ method calls (the swarm is studied with ideal in-range comms).
//
moduleinterface IMobileApp
{
    parameters:
        @display("i=block/app");
}

//
// Minimal node used by mainNode, MBSs and drones. It carries a mobility model
// plus an array of application modules, and nothing else: no radio stack,
// no IP, no UDP. The simulation models communication purely as direct calls
// from the app code, guarded by a distance check against IMobility.
//
module MobileHost
{
    parameters:
        int numApps = default(1);
        @display("bgb=,,,,;i=device/server");

    submodules:
        mobility: <default("uavswarmta.DroneMobility")> like IMobility {
            parameters:
                @display("p=50,50");
        }
        app[numApps]: <> like IMobileApp {
            parameters:
                @display("p=150,50,row,80");
        }
}
