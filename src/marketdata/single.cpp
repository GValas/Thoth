#include "thoth.hpp"
#include "single.hpp"

//! constructor
Single::Single( const string& ObjectName,
                const string& ObjectKind ) : Asset( ObjectName, ObjectKind )
{
    _volatility = nullptr;
}

//! destructor
Single::~Single() = default;

//! setter
void Single::SetVolatility( Volatility& Volatility )
{
    _volatility = &Volatility;
}

//! setter
void Single::SetToday( const date& Today )
{
    _volatility->SetToday( Today );
    Asset::SetToday( Today );
}

//! setter
void Single::SetSpot( double Spot )
{
    _spot = Spot;
}

//! getter
Volatility* Single::GetVolatility()
{
    return _volatility;
}

//! getter
double Single::GetSpot()
{
    return _spot;
}

//! implicit vol
double Single::GetImplicitVol( const double Strike,
                               const date& MaturityDate )
{
    //! supply the underlying's forward so a forward-measure surface (SABR) is
    //! evaluated correctly; flat surfaces ignore it
    return _volatility->GetImplicitVol( Strike, GetForward( MaturityDate ), MaturityDate );
}

//! mcl nodes
MonteCarloNode* Single::GetNode( NodeCollector& NC )

{
    //! stochastic vol (Heston): a dedicated variance + spot diffusion (Andersen
    //! QE) instead of the constant-vol GBM step. Reuses the shared "#white_noise"
    //! for the spot's independent Gaussian and "#vol_white_noise" for the variance.
    if ( _volatility->IsStochastic() )
    {
        const StochasticVolParams h = _volatility->StochasticParams();
        MonteCarloNode* var = NC.GetOrCreate<HestonVarianceNode>(
            _name + "#variance",
            [&]( HestonVarianceNode* V )
            {
                V->SetParameters( h.v0, h.kappa, h.theta, h.xi );
                V->SetNoiseNode( NC.GetNode( _name + "#vol_white_noise" ) );
            } );
        return NC.GetOrCreate<HestonSpotNode>(
            _name + "#spot",
            [&]( HestonSpotNode* S )
            {
                S->SetParameters( h.kappa, h.theta, h.xi, h.rho );
                S->SetVarianceNode( var );
                S->SetDriftNode( GetDriftNode( NC ) );
                S->SetNoiseNode( NC.GetNode( _name + "#white_noise" ) );
                //! Bates : wire the jump source if this stochastic vol carries jumps
                if ( h.has_jumps() )
                {
                    S->SetJumpNode( NC.GetNode( _name + "#jump_noise" ) );
                }
                S->SetSpot( _spot );
            } );
    }

    return NC.GetOrCreate<SpotDiffusionNode>(
        _name + "#spot",
        [&]( SpotDiffusionNode* S )
        {
            S->SetBrownianNode( NC.GetNode( _name + "#brownian" ) );
            S->SetDriftNode( GetDriftNode( NC ) );
            S->SetSpot( _spot );
            //! local-vol surface (e.g. SABR): sample the Dupire surface onto a
            //! per-date log-spot grid the diffusion reads along its own path.
            //! Flat surface (bs): one constant-vol node.
            if ( _volatility->_is_local )
            {
                LocalVolatilityNode* lv = BuildLocalVolNode( NC, S );
                S->SetLocalVolNode( lv );
                //! always refine the local-vol diffusion with the Milstein step
                //! (its correction is a no-op for constant vol, so it is only ever
                //! applied here, where the vol genuinely depends on the spot)
                S->EnableMilstein( lv );
            }
            else
            {
                S->SetLocalVolNode( GetVolNode( NC ) );
            }
        } );
}

MonteCarloNode* Single::GetVolNode( NodeCollector& NC )
{
    //! A local surface (SABR) has no single scalar "vol node" — the spot diffusion
    //! reads its full grid (see BuildLocalVolNode). The remaining scalar-vol callers
    //! (the quanto drift correction, the composite vol/correl nodes) only need a
    //! representative level, so give them a constant ATM vol at the last diffusion
    //! date — the same ATM vol the ANA/PDE quanto correction uses. Rebuilt per Greek
    //! scenario (suffixed GetOrCreate) so a vega bump re-samples it.
    if ( _volatility->_is_local )
    {
        const vector<date>& dates = NC.GetDateList();
        double atm = GetImplicitVol( 0, dates.back() );
        return NC.GetOrCreate<ConstantNode>( _name + "#atmvol",
                                             [&]( ConstantNode* C )
                                             { C->SetConstantValue( atm ); } );
    }
    return _volatility->GetNode( NC );
}

//! half-width of the log-spot grid in ATM-vol standard deviations: wide enough that
//! almost every diffused path lands inside it (paths beyond clamp to the boundary
//! local vol). Generous vs the PDE's factor because MC paths reach further tails.
static constexpr double LOCAL_VOL_GRID_SIGMA_FACTOR = 8.0;
//! log-spot grid points per diffusion date (odd -> a node sits on the spot)
static constexpr int LOCAL_VOL_GRID_POINTS = 201;

LocalVolatilityNode* Single::BuildLocalVolNode( NodeCollector& NC, MonteCarloNode* SpotNode )
{
    return static_cast<LocalVolatilityNode*>( NC.GetOrCreate<LocalVolatilityNode>(
        _name + "#localvol",
        [&]( LocalVolatilityNode* L )
        {
            L->SetSpotNode( SpotNode );

            const vector<date>& dates = NC.GetDateList();
            const date today = dates.front();
            const double ln_spot = log( _spot );

            //! one Dupire-sampled log-spot grid per diffusion date
            for ( size_t k = 0; k < dates.size(); k++ )
            {
                double T = YearFraction( today, dates[k] );
                if ( T <= 0 )
                {
                    T = 1.0 / NB_OF_DAYS_A_YEAR; //!< today: degenerate, never diffused
                }
                double v_atm = GetImplicitVol( 0, dates[k] ); //!< ATM vol, grid sizing only
                double half_width = LOCAL_VOL_GRID_SIGMA_FACTOR * v_atm * sqrt( T );
                double ln_step = 2.0 * half_width / ( LOCAL_VOL_GRID_POINTS - 1 );
                if ( ln_step <= 0 )
                {
                    ln_step = 1e-4; //!< guard for v_atm ~ 0
                }
                //! signed log-spot index of the grid's first point, so grid point i
                //! sits at log-spot = (offset + i) * ln_step, centred on the spot
                long offset = (long)floor( ( ln_spot - half_width ) / ln_step );

                la_vector* vol = la_vector_alloc( LOCAL_VOL_GRID_POINTS );
                for ( int i = 0; i < LOCAL_VOL_GRID_POINTS; i++ )
                {
                    double s_grid = exp( ( offset + i ) * ln_step );
                    la_vector_set( vol, i, GetLocalVolatility( s_grid, dates[k] ) );
                }
                L->PushLnStep( ln_step );
                L->PushOffset( offset );
                L->PushVolVector( vol );
            }
        } ) );
}