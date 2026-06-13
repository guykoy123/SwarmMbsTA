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

        // mainNode is the central allocator -- pure compute, nothing to look
        // at on the canvas. We push it far off the visible 2000x2000 area
        // (and MainNodeApp::initialize also clears its icon at runtime, see
        // there) so the canvas only shows the drones + MBSs. The module
        // itself runs normally; only its visual representation is suppressed.
        mainNode: MobileHost {
            @display("p=-10000,-10000;b=1,1;i=");
        }

        mbs[numMbs]: MobileHost {
            @display("i=device/antennatower;is=s");
        }

        drone[numDrones]: MobileHost {
            @display("i=misc/drone;is=vs");
        }
}
