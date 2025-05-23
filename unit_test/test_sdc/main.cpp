#include <AMReX_PlotFileUtil.H>
#include <AMReX_ParmParse.H>
#include <AMReX_Print.H>

#include <AMReX_Geometry.H>
#include <AMReX_MultiFab.H>
#include <AMReX_iMultiFab.H>
#include <AMReX_BCRec.H>


using namespace amrex;

#include <test_react.H>
#include <AMReX_buildInfo.H>

#include <network.H>
#include <eos.H>
#include <variables.H>
#include <react_util.H>

#include <cmath>
#include <unit_test.H>

#include <react_zones.H>
#include <integrator.H>

int main (int argc, char* argv[])
{
    amrex::Initialize(argc, argv);

    main_main();

    amrex::Finalize();
    return 0;
}

void main_main ()
{

    // AMREX_SPACEDIM: number of dimensions
    int n_cell, max_grid_size;

    std::string prefix = "plt";

    IntVect tile_size(1024, 8, 8);

    // inputs parameters
    {
        // ParmParse is way of reading inputs from the inputs file
        ParmParse pp;

        // We need to get n_cell from the inputs file - this is the
        // number of cells on each side of a square (or cubic) domain.
        pp.get("n_cell", n_cell);

        // The domain is broken into boxes of size max_grid_size
        max_grid_size = 32;
        pp.query("max_grid_size", max_grid_size);

        pp.query("prefix", prefix);

    }

    Vector<int> is_periodic(AMREX_SPACEDIM,0);
    for (int idim=0; idim < AMREX_SPACEDIM; ++idim) {
      is_periodic[idim] = 1;
    }

    // make BoxArray and Geometry
    BoxArray ba;
    Geometry geom;
    {
        IntVect dom_lo(AMREX_D_DECL(       0,        0,        0));
        IntVect dom_hi(AMREX_D_DECL(n_cell-1, n_cell-1, n_cell-1));
        Box domain(dom_lo, dom_hi);

        // Initialize the boxarray "ba" from the single box "bx"
        ba.define(domain);

        // Break up boxarray "ba" into chunks no larger than
        // "max_grid_size" along a direction
        ba.maxSize(max_grid_size);

        // This defines the physical box, [0, 1] in each direction.
        RealBox real_box({AMREX_D_DECL(0.0, 0.0, 0.0)},
                         {AMREX_D_DECL(1.0, 1.0, 1.0)});

        // This defines a Geometry object
        geom.define(domain, &real_box,
                    CoordSys::cartesian, is_periodic.data());
    }

    // Nghost = number of ghost cells for each array
    int Nghost = 0;

    // do the runtime parameter initializations and microphysics inits
    if (ParallelDescriptor::IOProcessor()) {
      std::cout << "reading extern runtime parameters ..." << std::endl;
    }

    ParmParse ppa("amr");

    init_unit_test();

    // C++ EOS initialization (must be done after init_extern_parameters)
    eos_init(unit_test_rp::small_temp, unit_test_rp::small_dens);

    // C++ Network, RHS, screening, rates initialization
    network_init();

    plot_t vars;
    vars = init_variables();

    amrex::Vector<std::string> names;
    get_varnames(vars, names);

    // time = starting time in the simulation
    Real time = 0.0;

    // How Boxes are distributed among MPI processes
    DistributionMapping dm(ba);

    // we allocate our main multifabs
    MultiFab state(ba, dm, vars.n_plot_comps, Nghost);

    // Initialize the state

    Real dlogrho;
    Real dlogT;

    if (n_cell > 1) {
        dlogrho = (std::log10(unit_test_rp::dens_max) - std::log10(unit_test_rp::dens_min)) / static_cast<amrex::Real>(n_cell - 1);
        dlogT   = (std::log10(unit_test_rp::temp_max) - std::log10(unit_test_rp::temp_min)) / static_cast<amrex::Real>(n_cell - 1);
    } else {
        dlogrho = 0.0_rt;
        dlogT   = 0.0_rt;
    }

    init_t comp_data = setup_composition(n_cell);

    for ( MFIter mfi(state); mfi.isValid(); ++mfi )
    {
        const Box& bx = mfi.validbox();

        auto state_arr = state.array(mfi);

        amrex::ParallelFor(bx,
        [=] AMREX_GPU_HOST_DEVICE (int i, int j, int k)
        {

            state_arr(i, j, k, vars.itemp) =
                std::pow(10.0_rt, (std::log10(unit_test_rp::temp_min) + static_cast<Real>(j)*dlogT));
            state_arr(i, j, k, vars.irho) =
                std::pow(10.0_rt, (std::log10(unit_test_rp::dens_min) + static_cast<Real>(i)*dlogrho));

            Real xn[NumSpec];
            get_xn(k, comp_data, xn, unit_test_rp::uniform_xn);

            for (int n = 0; n < NumSpec; n++) {
                state_arr(i, j, k, vars.ispec_old+n) =
                    amrex::max(xn[n], 1.e-10_rt);
            }

            // initialize the auxiliary state (in particular, for NSE)
#ifdef AUX_THERMO
            eos_extra_t eos_state;
            for (int n = 0; n < NumSpec; n++) {
                eos_state.xn[n] = xn[n];
            }
            set_aux_comp_from_X(eos_state);
            for (int n = 0; n < NumAux; n++) {
                state_arr(i, j, k, vars.iaux_old+n) = eos_state.aux[n];
            }
#endif
        });
    }

    // allocate a multifab for the number of RHS calls
    // so we can manually do the reductions (for GPU)
    iMultiFab integrator_n_rhs(ba, dm, 1, Nghost);

    // What time is it now?  We'll use this to compute total react time.
    Real strt_time = ParallelDescriptor::second();

    int num_failed = 0;
    AsyncArray<int> aa_num_failed(&num_failed, 1);
    int* num_failed_d = aa_num_failed.data();

    // Do the reactions
#ifdef _OPENMP
#pragma omp parallel
#endif
    for ( MFIter mfi(state, tile_size); mfi.isValid(); ++mfi )
    {
        const Box& bx = mfi.tilebox();

        auto s = state.array(mfi);
        auto n_rhs = integrator_n_rhs.array(mfi);

        amrex::ParallelFor(bx,
        [=] AMREX_GPU_HOST_DEVICE (int i, int j, int k)
        {
            bool success = do_react(vars, i, j, k, s, n_rhs);

            if (!success) {
                Gpu::Atomic::Add(num_failed_d, 1);
            }
        });

    }

    aa_num_failed.copyToHost(&num_failed, 1);
    Gpu::synchronize();

    if (num_failed > 0) {
        amrex::Abort("Integration failed");
    }

    // Call the timer again and compute the maximum difference between
    // the start time and stop time over all processors
    Real stop_time = ParallelDescriptor::second() - strt_time;
    const int IOProc = ParallelDescriptor::IOProcessorNumber();
    ParallelDescriptor::ReduceRealMax(stop_time, IOProc);

    int n_rhs_min = integrator_n_rhs.min(0);
    int n_rhs_max = integrator_n_rhs.max(0);
    long n_rhs_sum = integrator_n_rhs.sum(0, 0, true);

    // get the name of the integrator from the build info functions
    // written at compile time.  We will append the name of the
    // integrator to the output file name
    int nmodules = buildInfoGetNumModules();

    int int_idx = -1;
    std::string INT_KEY = "INTEGRATOR";
    for (int i=1; i<=nmodules; i++) {
      if (INT_KEY == buildInfoGetModuleName(i)) {
        int_idx = i;
      }
    }

    std::string name = "test_react.";
    std::string integrator = buildInfoGetModuleVal(int_idx);

    // Write a plotfile
    WriteSingleLevelPlotfile(prefix + name + integrator, state, names, geom, time, 0);

    write_job_info(prefix + name + integrator);

    // Tell the I/O Processor to write out the "run time"
    amrex::Print() << "Run time = " << stop_time << std::endl;

    // print statistics
    std::cout << "min number of rhs calls: " << n_rhs_min << std::endl;
    std::cout << "avg number of rhs calls: "
              << static_cast<Real>(n_rhs_sum) / static_cast<Real>(n_cell*n_cell*n_cell) << std::endl;
    std::cout << "max number of rhs calls: " << n_rhs_max << std::endl;

}
