#ifndef INITIAL_TIMESTEP_H
#define INITIAL_TIMESTEP_H

#include <AMReX_REAL.H>
#include <AMReX_Algorithm.H>

#include <actual_network.H>
#ifdef NEW_NETWORK_IMPLEMENTATION
#include <rhs.H>
#else
#include <actual_rhs.H>
#endif
#include <burn_type.H>

template <typename BurnT, typename IntT>
AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
amrex::Real initial_react_dt (BurnT& burn_state, IntT& int_state,
                              amrex::Array1D<amrex::Real, 1, INT_NEQS>& ydot)
{
    // this is a version of the initial timestep estimator from VODE
    // we come in with int_state..y() storing the initial solution

    // initial lower and upper bounds on the timestep

    amrex::Real hL = 100.0_rt * std::numeric_limits<amrex::Real>::epsilon() * int_state.tout;
    amrex::Real hU = 0.1_rt * int_state.tout;

    // initial guess for the itteraction

    amrex::Real h = std::sqrt(hL * hU);
    amrex::Real h_old = 10.0_rt * h;

    // Iterate on ddydtt = (RHS(t + h, y + h * dydt) - dydt) / h

    amrex::Array1D<amrex::Real, 1, INT_NEQS> ewt;
    amrex::Array1D<amrex::Real, 1, INT_NEQS> ydot_temp;
    amrex::Array1D<amrex::Real, 1, INT_NEQS> ddydtt;

    // save the old state and update the stored state
    amrex::Array1D<amrex::Real, 1, INT_NEQS> y_old;
    for (int n = 1; n <= INT_NEQS; ++n) {
        y_old(n) = int_state.y(n);
    }

    for (int n = 1; n <= 4; n++) {

       h_old = h;

       // Get the error weighting -- this is similar to VODE's dewset
       // routine

       for (int ii = 1; ii <= NumSpec; ii++) {
           ewt(ii) = int_state.rtol_spec * std::abs(y_old(ii)) + int_state.atol_spec;
       }
       ewt(net_ienuc) = int_state.rtol_enuc * std::abs(y_old(net_ienuc)) + int_state.atol_enuc;

       // Construct the trial point.

       for (int ii = 1; ii <= INT_NEQS; ii++) {
           int_state.y(ii) = y_old(ii) + h * ydot(ii);
       }

       // Call the RHS, then estimate the finite difference.

       rhs(int_state.t, burn_state, int_state, ydot_temp);

       for (int ii = 1; ii <= INT_NEQS; ii++) {
           ddydtt(ii) = (ydot_temp(ii) - ydot(ii)) / h;
       }

       amrex::Real yddnorm = 0.0_rt;
       for (int ii = 1; ii <= INT_NEQS; ii++) {
           yddnorm += amrex::Math::powi<2>(ddydtt(ii) * ewt(ii));
       }
       yddnorm = std::sqrt(yddnorm / INT_NEQS);

       if (yddnorm*hU*hU > 2.0_rt) {
          h = std::sqrt(2.0_rt / yddnorm);
       } else {
          h = std::sqrt(h * hU);
       }

       if (h_old < 2.0_rt * h && h_old > 0.5_rt * h) {
           break;
       }

    }

    // Save the final timestep, with a bias factor.

    amrex::Real dt = h / 2.0_rt;
    dt = amrex::Clamp(h, hL, hU);

    dt = amrex::min(dt, integrator_rp::ode_max_dt);

    // restore the old time solution
    for (int n = 1; n <= INT_NEQS; ++n) {
        int_state.y(n) = y_old(n);
    }

    return dt;
}

#endif
