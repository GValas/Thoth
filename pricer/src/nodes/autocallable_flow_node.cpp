#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

AutocallableFlowNode::AutocallableFlowNode( const string& Name ) : MonteCarloNode( Name )
{
}

AutocallableFlowNode::~AutocallableFlowNode() = default;

//! Emit this schedule date's cash flow. Side effect: writes _value_list[DateIndex].
void AutocallableFlowNode::ComputeValue( size_t DateIndex )
{
    //! the flow only pays at its own date
    if ( DateIndex != _flow_date_index )
    {
        _value_list[DateIndex] = 0;
        return;
    }

    //! an autocall flow inspects the prior observations plus its own (last in the
    //! list); the maturity flow inspects every autocall observation
    const size_t prior = _is_maturity ? _autocall_index_list.size()
                                      : _autocall_index_list.size() - 1;
    for ( size_t i = 0; i < prior; i++ )
    {
        if ( _spot_node->GetValue( _autocall_index_list[i] ) >= _autocall_level )
        {
            _value_list[DateIndex] = 0; //!< already autocalled earlier: this flow is dead
            return;
        }
    }

    //! the observed spot deciding this date's flow: the own observation for an
    //! autocall date, the terminal spot for the maturity flow
    const double s = _is_maturity ? _spot_node->GetValue( _flow_date_index )
                                  : _spot_node->GetValue( _autocall_index_list.back() );

    //! Phoenix: the date's conditional coupon, paid ALIVE at this date whenever
    //! S >= coupon level; with memory it also recovers the consecutively missed
    //! coupons since the last payment (scanned backward over the prior alive
    //! observations — all prior dates are alive here, the kill test passed)
    double coupon_flow = 0;
    if ( _is_phoenix && s >= _coupon_level )
    {
        size_t periods = 1;
        if ( _coupon_memory )
        {
            for ( size_t i = prior; i-- > 0; )
            {
                if ( _spot_node->GetValue( _autocall_index_list[i] ) >= _coupon_level )
                {
                    break; //!< that date paid (and caught up), memory is clear
                }
                periods++;
            }
        }
        coupon_flow = (double)periods * _nominal * _coupon;
    }

    if ( !_is_maturity )
    {
        //! first trigger exactly here -> the redemption: Athena pays the accrued
        //! ("snowball") rebate; Phoenix pays the bare nominal (its coupon flow,
        //! implied by S >= autocall >= coupon level, is added separately above)
        double redemption = 0;
        if ( s >= _autocall_level )
        {
            redemption = _is_phoenix ? _nominal : _nominal * ( 1 + (double)_position * _coupon );
        }
        _value_list[DateIndex] = redemption + coupon_flow;
        return;
    }

    //! survived to maturity: the redemption profile on the terminal spot
    double payoff;
    if ( !_is_phoenix && s >= _autocall_level )
    {
        payoff = _nominal * ( 1 + (double)_position * _coupon ); //!< Athena full snowball
    }
    else if ( s >= _protection_level )
    {
        payoff = _nominal;
    }
    else
    {
        payoff = _nominal * s / _reference_spot; //!< linear capital at risk
    }
    _value_list[DateIndex] = payoff + coupon_flow;
}

void AutocallableFlowNode::SetSpotNode( MonteCarloNode* N )
{
    _spot_node = N;
}

void AutocallableFlowNode::SetFlowDateIndex( size_t DateIndex )
{
    _flow_date_index = DateIndex;
}

void AutocallableFlowNode::SetPosition( size_t Position, bool IsMaturity )
{
    _position = Position;
    _is_maturity = IsMaturity;
}

void AutocallableFlowNode::SetLevels( double AutocallLevel, double ProtectionLevel,
                                      double ReferenceSpot )
{
    _autocall_level = AutocallLevel;
    _protection_level = ProtectionLevel;
    _reference_spot = ReferenceSpot;
}

void AutocallableFlowNode::SetPayout( double Nominal, double Coupon )
{
    _nominal = Nominal;
    _coupon = Coupon;
}

void AutocallableFlowNode::SetAutocallIndexList( const vector<size_t>& Indices )
{
    _autocall_index_list = Indices;
}

void AutocallableFlowNode::SetPhoenix( double CouponLevel, bool Memory )
{
    _is_phoenix = true;
    _coupon_level = CouponLevel;
    _coupon_memory = Memory;
}

//! at the flow date, declare the spot at every inspected observation; the
//! maturity flow also reads the terminal spot at its own date
void AutocallableFlowNode::GetDateDependencies( size_t DateIndex,
                                                vector<MonteCarloNode*>& NodeList,
                                                vector<size_t>& DateList )
{
    if ( DateIndex != _flow_date_index )
    {
        return;
    }
    for ( size_t idx : _autocall_index_list )
    {
        NodeList.push_back( _spot_node );
        DateList.push_back( idx );
    }
    if ( _is_maturity )
    {
        NodeList.push_back( _spot_node );
        DateList.push_back( _flow_date_index );
    }
}
