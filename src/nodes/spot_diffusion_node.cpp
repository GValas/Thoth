#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

//! Construct with all inputs unwired and the spot zeroed; the graph builder sets
//! the children and the spot before the first path.
SpotDiffusionNode::SpotDiffusionNode( const string& Name ) : MonteCarloNode( Name )
{
    _local_vol_node = nullptr;
    _drift_node = nullptr;
    _brownian_node = nullptr;
    _spot = 0;
}

SpotDiffusionNode::~SpotDiffusionNode() = default;

//! Fill this date's spot. Side effect: writes _value_list[DateIndex]. Date 0 is the
//! observed spot today; later dates advance the previous value by one log-Euler GBM
//! step (with optional escrowed-dividend handling and Milstein correction).
void SpotDiffusionNode::ComputeValue( size_t DateIndex )
{
    //! diffusion value
    if ( DateIndex > 0 )
    {
        //! escrowed-dividend model: the published value is the observed spot
        //! (clean process + future-dividend PV). Recover the clean escrowed value
        //! of the previous step by stripping its future-dividend PV before the GBM
        //! step, then re-add this step's PV. No dividend node -> both PVs are 0 and
        //! this is the plain GBM step.
        double div_prev = _dividend_node ? _dividend_node->GetValue( DateIndex - 1 ) : 0;
        double s0 = _value_list[DateIndex - 1] - div_prev;
        double w1 = _brownian_node->GetValue( DateIndex );
        double w0 = _brownian_node->GetValue( DateIndex - 1 );
        double dw = w1 - w0;
        double v = _local_vol_node->GetValue( DateIndex );
        double dt = _dt_list[DateIndex];

        //! term-structured carry: the drift node carries the net zero rate to each
        //! date (r_dom - r_for - repo - div), so the deterministic log-drift over
        //! this step is the difference of the cumulative carries, i.e. the integral
        //! of the forward carry across [t_{i-1}, t_i]. This makes the simulated
        //! forward reproduce the curve exactly on a steep term structure; for a flat
        //! carry c it collapses to c*(t_i - t_{i-1}) = c*dt, the old flat behaviour.
        double carry_drift = _drift_node->GetValue( DateIndex ) * _t_list[DateIndex] -
                             _drift_node->GetValue( DateIndex - 1 ) * _t_list[DateIndex - 1];

        //! log-Euler step for d(lnS) = carry dt - v^2/2 dt + v dW (exact for const v).
        double expo = carry_drift - v * v / 2 * dt + v * dw;

        //! log-space Milstein correction for a state-dependent (local) vol:
        //! + 1/2 v (dv/dlnS) (dW^2 - dt). Vanishes when v is constant, so this only
        //! refines the local-vol diffusion; it is enabled only there.
        if ( _milstein_lv )
        {
            double dv_dlns = _milstein_lv->LogSpotDerivative( DateIndex );
            expo += 0.5 * v * dv_dlns * ( dw * dw - dt );
        }

        double div_now = _dividend_node ? _dividend_node->GetValue( DateIndex ) : 0;
        _value_list[DateIndex] = s0 * exp( expo ) + div_now;
    }
    //! spot (observed spot at today = clean escrowed spot + total future-dividend PV)
    else if ( DateIndex == 0 )
    {
        _value_list[DateIndex] = _spot;
    }
    //! spot record
    else
    {
        _value_list[DateIndex] = 0;
    }
}

//! wire the per-step vol node (constant-vol or local-vol surface).
void SpotDiffusionNode::SetLocalVolNode( MonteCarloNode* N )
{
    _local_vol_node = N;
}

//! enable the log-space Milstein correction, reading d(vol)/d(lnS) from Lv.
void SpotDiffusionNode::EnableMilstein( LocalVolatilityNode* Lv )
{
    _milstein_lv = Lv;
}

//! wire the drift node (cumulative net carry per date).
void SpotDiffusionNode::SetDriftNode( MonteCarloNode* N )
{
    _drift_node = N;
}

//! wire the Brownian W node supplying the per-step increment.
void SpotDiffusionNode::SetBrownianNode( MonteCarloNode* N )
{
    _brownian_node = N;
}

//! attach the optional discrete-dividend escrow node (future-dividend PV per date).
void SpotDiffusionNode::SetDividendNode( MonteCarloNode* N )
{
    _dividend_node = N;
}

//! set the spot today; also writes date 0 directly so it is available before the
//! first ComputeValue pass.
void SpotDiffusionNode::SetSpot( double Spot )
{
    _spot = Spot;
    _value_list[0] = _spot;
}
//! Date 0 (the spot today) is identical on every path, so mark it constant; the
//! engine then evaluates it once instead of per path.
bool SpotDiffusionNode::IsConstant( size_t DateIndex )
{
    return ( DateIndex == 0 );
}

//! Declare the inputs the step at DateIndex needs. Date 0 has none (it is the fixed
//! spot). A later step reads vol/drift/Brownian at this date, the drift and Brownian
//! at the previous date (for the differenced carry and dW), this node itself at the
//! previous date (the GBM recursion), and the dividend PV at both dates if escrowed.
void SpotDiffusionNode::GetDateDependencies( size_t DateIndex,
                                             vector<MonteCarloNode*>& NodeList,
                                             vector<size_t>& DateList )
{
    if ( DateIndex > 0 )
    {
        NodeList.push_back( _local_vol_node );
        DateList.push_back( DateIndex );
        NodeList.push_back( _drift_node );
        DateList.push_back( DateIndex );
        //! the term-structured carry over the step also reads the previous date's
        //! cumulative carry (drift node), so depend on it at DateIndex - 1 too
        NodeList.push_back( _drift_node );
        DateList.push_back( DateIndex - 1 );
        NodeList.push_back( _brownian_node );
        DateList.push_back( DateIndex );
        NodeList.push_back( _brownian_node );
        DateList.push_back( DateIndex - 1 );
        NodeList.push_back( this );
        DateList.push_back( DateIndex - 1 );
        //! the observed-spot step reads the future-dividend PV at this and the
        //! previous date (to strip / re-add the escrow)
        if ( _dividend_node )
        {
            NodeList.push_back( _dividend_node );
            DateList.push_back( DateIndex );
            NodeList.push_back( _dividend_node );
            DateList.push_back( DateIndex - 1 );
        }
    }
}
