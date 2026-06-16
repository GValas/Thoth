#pragma once
#include "asset.hpp"
#include "correlation.hpp"

//! Abstract diffusable underlying of a contract (mono / composite / basket /
//! rainbow): exposes the forward, implied vol and its single-name & currency sets,
//! and builds the MCL spot / vol / correlation nodes.
class Underlying : public Asset
{

    //
  protected:
    Correlation* _correlation; // quanto stuffs

    //
  public:
    //! setter
    // void SetSpot( const double Spot );
    // void SetCurrency( Currency * Currency );
    virtual void SetCorrelation( Correlation* Correlation );

    //! fwd & vol
    virtual double GetForward( const date& MaturityDate,
                               Currency* QuantoCurrency ) = 0;
    virtual double GetImplicitVol( const double Strike,
                                   const date& MaturityDate ) = 0;

    //! singles & ccys
    // virtual set<string> GetSingleNameList() = 0;
    // virtual set<string> GetCurrencyNameList() = 0;
    virtual SingleSet GetSingleSet() = 0;
    virtual CurrencySet GetCurrencySet() = 0;

    //! getter
    // Currency * GetCurrency();
    Correlation* GetCorrelation();

    //! mcl node
    virtual MonteCarloNode* GetNode( NodeCollector& NC ) = 0;
    virtual MonteCarloNode* GetVolNode( NodeCollector& NC ) = 0;
    virtual MonteCarloNode* GetCorrelNode( NodeCollector& NC,
                                           const string& UnderlyingCurrency,
                                           const string& BaseCurrency ) = 0;

    //!
    Underlying( const string& ObjectName,
                const string& ObjectKind );
    ~Underlying() override;
};
