#include <actual_network.H>


namespace network
{
    AMREX_GPU_MANAGED amrex::Array1D<amrex::Real, 1, NumSpec> bion;
    AMREX_GPU_MANAGED amrex::Array1D<amrex::Real, 1, NumSpec> mion;
}

#ifdef NSE_NET
namespace NSE_INDEX
{
    AMREX_GPU_MANAGED amrex::Array2D<int, 1, Rates::NumRates, 1, 7, Order::C> rate_indices {
        -1, 3, 3, -1, 2, 5, -1,
        -1, 3, 3, -1, 0, 7, -1,
        -1, 3, 3, -1, 1, 6, -1,
        -1, 2, 3, -1, -1, 4, -1,
        -1, -1, 0, -1, -1, 1, -1
    };
}
#endif

void actual_network_init()
{
    using namespace Species;
    using namespace network;

    // binding energies per nucleon in MeV
    amrex::Array1D<amrex::Real, 1, NumSpec> ebind_per_nucleon;

    ebind_per_nucleon(N) = 0.0_rt;
    ebind_per_nucleon(H1) = 0.0_rt;
    ebind_per_nucleon(He4) = 7.073915_rt;
    ebind_per_nucleon(C12) = 7.680144_rt;
    ebind_per_nucleon(O16) = 7.976206_rt;
    ebind_per_nucleon(Ne20) = 8.03224_rt;
    ebind_per_nucleon(Na23) = 8.111493000000001_rt;
    ebind_per_nucleon(Mg23) = 7.901115_rt;

    // convert to binding energies per nucleus in MeV
    for (int i = 1; i <= NumSpec; ++i) {
        bion(i) = ebind_per_nucleon(i) * aion[i-1];
    }

    // Set the mass -- this will be in grams
    for (int i = 1; i <= NumSpec; ++i) {
        mion(i) = (aion[i-1] - zion[i-1]) * C::Legacy::m_n + zion[i-1] * (C::Legacy::m_p + C::Legacy::m_e) - bion(i) * C::Legacy::MeV2gr;
    }

}
