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
        -1, -1, 4, -1, -1, 3, -1,
        -1, -1, 7, -1, -1, 5, -1,
        -1, -1, 8, -1, -1, 6, -1,
        -1, -1, 12, -1, -1, 10, -1,
        -1, -1, 13, -1, -1, 11, -1,
        -1, -1, 15, -1, -1, 13, -1,
        -1, -1, 16, -1, -1, 14, -1,
        -1, 0, 2, -1, -1, 4, -1,
        -1, 1, 2, -1, -1, 9, -1,
        -1, 0, 3, -1, -1, 5, -1,
        -1, 0, 4, -1, -1, 7, -1,
        -1, 0, 5, -1, -1, 8, -1,
        -1, 1, 5, -1, -1, 13, -1,
        -1, 0, 6, -1, -1, 9, -1,
        -1, 1, 6, -1, -1, 14, -1,
        -1, 1, 7, -1, -1, 15, -1,
        -1, 1, 8, -1, -1, 16, -1,
        -1, 0, 9, -1, -1, 12, -1,
        -1, 1, 9, -1, -1, 17, -1,
        -1, 0, 10, -1, -1, 13, -1,
        -1, 0, 11, -1, -1, 14, -1,
        -1, 0, 12, -1, -1, 15, -1,
        -1, 0, 13, -1, -1, 16, -1,
        -1, 0, 14, -1, -1, 17, -1,
        -1, 1, 15, -1, -1, 18, -1,
        -1, 1, 17, -1, -1, 19, -1,
        -1, 2, 2, -1, 1, 17, -1,
        -1, 1, 4, -1, 0, 9, -1,
        -1, 0, 6, -1, 1, 2, -1,
        -1, 1, 7, -1, 0, 12, -1,
        -1, 2, 9, -1, 1, 19, -1,
        -1, 0, 10, -1, 1, 5, -1,
        -1, 0, 11, -1, 1, 6, -1,
        -1, 0, 13, -1, 1, 8, -1,
        -1, 0, 14, -1, 1, 9, -1,
        -1, 0, 17, -1, 1, 12, -1,
        1, 1, 1, -1, -1, 2, -1
    };
}
#endif

void actual_network_init()
{
    using namespace Species;
    using namespace network;

    // binding energies per nucleon in MeV
    amrex::Array1D<amrex::Real, 1, NumSpec> ebind_per_nucleon;

    ebind_per_nucleon(H1) = 0.0_rt;
    ebind_per_nucleon(He4) = 7.073915_rt;
    ebind_per_nucleon(C12) = 7.680144_rt;
    ebind_per_nucleon(C13) = 7.469849_rt;
    ebind_per_nucleon(N13) = 7.238863_rt;
    ebind_per_nucleon(N14) = 7.475613999999999_rt;
    ebind_per_nucleon(N15) = 7.69946_rt;
    ebind_per_nucleon(O14) = 7.052278_rt;
    ebind_per_nucleon(O15) = 7.463692_rt;
    ebind_per_nucleon(O16) = 7.976206_rt;
    ebind_per_nucleon(O17) = 7.7507280000000005_rt;
    ebind_per_nucleon(O18) = 7.767097_rt;
    ebind_per_nucleon(F17) = 7.542328_rt;
    ebind_per_nucleon(F18) = 7.631638_rt;
    ebind_per_nucleon(F19) = 7.779018_rt;
    ebind_per_nucleon(Ne18) = 7.341257_rt;
    ebind_per_nucleon(Ne19) = 7.567343_rt;
    ebind_per_nucleon(Ne20) = 8.03224_rt;
    ebind_per_nucleon(Mg22) = 7.662761000000001_rt;
    ebind_per_nucleon(Mg24) = 8.260709_rt;
    ebind_per_nucleon(Fe56) = 8.790353999999999_rt;

    // convert to binding energies per nucleus in MeV
    for (int i = 1; i <= NumSpec; ++i) {
        bion(i) = ebind_per_nucleon(i) * aion[i-1];
    }

    // Set the mass -- this will be in grams
    for (int i = 1; i <= NumSpec; ++i) {
        mion(i) = (aion[i-1] - zion[i-1]) * C::Legacy::m_n + zion[i-1] * (C::Legacy::m_p + C::Legacy::m_e) - bion(i) * C::Legacy::MeV2gr;
    }

}
