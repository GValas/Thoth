#pragma once
#include "monte_carlo_node.hpp"

//! Discrete-dividend node (escrowed-dividend model). For each diffusion date t_i it
//! holds the present value, *as of t_i*, of the cash dividends with ex-date strictly
//! after t_i — i.e. the dividend cash still embedded in the observed spot at t_i.
//! The values are precomputed per date (deterministic; no path / child dependency)
//! by Equity from its discount curve. The spot diffusion uses it to recover the
//! clean escrowed process each step and to re-add the future-dividend PV to the
//! observed spot it publishes — so it also gives the seam for dividend-sensitivity
//! Greeks and a future spot-jump diffusion.
class DividendNode : public MonteCarloNode
{
  private:
    vector<double> _future_pv; //!< future-dividend PV at each diffusion date (date-indexed)

  public:
    //! append the future-dividend PV for the next diffusion date (build order)
    void PushFuturePv( double Pv );

    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    DividendNode( const string& Name );
    ~DividendNode() override;
};
