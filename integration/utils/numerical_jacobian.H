#ifndef NUMERICAL_JACOBIAN_H
#define NUMERICAL_JACOBIAN_H

#include <eos_composition.H>
#include <network.H>
#include <burn_type.H>
#include <extern_parameters.H>
#ifdef NEW_NETWORK_IMPLEMENTATION
#include <rhs.H>
#else
#include <actual_rhs.H>
#endif
#include <integrator_data.H>


///
/// Compute the numerical Jacobian of the reactive system, dydot/dy,
/// where ydot = {dX/dt, denuc/dt}, and y = {X, e}.
///
/// Note the analytic Jacobian usually works in terms of molar
/// fractions, Y, instead of mass fractions, X, but our integrators
/// always work in terms of X, so the numerical Jacobian is expressed
/// in terms of X.
///
/// Note: even for the various flavors of SDC, this is the form we
/// want (we call this dR/dw)
///
/// Even though we have e as an independent variable, we will
/// difference in terms of X and T and then convert the Jacobian
/// elements to be in terms of X and e

struct jac_info_t {
    amrex::Real h;
};

const amrex::Real U = std::numeric_limits<amrex::Real>::epsilon();

template <typename BurnT>
AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
void numerical_jac(BurnT& state, const jac_info_t& jac_info, JacNetArray2D& jac)
{

    // we already come in with a cleaned state, and density updated to
    // the current time

    constexpr int int_neqs = integrator_neqs<BurnT>();

    //for (int i = 1; i <= int_neqs; i++) {
    //    for (int j = 1; j <= int_neqs; j++) {
    //        jac(i,j) = 0.0_rt;
    //    }
    //}

    YdotNetArray1D ydotp;
    YdotNetArray1D ydotm;

    // note: we assume that coming into the numerical jacobian that
    // state is thermodynamically consistent (T matches e)

    BurnT state_delp = state;

    // default -- plus convert the dY/dt into dX/dt

    actual_rhs(state, ydotm);

    for (int q = 1; q <= NumSpec; q++) {
        ydotm(q) *= aion[q-1];
    }

    // start by computing |f|, which is the norm of the RHS / weight

    amrex::Real fac = 0.0_rt;
    amrex::Real w;
    for (int i = 1; i <= NumSpec; i++) {
        // the weight is 1 / EWT defined in VODE
        w = 1.0_rt / (integrator_rp::rtol_spec * std::abs(state.xn[i-1]) + integrator_rp::atol_spec);
        fac += (ydotm(i) * w) * (ydotm(i) * w);
    }

    // even though we difference wrt T below, for |f|, we want to use
    // energy to get the units correct

    w = 1.0_rt / (integrator_rp::rtol_enuc * std::abs(state.e) + integrator_rp::atol_enuc);
    fac += (ydotm(net_ienuc) * w) * (ydotm(net_ienuc) * w);
    fac = std::sqrt(fac / int_neqs);

    // now compute r0, defined in the LSODE paper, Eq. 3.34

    amrex::Real r0 = 1000.0_rt * std::abs(jac_info.h) * U * int_neqs * fac;
    if (r0 == 0.0_rt) {
        r0 = 1.0_rt;
    }

    // now we compute the Jacobian elements

    // species derivatives -- we will difference here at constant T,
    // rho, and below we will convert these to be at constant e, rho

    for (int n = 1; n <= NumSpec; n++) {
        // perturb species -- we send in X, but ydot is in terms
        // of dY/dt, not dX/dt

        amrex::Real yj = state_delp.xn[n-1];

        w = integrator_rp::rtol_spec * std::abs(yj) + integrator_rp::atol_spec;

        // the increment we use in the derivative is defined in the LSODE paper, Eq. 3.35

        amrex::Real dy = amrex::max(std::sqrt(U) * std::abs(yj), r0 * w);

        state_delp.xn[n-1] += dy;

        actual_rhs(state_delp, ydotp);

        // We integrate X, so convert from the Y we got back from the RHS

        for (int q = 1; q <= NumSpec; q++) {
            ydotp(q) *= aion[q-1];
        }

        // now fill in all of the rows for this column X_n

        for (int m = 1; m <= int_neqs; m++) {
            jac(m, n) = (ydotp(m) - ydotm(m)) / dy;
        }

        state_delp.xn[n-1] = yj;
    }

    // T derivative

    w = integrator_rp::rtol_enuc * std::abs(state.T) + integrator_rp::atol_enuc;

    amrex::Real dy = amrex::max(std::sqrt(U) * std::abs(state.T), r0 * w);

    state_delp.T += dy;

    if (state_delp.T <= EOSData::mintemp || state_delp.T >= integrator_rp::MAX_TEMP) {
        jac.zero();
        return;
    }


#ifdef AUX_THERMO
    // make the aux data consistent with the state X's
    set_aux_comp_from_X(state_delp);
#endif

    eos(eos_input_rt, state_delp);

    actual_rhs(state_delp, ydotp);

    for (int q = 1; q <= NumSpec; q++) {
        ydotp(q) *= aion[q-1];
    }

    // first fill just the last column with dy/dT

    for (int m = 1; m <= int_neqs; m++) {
        jac(m, net_ienuc) = (ydotp(m) - ydotm(m)) / dy;
    }

    // back to the original state, get the thermodynamics -- in particular, we need c_v and e_X

    eos_re_extra_t eos_state;
    eos_state.rho = state.rho;
    eos_state.T = state.T;
    eos_state.e = state.e;
    for (int n = 0; n < NumSpec; n++) {
        eos_state.xn[n] = state.xn[n];
    }
#ifdef AUX_THERMO
    // make the aux data consistent with the state X's
    set_aux_comp_from_X(eos_state);
#endif

    eos(eos_input_re, eos_state);

    eos_xderivs_t eos_xderivs = composition_derivatives(eos_state);

    // now correct the energy column, since we differenced with
    // respect to T above

    for (int m = 1; m <= int_neqs; m++) {
        jac(m, net_ienuc) *= 1.0_rt / eos_state.cv;
    }

    // now correct the species derivatives
    // this constructs dy/dX_k |_e = dy/dX_k |_T - e_{X_k} |_T dy/dT / c_v

    for (int n = 1; n <= NumSpec; n++) {
        for (int m = 1; m <= int_neqs; m++) {
            jac(m, n) -= eos_xderivs.dedX[n-1] * jac(m, net_ienuc);
        }
    }

    // scale the energy derivatives
    if (integrator_rp::scale_system) {
        // first the de/dX row
        for (int n = 1; n <= INT_NEQS; ++n) {
            jac(net_ienuc, n) /= state.e_scale;
        }

        // now the dX/de column
        for (int m = 1; m <= INT_NEQS; ++m) {
            jac(m, net_ienuc) *= state.e_scale;
        }
    }

    // apply boosting factor:

    if (integrator_rp::react_boost > 0.0_rt) {
        jac.mul(integrator_rp::react_boost);
    }

    // Allow temperature and energy integration to be disabled.

    if (! integrator_rp::integrate_energy) {
        for (int n = 1; n <= int_neqs; ++n) {
            jac(net_ienuc, n) = 0.0_rt;
        }
    }



}
#endif
