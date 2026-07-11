#pragma once
#include "forex.hpp"
#include "market_data.hpp"

//! The correlation matrix over the book's underlyings and FX pairs: validated
//! positive-definite at load, it serves correlation entries to the analytic
//! engines and the Cholesky factor (cached) that correlates the MC noise factors.
//! A market-data input (correlation Greeks would override ApplyShift on "correl").
//!
//! Term structure (optional): with a `maturities` pillar list the matrix becomes a
//! piecewise-linear function of time C(t) — one matrix per pillar, entries lerped
//! between pillars and flat beyond (a convex combination of correlation matrices
//! is a correlation matrix, so C(t) stays valid everywhere). Consumers read two
//! integrated views, both exact for the piecewise-linear form:
//!  - GetTermValue: the running average rho_bar(T) = (1/T) integral_0^T rho(t) dt,
//!    for the analytic quanto / composite / basket formulas (their corr*vol
//!    products are LINEAR in the instantaneous entries, so feeding the averaged
//!    entries through the existing triangle algebra is exact). Exception: a
//!    composite used as a correlated LEG of a further quanto goes through the
//!    composite-vol sqrt (nonlinear in rho) — an approximation under a term
//!    structure, exact for a constant matrix;
//!  - EnsureCholeskySchedule / GetCholeskyValue(u1,u2,idx): one Cholesky factor
//!    per diffusion step, of the STEP-average matrix over [t_{i-1}, t_i], so the
//!    correlated Brownian increments reproduce the exact integrated covariance.
//! Without `maturities` everything reduces to the previous constant behaviour.
class Correlation : public MarketData
{

  private:
    //! mandatory
    LaMatrix _matrix; //!< constant matrix, or the FIRST pillar of a term structure
    //! term structure (empty _pillar_list = constant matrix, the historic behaviour)
    vector<double> _pillar_list;   //!< pillar maturities, year fractions, increasing
    vector<LaMatrix> _matrix_list; //!< one validated matrix per pillar
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

    //! term structure: one factor per diffusion date (of the step-average matrix
    //! over [t_{i-1}, t_i]); cached against the schedule it was built for, same
    //! sub-set as _cholesky_single_list (EnsureCholeskySchedule requires a prior
    //! ComputeCholeskyMatrix). Cleared with the scalar cache by SetMatrix.
    vector<LaMatrix> _cholesky_schedule;
    vector<double> _cholesky_schedule_t;
    string _cholesky_schedule_key; //!< the _cholesky_key the schedule was built for

    //! --- term-structure entry maths (all no-ops reading _matrix when constant) ---
    //! integral_0^t of entry (i,j): closed form for the piecewise-linear pillars
    double EntryPrimitive( size_t i, size_t j, double t ) const;
    //! average of entry (i,j) over [a,b]; a == b returns the instantaneous value
    double AvgEntry( size_t i, size_t j, double a, double b ) const;
    double AvgEntryByName( const string& u1, const string& u2, double a, double b );
    //! the pivot-triangle formulas of GetValue, over window-averaged entries
    double AvgValue( const string& I, const string& J, const string& S,
                     double a, double b );
    double AvgValue( const string& I, const string& J,
                     const string& M, const string& N,
                     double a, double b );
    //! window-averaged dense sub-matrix (the (0,0) window = today's entries)
    LaMatrix ExtractMatrix( const vector<string>& UnderlyingNameList,
                            const vector<Forex*>& ForexList,
                            double a, double b );
    //! store the pillar matrices (validated square/PSD, consistent sizes)
    void SetTermMatrices( vector<LaMatrix> MatrixList );
    //! reject term-varying "<name>_var" (spot/variance) entries: the Heston /
    //! LSV engines consume a single scalar rho, so a pillar-dependent one would
    //! silently misprice — fail fast at load instead
    void ValidateTermVarEntries();

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
    //! same, with each entry averaged over [0, T] (term view for moment matching)
    LaMatrix ExtractTermMatrix( const vector<string>& UnderlyingNameList,
                                const vector<Forex*>& ForexList,
                                double T );

    //! true when a `maturities` pillar list makes the matrix time-dependent
    bool IsTermStructured() const;

    //! read correl matrix : 3 ways — today's (t = 0) instantaneous entries
    double GetValue( const string& udl1,
                     const string& udl2 );
    double GetValue( const string& fx_udl_ccy,
                     const string& fx_base_ccy,
                     const string& udl );
    double GetValue( const string& fx_udl_ccy1,
                     const string& fx_ccy1,
                     const string& fx_udl_ccy2,
                     const string& fx_ccy2 );

    //! the same 3 reads, averaged over [0, T] (equal to GetValue when constant)
    double GetTermValue( const string& udl1,
                         const string& udl2,
                         double T );
    double GetTermValue( const string& fx_udl_ccy,
                         const string& fx_base_ccy,
                         const string& udl,
                         double T );
    double GetTermValue( const string& fx_udl_ccy1,
                         const string& fx_ccy1,
                         const string& fx_udl_ccy2,
                         const string& fx_ccy2,
                         double T );
    //! (cross-FX, underlying) correlation averaged over a forward window [t1, t2]
    //! (the PDE's per-step quanto carry)
    double GetStepValue( const string& fx_udl_ccy,
                         const string& fx_base_ccy,
                         const string& udl,
                         double t1,
                         double t2 );

    double GetCholeskyValue( const string& u1,
                             const string& u2 );

    //! term structure: build (once per schedule) the per-step Cholesky factors of
    //! the step-average matrices, then serve their entries by date index
    void EnsureCholeskySchedule( const vector<double>& TList );
    double GetCholeskyValue( const string& u1,
                             const string& u2,
                             size_t DateIndex );
    //! false only when L(u1,u2) is structurally zero at every date (lets the MC
    //! graph skip wiring the pair); constant case = the scalar factor's zero test
    bool CholeskyMayBeNonZero( const string& u1,
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
