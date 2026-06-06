package uavswarmta;

@namespace(uavswarmta);

//
// Mobile Base Station app. Coordinates with the main node entirely through
// direct C++ calls; no network gates.
//
simple MbsApp like IMobileApp
{
    parameters:
        double speed @unit(mps) = default(15mps);
        double mbs_comm_range @unit(m) = default(800m);

        @display("i=device/antennatower,blue");
}
