#pragma once

//! Internal scenario tags for the single-tree Greek bumps. The delta/gamma tags are
//! prefixes completed by the bumped single's name ("@D+:<single>"); vega and rho are
//! whole tags. Centralised here so the producer (BuildGreekScenarios), the consumers
//! (ComputeGreeks and the American reprice) and GraphTreeKey share one spelling — a
//! stray literal would otherwise silently yield a wrong/zero Greek or an .at() throw.
namespace scenario_tag
{
inline constexpr char DELTA_PREFIX[] = "@D+:";
inline constexpr char GAMMA_UP_PREFIX[] = "@G+:";
inline constexpr char GAMMA_DOWN_PREFIX[] = "@G-:";
inline constexpr char VEGA[] = "@V+";
inline constexpr char RHO[] = "@R+";

//! per-single delta / gamma tag = prefix + the bumped single's name
inline string delta( const string& single ) { return DELTA_PREFIX + single; }
inline string gamma_up( const string& single ) { return GAMMA_UP_PREFIX + single; }
inline string gamma_down( const string& single ) { return GAMMA_DOWN_PREFIX + single; }
} // namespace scenario_tag
