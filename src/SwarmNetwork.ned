
package uavswarmta;


import inet.visualizer.contract.IIntegratedVisualizer;
import inet.physicallayer.wireless.common.contract.packetlevel.IRadioMedium;
import inet.networklayer.configurator.ipv4.Ipv4NetworkConfigurator;
import inet.node.inet.AdhocHost;
import inet.node.inet.StandardHost;

network SwarmNetwork
{
    parameters:
        // Define the map size for visualization
        @display("bgb=2000,2000");

        // We will define these numbers later in the ini file
        int numMbs;
        int numDrones;

    submodules:
        // The visualizer draws the map, drones, and communication circles
       visualizer: <default("IntegratedVisualizer")> like IIntegratedVisualizer if true;

        // The Ideal Radio Medium for abstract, error-free communication based strictly on distance
        radioMedium: <default("UnitDiskRadioMedium")> like IRadioMedium {
            @display("p=100,150");
        }

        // Automatically assigns IP addresses to all nodes so they can route messages,
        // using ipv4 since we are not testing the actuall routing protocol
        // we force it into multiple LANs between drones and their respective MBS,
        // and a bigger LAN between MBSs and the main node
        configurator: Ipv4NetworkConfigurator {
            @display("p=100,250");
        }

        // -----------------------------------------------------------
        // SWARM ENTITIES
        // -----------------------------------------------------------

        // The Main Node (Stationary, generates tasks)
        mainNode: StandardHost {
            @display("p=1000,1000;i=device/server");
        }

        // The Mobile Base Stations (Array of nodes)
        mbs[numMbs]: AdhocHost {
            @display("i=device/antennatower");
        }

        // The Drones (Array of nodes that will perform the tasks)
        drone[numDrones]: AdhocHost {
            @display("i=misc/drone");
        }
}