#pragma once

//! ----------------------------------------------------------------------
//! Probability distribution functions (normal & gamma) used by the closed-form
//! analytics and the Monte-Carlo path engine.
//!
//! This is a thin facade that isolates the underlying implementation from the
//! call sites — part of the migration off GSL. The normal CDF/pdf/quantile are
//! std / closed-form / a rational approximation (faster than the GSL routines
//! they replace and free of any third-party dependency); the gamma CDF is the
//! one piece that delegates to Boost.Math, which is acceptable as it is only
//! ever called O(1) per pricing (never on the per-path hot loop).
//! ----------------------------------------------------------------------

//! standard normal CDF  Phi(x) = P(Z <= x)  — exact (via std::erfc)
double NormalCdf( double X );

//! standard normal pdf  phi(x) = exp(-x^2/2) / sqrt(2*pi)
double NormalPdf( double X );

//! inverse standard normal CDF  Phi^-1(p),  p in (0,1)  — Acklam's rational
//! approximation (max abs error ~1.2e-9; this is the MC Sobol->Gaussian hot path)
double NormalCdfInv( double P );

//! gamma CDF  P(x; Shape, Scale)  (shape k, scale theta) — delegates to Boost.Math
double GammaCdf( double X, double Shape, double Scale );
