#pragma once
#include "monte_carlo_node.hpp"

//! Monte-Carlo flow of a variance swap. At the maturity date index it reads the
//! whole simulated spot path (every diffusion date up to maturity), computes the
//! annualized realized variance RV = (1/T) * sum( ln(S_j / S_{j-1})^2 ), and
//! produces the cash flow notional * (RV - strike_variance). Zero on every other
//! date.
//!
//! Role in the MC node graph: a path-dependent payoff leaf. Unlike the vanilla flow
//! (which reads only S_T), this reads the *whole* spot path because realized
//! variance is the path's quadratic variation. DAG input: the spot node, sampled at
//! every date 0..maturity, but only when evaluated at the flow date.
class VarianceSwapFlowNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _spot_node = nullptr; //!< underlying spot path (read at every date)
    double _strike_variance = 0;          //!< K_var (decimal^2)
    double _notional = 1;                 //!< variance notional (cash per unit of variance)
    size_t _flow_date_index = 0;          //!< maturity date index at which the swap settles
    //! discrete observation schedule as sorted date indices (today's index 0 is the
    //! implicit first fixing). Empty -> continuous observation (every diffusion step).
    vector<size_t> _observation_date_index;
    //! seasoned swaps: realised past sum of squared log-returns (a constant, from
    //! the fixings) and the whole-window annualizer YearFraction(start, maturity);
    //! 0 keeps the spot-started behaviour (annualize by the grid's maturity time)
    double _past_variance = 0;
    double _total_year_fraction = 0;

  public:
    //! at the flow date, compute annualized realized variance and emit the swap cash
    //! flow; 0 on every other date.
    void ComputeValue( size_t DateIndex ) override;
    //! at the flow date, depend on the spot at every date 0..maturity (path-dependent).
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setter
    void SetSpotNode( MonteCarloNode* N );           //!< wire the underlying spot
    void SetStrikeVariance( double StrikeVariance ); //!< set K_var
    void SetNotional( double Notional );             //!< set the variance notional
    void SetFlowDateIndex( size_t DateIndex );       //!< set the maturity date index
    //! set the discrete fixing schedule (sorted date indices; empty = continuous)
    void SetObservationDateIndices( const vector<size_t>& DateIndices );
    void SetPastVariance( double PastSum2 );               //!< realised past sum of squared returns
    void SetTotalYearFraction( double TotalYearFraction ); //!< start -> maturity annualizer

    VarianceSwapFlowNode( const string& Name );
    ~VarianceSwapFlowNode() override;
};
