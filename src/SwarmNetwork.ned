package uavswarmta;

import inet.visualizer.contract.IIntegratedVisualizer;

//
// Top-level network. No radio medium, no IP configurator: communication is
// modelled as ideal-within-range direct C++ calls between the apps.
//
network SwarmNetwork
{
    parameters:
        @display("bgb=2000,2000");

        int numMbs;
        int numDrones;

    submodules:
        visualizer: <default("IntegratedVisualizer")> like IIntegratedVisualizer if typename != "" {
            @display("p=100,50");
        }

        mainNode: MobileHost {
            @display("p=1000,1000;i=device/server;is=l");
        }

        mbs[numMbs]: MobileHost {
            @display("i=device/antennatower;is=s");
        }

        drone[numDrones]: MobileHost {
            @display("i=misc/drone;is=vs");
        }
}
