#pragma once
#include "forex.hpp"
#include "object.hpp"

class Correlation : public Object
{

  private:
    //! mandatory
    GslMatrix _matrix;
    vector<Forex*> _forex_list;
    vector<string> _underlying_list;
    vector<string> _single_list; // = forex_list + underlying_list
    void SetSingleList();

    //! sub matrix : contains useful information for pricing
    GslMatrix _cholesky_matrix;
    vector<Forex*> _cholesky_forex_list;
    vector<string> _cholesky_underlying_list;
    vector<string> _cholesky_single_list; // = forex_list + underlying_list
    void SetCholeskySingleList();

    //! additional
    set<string> _currency_list;
    string _pivot_currency;

    size_t LookAtPosition( const vector<string>& UnderlyingList, const string& u ) const;

    //! public
    Forex* GetForex( const string& UnderlyingCurrency,
                                   const string& BaseCurrency );

  public:
    //! cholesky
    void ComputeCholeskyMatrix( const vector<string>& UnderlyingList );

    //! setter
    void SetMatrix( gsl_vector* Matrix );
    void SetSymmetricMatrix( gsl_vector* SymmetricMatrix );
    void SetForexList( const vector<Forex*>& ForexList );
    void SetUnderlyingList( const vector<string>& UnderlyingList );
    void SetSubMatrix( const vector<string>& UnderlyingList,
                       vector<string> ForexList );

    //! access to matrix
    gsl_matrix* ExtractCholeskyMatrix( const vector<string> UnderlyingNames );
    gsl_matrix* ExtractMatrix( vector<string> UnderlyingNameList,
                               vector<Forex*> ForexList );
    gsl_matrix* ExtractMatrix();

    //! read correl matrix : 3 ways
    double GetValue( const string& udl1,
                     const string& udl2 );
    double GetValue( const string& fx_udl_ccy,
                     const string& fx_base_ccy,
                     const string& udl );
    double GetValue( const string& fx_udl_ccy1,
                     const string& fx_ccy1,
                     const string& fx_udl_ccy2,
                     const string& fx_ccy2 );

    double GetCholeskyValue( const string& u1,
                             const string& u2 );

    //! fx
    double GetFxSpot( const string& fx_udl_ccy,
                      const string& fx_base_ccy );
    double GetFxVol( const string& fx_udl_ccy,
                     const string& fx_base_ccy );
    set<string> GetForexNameList( set<string> currency_name_list );

    //!
    ForexSet GetForexSet( const string& UnderlyingCurrency,
                                        const string& BaseCurrency );

    //! mcl nodes
    MonteCarloNode* GetCholeskyNode( NodeCollector& NC,
                                     const string& Underlying1,
                                     const string& Underlying2 );
    MonteCarloNode* GetFxVolNode( NodeCollector& NC,
                                  const string& UnderlyingCurrency,
                                  const string& BaseCurrency );
    MonteCarloNode* GetCorrelNode( NodeCollector& NC,
                                   const string& UnderlyingCurrency,
                                   const string& BaseCurrency,
                                   const string& Underlying );
    MonteCarloNode* GetCorrelNode( NodeCollector& NC,
                                   const string& UnderlyingCurrency1,
                                   const string& BaseCurrency1,
                                   const string& UnderlyingCurrency2,
                                   const string& BaseCurrency2 );
    MonteCarloNode* GetFxNode( NodeCollector& NC,
                               const string& UnderlyingCurrency,
                               const string& BaseCurrency );

    // constructor, destructor
    Correlation( const string& ObjectName );
    ~Correlation() override;
};
