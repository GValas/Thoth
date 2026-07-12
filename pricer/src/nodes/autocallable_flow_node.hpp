#pragma once
#include "monte_carlo_node.hpp"

//! One schedule date of an autocallable note (see the Autocallable contract).
//! At its own flow date the node inspects the spot at the schedule observations:
//!  - an AUTOCALL flow (position k) pays nominal * (1 + k * coupon) iff the
//!    first trigger of the path is exactly its own date (all prior observations
//!    below the autocall level, its own at or above);
//!  - the MATURITY flow (position n+1) pays the redemption profile — accrued
//!    coupon / nominal / linear capital loss — iff no prior observation
//!    triggered.
//! Zero on every other date and on every path where the condition fails. The
//! per-date flows sum inside the ContractNode, each discounted from its own
//! payment date (pathwise under Hull-White), so exactly one flow pays per path.
class AutocallableFlowNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _spot_node = nullptr; //!< underlying spot path
    size_t _flow_date_index = 0;          //!< this flow's payment date index
    size_t _position = 0;                 //!< 1-based observation count (n+1 = maturity)
    bool _is_maturity = false;            //!< redemption flow vs autocall flow
    //! schedule observations to inspect: prior dates + own for an autocall flow,
    //! every autocall date for the maturity flow
    vector<size_t> _autocall_index_list;
    double _autocall_level = 0;   //!< cash trigger level
    double _protection_level = 0; //!< cash protection level (maturity only)
    double _reference_spot = 0;   //!< S_ref of the linear capital-loss leg
    double _nominal = 100;
    double _coupon = 0; //!< per-observation coupon (decimal)
    //! Phoenix flavour: per-period conditional coupon (cash level + memory flag);
    //! _is_phoenix false keeps the Athena snowball behaviour
    bool _is_phoenix = false;
    bool _coupon_memory = false;
    double _coupon_level = 0;

  public:
    //! this date's autocall / redemption cash flow; 0 elsewhere
    void ComputeValue( size_t DateIndex ) override;
    //! at the flow date, depends on the spot at every inspected observation
    //! (plus its own date for the maturity redemption)
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setters
    void SetSpotNode( MonteCarloNode* N );
    void SetFlowDateIndex( size_t DateIndex );
    void SetPosition( size_t Position, bool IsMaturity );
    void SetLevels( double AutocallLevel, double ProtectionLevel, double ReferenceSpot );
    void SetPayout( double Nominal, double Coupon );
    void SetAutocallIndexList( const vector<size_t>& Indices );
    //! enable the Phoenix flavour (per-period conditional coupon, optional memory)
    void SetPhoenix( double CouponLevel, bool Memory );

    AutocallableFlowNode( const string& Name );
    ~AutocallableFlowNode() override;
};
