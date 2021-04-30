&extern

  dens_min   = 1.d5
  dens_max   = 5.d8
  temp_min   = 5.d7
  temp_max   = 5.d9

  C_nse = 0.2
  T_nse = 5.e8
  rho_nse = 3.e7

  ! NSE doesn't really care about the timestep
  tmax = 1.d-9

  primary_species_1 = "helium-4"
  primary_species_2 = "iron-54"

  jacobian = 1

/
