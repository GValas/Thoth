#pragma once

//! Monte-Carlo node-name suffixes.
//!
//! Every node in the graph is keyed by a string name, almost always
//! "<object-name><suffix>" (e.g. "eq#spot"). The suffix is the *role* of the node
//! for that object. These were scattered string literals, so a builder and a
//! consumer had to spell the same "#white_noise" by hand — a typo produced a
//! silent null child at lookup, not a compile error. Centralising them here makes
//! the construction site and the lookup site share one symbol, and a typo a
//! compile error. The collector still keys by the resulting string; this only
//! formalises how that string is spelled.
namespace node_name
{
inline constexpr char SPOT[] = "#spot";                       //!< the diffused spot
inline constexpr char BROWNIAN[] = "#brownian";               //!< correlated Brownian increment
inline constexpr char WHITE_NOISE[] = "#white_noise";         //!< independent Gaussian (spot)
inline constexpr char VOL_WHITE_NOISE[] = "#vol_white_noise"; //!< independent Gaussian (variance)
inline constexpr char JUMP_NOISE[] = "#jump_noise";           //!< Bates compound-Poisson jump
inline constexpr char NOISE[] = "#noise";                     //!< generic noise source
inline constexpr char VARIANCE[] = "#variance";               //!< Heston variance process
inline constexpr char DRIFT[] = "#drift";                     //!< risk-neutral drift
inline constexpr char DIVIDEND[] = "#dividend";               //!< discrete-dividend future-PV (escrow)
inline constexpr char VOL[] = "#vol";                         //!< scalar (constant) vol
inline constexpr char ATM_VOL[] = "#atmvol";                  //!< representative ATM vol (local-vol surfaces)
inline constexpr char LOCAL_VOL[] = "#localvol";              //!< Dupire local-vol grid
inline constexpr char LEVERAGE[] = "#leverage";               //!< LSV leverage grid L(S,t)
inline constexpr char CORREL[] = "#correl";                   //!< correlation node
inline constexpr char FLOW[] = "#flow";                       //!< contract payoff / flow
inline constexpr char RATE[] = "#rate";                       //!< projection / funding zero-rate node
inline constexpr char DISC_RATE[] = "#discrate";              //!< OIS / discounting curve node (multi-curve)
inline constexpr char IR_FACTOR[] = "#irfactor";              //!< Hull-White OU factor x(t)
inline constexpr char IR_EXPONENT[] = "#irexponent";          //!< Hull-White exponent int x + V/2
inline constexpr char CARRY[] = "#carry";                     //!< deterministic-carry spot under a hybrid wrapper
inline constexpr char FX[] = "#fx";                           //!< FX rate node
inline constexpr char CHOLESKY[] = "#cholesky";               //!< Cholesky-correlated factor
inline constexpr char QUANTO_PREFIX[] = "#quanto_";           //!< prefix: "#quanto_<ccy>" (quanto-adjusted spot)

//! separator between two OBJECT names inside one composite key (e.g. the pair
//! correlation "eur_usd#eq#correl") — distinct from the role suffixes above,
//! which always start the tail of the name.
inline constexpr char SEP[] = "#";
} // namespace node_name
