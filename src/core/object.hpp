#pragma once
#include "thoth.hpp"

//! constants
inline constexpr char KIND_BARRIER[] = "barrier";
inline constexpr char KIND_VARIANCE_SWAP[] = "variance_swap";
inline constexpr char KIND_BOOK[] = "book";
inline constexpr char KIND_PRICER[] = "pricer";
inline constexpr char KIND_BS_VOLATILITY[] = "bs_volatility";
inline constexpr char KIND_CONTINUOUS_DIVIDENDS_CURVE[] = "continuous_dividends_curve";
inline constexpr char KIND_CORRELATION_MATRIX[] = "correlation_matrix";
inline constexpr char KIND_DEBUG_CONFIGURATION[] = "debug_configuration";
inline constexpr char KIND_CURRENCY[] = "currency";
inline constexpr char KIND_EQUITY[] = "equity";
inline constexpr char KIND_COMPOSITE[] = "composite";
inline constexpr char KIND_BASKET[] = "basket";
inline constexpr char KIND_FOREX[] = "forex";
inline constexpr char KIND_HISTORICAL_CORRELATION_COMPUTATION[] = "historical_correlation_computation";
inline constexpr char KIND_HISTORICAL_VOLATILITY_COMPUTATION[] = "historical_volatility_computation";
inline constexpr char KIND_MCL_CONFIGURATION[] = "mcl_configuration";
inline constexpr char KIND_SABR_VOLATILITY[] = "sabr_volatility";
inline constexpr char KIND_PDE_CONFIGURATION[] = "pde_configuration";
inline constexpr char KIND_PRICER_CONFIGURATION[] = "pricer_configuration";
inline constexpr char KIND_REPO_CURVE[] = "repo_curve";
inline constexpr char KIND_SEQUENCE[] = "sequence";
inline constexpr char KIND_SIMPLE_FIXING_DATA[] = "simple_fixing_data";
inline constexpr char KIND_VANILLA[] = "vanilla";
inline constexpr char KIND_YIELD_CURVE[] = "yield_curve";

//!
class Object
{

  protected:
    //! attributes
    string _name;
    string _kind;
    date _today;

  public:
    //! getter
    const string& GetName() const;
    const string& GetKind() const;

    //! setter
    virtual void SetToday( const date& Today );

    //! constructor, destructor
    Object( const string& ObjectName,
            const string& ObjectKind );
    virtual ~Object();
};
