#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

BarrierFlowNode::BarrierFlowNode( const string& Name ) : MonteCarloNode( Name )
{
    _spot_node = nullptr;
    _strike = 0;
    _floor = 0;
    _flow_date_index = 0;
    _barrier_level = 0;
    _is_up = false;
    _is_in = false;
}

BarrierFlowNode::~BarrierFlowNode() = default;

void BarrierFlowNode::ComputeValue( size_t DateIndex )
{
    if ( DateIndex == _flow_date_index )
    {
        //! has the (continuously monitored) barrier been touched along the path ?
        bool hit = false;
        for ( size_t idx : _monitor_index_list )
        {
            double s = _spot_node->GetValue( idx );
            if ( _is_up ? ( s >= _barrier_level ) : ( s <= _barrier_level ) )
            {
                hit = true;
                break;
            }
        }

        //! alive : knock-in & touched, or knock-out & not touched
        bool alive = _is_in ? hit : !hit;
        double sT = _spot_node->GetValue( _flow_date_index );
        _value_list[DateIndex] = alive
                                     ? payoff_vanilla( sT, _strike, _type, false, 0, true, _floor )
                                     : 0;
    }
    else
    {
        _value_list[DateIndex] = 0;
    }
}

void BarrierFlowNode::GetDateDependencies( size_t DateIndex,
                                           vector<MonteCarloNode*>& NodeList,
                                           vector<size_t>& DateList )
{
    if ( DateIndex == _flow_date_index )
    {
        //! the payoff depends on the spot at every monitoring date ...
        for ( size_t idx : _monitor_index_list )
        {
            NodeList.push_back( _spot_node );
            DateList.push_back( idx );
        }
        //! ... and at the flow (maturity) date for the terminal payoff
        NodeList.push_back( _spot_node );
        DateList.push_back( _flow_date_index );
    }
}

void BarrierFlowNode::SetStrike( double Strike )
{
    _strike = Strike;
}

void BarrierFlowNode::SetFloor( double Floor )
{
    _floor = Floor;
}

void BarrierFlowNode::SetType( OptionType Type )
{
    _type = Type;
}

void BarrierFlowNode::SetFlowDateIndex( size_t DateIndex )
{
    _flow_date_index = DateIndex;
}

void BarrierFlowNode::SetSpotNode( MonteCarloNode* N )
{
    _spot_node = N;
}

void BarrierFlowNode::SetBarrierLevel( double Level )
{
    _barrier_level = Level;
}

void BarrierFlowNode::SetIsUp( bool IsUp )
{
    _is_up = IsUp;
}

void BarrierFlowNode::SetIsIn( bool IsIn )
{
    _is_in = IsIn;
}

void BarrierFlowNode::SetMonitorIndexList( const vector<size_t>& Indices )
{
    _monitor_index_list = Indices;
}
