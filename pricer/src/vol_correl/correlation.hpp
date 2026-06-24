#pragma once
#include "forex.hpp"
#include "market_data.hpp"

//! The correlation matrix over the book's underlyings and FX pairs: validated
//! positive-definite at load, it serves correlation entries to the analytic
//! engines and the Cholesky factor (cached) that correlates the MC noise factors.
//! A market-data input (correlation Greeks would override ApplyShift on "correl").
class Correlation : public MarketData
{

  private:
    //! mandatory
    LaMatrix _matrix;
    vector<Forex*> _forex_list;
    vector<string> _underlying_list;
    vector<string> _single_list; // = forex_list + underlying_list
    void SetSingleList();

    //! sub matrix : contains useful information for pricing
    LaMatrix _cholesky_matrix;
    vector<Forex*> _cholesky_forex_list;
    vector<string> _cholesky_underlying_list;
    vector<string> _cholesky_single_list; // = forex_list + underlying_list
    void SetCholeskySingleList();

    //! cache key: the requested sub-set the current _cholesky_matrix was built for.
    //! The factor depends only on this sub-set and the (run-immutable) correlation
    //! matrix, so a matching key lets ComputeCholeskyMatrix skip the rebuild that
    //! every bump-and-revalue reprice would otherwise repeat. Cleared by SetMatrix.
    string _cholesky_key;

    //! additional
    set<string> _currency_list;
    string _pivot_currency;

    size_t LookAtPosition( const vector<string>& UnderlyingList, const string& u ) const;

    //! public
    Forex* GetForex( const string& UnderlyingCurrency,
                     const string& BaseCurrency );

  public:
    //! read own fields: the matrix (full or symmetric/lower-triangular form) plus
    //! the optional underlying / forex name lists it indexes
    void Configure( ObjectReader& reader ) override;

    //! cholesky
    void ComputeCholeskyMatrix( const vector<string>& UnderlyingList );

    //! setter (takes ownership of Matrix: the RAII LaVector frees it on return)
    void SetMatrix( LaVector Matrix );
    void SetSymmetricMatrix( la_vector* SymmetricMatrix );
    void SetForexList( const vector<Forex*>& ForexList );
    void SetUnderlyingList( const vector<string>& UnderlyingList );

    //! access to matrix (ExtractMatrix returns a freshly-built, caller-owned matrix)
    LaMatrix ExtractMatrix( const vector<string>& UnderlyingNameList,
                            const vector<Forex*>& ForexList );

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
    set<string> GetForexNameList( const set<string>& currency_name_list );

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
