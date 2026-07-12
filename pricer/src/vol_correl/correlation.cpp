//! correlation.cpp — the book-wide correlation matrix.
//!
//! Holds one symmetric positive-definite correlation matrix indexed by the book's
//! singles (equity underlyings + FX pairs). It (1) validates SPD at load, (2)
//! serves pairwise correlations to the analytic engines — including the triangle
//! algebra that derives cross-FX correlations through a pivot currency from a
//! basis of pivot/X pairs, and (3) caches the lower Cholesky factor of any
//! requested sub-matrix to correlate the Monte-Carlo noise factors.
//!
//! FX convention: a Forex "A_X" is the pivot-anchored pair (pivot currency A vs X),
//! and an arbitrary X/Y rate is reconstructed as A_Y / A_X via the triangle rules
//! in the three GetValue overloads.

#include "thoth.hpp"
#include "maths.hpp"
#include "correlation.hpp"
#include "object_reader.hpp"

//!
Correlation::Correlation( const string& ObjectName ) : MarketData( ObjectName, KIND_CORRELATION_MATRIX )
{
}

//!
Correlation::~Correlation() = default;

//! read the matrix (full "matrix" or symmetric/lower-triangular "symmetric_matrix"
//! form) and the optional "underlyings" / "forexs" name lists that index its rows
//! and columns. With the optional "maturities" pillar list (year fractions,
//! strictly increasing) the same flat field carries the P pillar matrices
//! CONCATENATED — its length must be P times the single-matrix length — and the
//! matrix becomes piecewise-linear in time (see the class comment).
void Correlation::Configure( ObjectReader& reader )
{
    vector<double> pillars;
    if ( reader.Has<vector<double>>( "maturities" ) )
    {
        pillars = reader.Get<vector<double>>( "maturities" );
        for ( size_t p = 0; p < pillars.size(); p++ )
        {
            if ( pillars[p] <= 0 || ( p > 0 && pillars[p] <= pillars[p - 1] ) )
            {
                ERR( "correlation '" + _name +
                     "' : maturities must be positive and strictly increasing" );
            }
        }
    }

    if ( reader.Has<vector<double>>( "matrix" ) )
    {
        if ( pillars.size() > 1 )
        {
            //! split the concatenation into P equal chunks, each a full row-major matrix
            LaVector flat( reader.LaVector( "matrix" ) );
            const size_t P = pillars.size();
            if ( flat->size % P != 0 )
            {
                ERR( "correlation '" + _name +
                     "' : matrix length is not a multiple of the maturities count" );
            }
            const size_t unit = flat->size / P;
            const double n_sqrt = sqrt( (double)unit );
            if ( n_sqrt != (int)n_sqrt )
            {
                ERR( "correlation '" + _name + "' : per-pillar matrix size is invalid" );
            }
            vector<LaMatrix> mats;
            for ( size_t p = 0; p < P; p++ )
            {
                LaVector chunk( la_vector_alloc( unit ) );
                for ( size_t k = 0; k < unit; k++ )
                {
                    la_vector_set( chunk, k, la_vector_get( flat, p * unit + k ) );
                }
                mats.emplace_back( ext_la_vector_to_matrix( chunk, (size_t)n_sqrt ) );
            }
            _pillar_list = pillars;
            SetTermMatrices( std::move( mats ) );
        }
        else
        {
            SetMatrix( reader.LaVector( "matrix" ) ); //!< 0/1 pillar: constant matrix
        }
    }
    else if ( reader.Has<vector<double>>( "symmetric_matrix" ) )
    {
        if ( pillars.size() > 1 )
        {
            //! split the concatenation into P equal lower-triangle chunks, mirror each
            LaVector flat( reader.LaVector( "symmetric_matrix" ) );
            const size_t P = pillars.size();
            if ( flat->size % P != 0 )
            {
                ERR( "correlation '" + _name +
                     "' : symmetric_matrix length is not a multiple of the maturities count" );
            }
            const size_t unit = flat->size / P;
            //! invert unit = n(n+1)/2  ->  n = (-1 + sqrt(1 + 8*unit)) / 2
            const double nd = ( -1.0 + sqrt( 1.0 + 8.0 * (double)unit ) ) / 2.0;
            const size_t n = (size_t)( nd + 0.5 );
            if ( n * ( n + 1 ) / 2 != unit )
            {
                ERR( "correlation '" + _name +
                     "' : per-pillar symmetric_matrix length is not a triangular number" );
            }
            vector<LaMatrix> mats;
            for ( size_t p = 0; p < P; p++ )
            {
                LaMatrix full = la_matrix_alloc( n, n );
                size_t k = p * unit;
                for ( size_t i = 0; i < n; i++ )
                {
                    for ( size_t j = 0; j <= i; j++ )
                    {
                        const double v = la_vector_get( flat, k++ );
                        la_matrix_set( full, i, j, v );
                        la_matrix_set( full, j, i, v );
                    }
                }
                mats.push_back( std::move( full ) );
            }
            _pillar_list = pillars;
            SetTermMatrices( std::move( mats ) );
        }
        else
        {
            SetSymmetricMatrix( reader.LaVector( "symmetric_matrix" ) );
        }
    }
    else
    {
        ERR( ".matrix & .symmetric matrix are missing" );
    }
    if ( reader.Has<vector<string>>( "underlyings" ) )
    {
        SetUnderlyingList( reader.Get<vector<string>>( "underlyings" ) );
    }
    if ( reader.Has<vector<string>>( "forexs" ) )
    {
        SetForexList( reader.Ref<vector<Forex>>( "forexs" ) );
    }
    //! the "<name>_var" spot/variance entries must not vary across pillars (the
    //! stochastic-vol engines read one scalar rho); needs the name lists, so last
    if ( IsTermStructured() )
    {
        ValidateTermVarEntries();
    }
}

//! setter (takes ownership of Matrix). The YAML supplies the matrix flattened
//! row-major into one vector; a perfect-square length n means an (n_sqrt x n_sqrt)
//! matrix, otherwise the input is malformed.
void Correlation::SetMatrix( LaVector Matrix )
{
    size_t n = Matrix->size;
    double n_sqrt = sqrt( (double)n );
    if ( n_sqrt == (int)n_sqrt ) //!< length is a perfect square -> reshape to square
    {
        //! bound the dimension: an n x n dense matrix plus its O(n^3) Cholesky is
        //! driven by the input list length, so cap it (untrusted-YAML trust
        //! boundary; see constants.hpp) before allocating.
        if ( (size_t)n_sqrt > (size_t)CORRELATION_MAX_DIM )
        {
            ERR( "correlation '" + _name + "' : matrix dimension exceeds " +
                 std::to_string( CORRELATION_MAX_DIM ) );
        }
        _matrix = ext_la_vector_to_matrix( Matrix, (size_t)n_sqrt );
        //! Matrix (the source la_vector) is freed by its RAII LaVector at return
        _cholesky_key.clear(); //!< invalidate any cached Cholesky factor
        _cholesky_schedule.clear();
        _cholesky_schedule_t.clear();
        _cholesky_schedule_key.clear();
        //! a plain matrix makes the correlation constant again (e.g. the historical
        //! computation overwriting a term-structured input)
        _pillar_list.clear();
        _matrix_list.clear();

        //! fail fast on an invalid correlation matrix: every engine needs it
        //! symmetric positive-definite (MCL Cholesky-factorises it; ANA/PDE read
        //! its entries directly and would otherwise misprice silently).
        if ( !ext_la_matrix_is_positive( _matrix ) )
        {
            ERR( "correlation '" + _name + "' : matrix is not symmetric positive-definite" );
        }
    }
    else
    {
        ERR( "matrix size is invalid" );
    }
}

//! store the pillar matrices of a term structure (Configure has already filled
//! _pillar_list, aligned by index). Each pillar is validated like a constant
//! matrix — square, all the same size, symmetric positive-definite — and pillar 0
//! is duplicated into _matrix so every t = 0 read (GetValue, the scalar Cholesky)
//! works unchanged. Piecewise-linear interpolation then keeps every interpolated
//! matrix valid: a convex combination of correlation matrices is one.
void Correlation::SetTermMatrices( vector<LaMatrix> MatrixList )
{
    if ( MatrixList.size() != _pillar_list.size() )
    {
        ERR( "correlation '" + _name + "' : one matrix per maturity pillar is required" );
    }
    const size_t n = MatrixList.front()->size1;
    for ( size_t p = 0; p < MatrixList.size(); p++ )
    {
        const la_matrix* m = MatrixList[p];
        if ( m->size1 != n || m->size2 != n )
        {
            ERR( "correlation '" + _name + "' : pillar matrices must all have the same size" );
        }
        if ( !ext_la_matrix_is_positive( m ) )
        {
            ERR( "correlation '" + _name + "' : pillar matrix " + std::to_string( p ) +
                 " is not symmetric positive-definite" );
        }
    }
    _matrix_list = std::move( MatrixList );

    //! _matrix <- copy of pillar 0, the t = 0 view served by the scalar reads
    LaMatrix first = la_matrix_alloc( n, n );
    for ( size_t i = 0; i < n; i++ )
    {
        for ( size_t j = 0; j < n; j++ )
        {
            la_matrix_set( first, i, j, la_matrix_get( _matrix_list.front(), i, j ) );
        }
    }
    _matrix = first.release();

    //! invalidate every cached factor (scalar and per-schedule)
    _cholesky_key.clear();
    _cholesky_schedule.clear();
    _cholesky_schedule_t.clear();
    _cholesky_schedule_key.clear();
}

//! reject a term-varying "<name>_var" (spot/variance) or "<ccy>_ir" (Hull-White
//! rate factor) correlation: Heston / LSV resolve the variance entry once into a
//! scalar rho (SetStochasticRho), and the BS+HW closed form integrates a constant
//! equity/rate rho — a pillar dependence would silently desynchronise the
//! engines, so fail fast at load.
void Correlation::ValidateTermVarEntries()
{
    static const vector<string> suffixes = { "_var", "_ir" };
    auto has_suffix = []( const string& name )
    {
        for ( const string& sfx : suffixes )
        {
            if ( name.size() >= sfx.size() &&
                 name.compare( name.size() - sfx.size(), sfx.size(), sfx ) == 0 )
            {
                return true;
            }
        }
        return false;
    };
    for ( size_t u = 0; u < _underlying_list.size(); u++ )
    {
        const string& name = _underlying_list[u];
        if ( !has_suffix( name ) )
        {
            continue;
        }
        //! row u pairs a variance factor: every entry must be pillar-independent
        const size_t n = _matrix_list.front()->size1;
        for ( size_t j = 0; j < n; j++ )
        {
            const double v0 = la_matrix_get( _matrix_list.front(), u, j );
            for ( size_t p = 1; p < _matrix_list.size(); p++ )
            {
                if ( la_matrix_get( _matrix_list[p], u, j ) != v0 )
                {
                    ERR( "correlation '" + _name + "' : spot/variance entries of '" + name +
                         "' must be identical across maturities (the stochastic-vol engines "
                         "read a single scalar rho)" );
                }
            }
        }
    }
}

//! setter: the YAML supplies only the lower triangle, row-major and including the
//! diagonal — (0,0),(1,0),(1,1),(2,0),(2,1),(2,2)… — so its length is a triangular
//! number n(n+1)/2 for an (n x n) matrix. Mirror it into a full row-major vector and
//! reuse SetMatrix so the same reshape + symmetric-positive-definite validation applies.
void Correlation::SetSymmetricMatrix( la_vector* SymmetricMatrix )
{
    LaVector tri( SymmetricMatrix ); //!< adopt -> freed on return
    const size_t len = tri->size;
    //! invert len = n(n+1)/2  ->  n = (-1 + sqrt(1 + 8*len)) / 2
    const double nd = ( -1.0 + sqrt( 1.0 + 8.0 * (double)len ) ) / 2.0;
    const size_t n = (size_t)( nd + 0.5 ); //!< round to nearest
    if ( n * ( n + 1 ) / 2 != len )
    {
        ERR( "correlation '" + _name +
             "' : symmetric_matrix length is not a triangular number n(n+1)/2" );
    }
    //! expand the lower triangle into a full n x n row-major vector (mirror j<->i)
    LaVector full( la_vector_alloc( n * n ) );
    size_t k = 0;
    for ( size_t i = 0; i < n; ++i )
    {
        for ( size_t j = 0; j <= i; ++j )
        {
            const double v = la_vector_get( tri, k++ );
            la_vector_set( full, i * n + j, v );
            la_vector_set( full, j * n + i, v );
        }
    }
    SetMatrix( std::move( full ) ); //!< reshape + positive-definite check + cache reset
}

//! setter for the FX legs of the matrix. They must form a *pivot basis*: every
//! pair shares the same underlying (pivot) currency, e.g. (EUR_USD, EUR_JPY) with
//! pivot EUR. The pivot is taken from the first pair; the loop rejects any pair
//! anchored on a different currency, since the triangle algebra in GetValue relies
//! on a single common pivot.
void Correlation::SetForexList( const vector<Forex*>& ForexList )
{
    //! set attributes
    _forex_list = ForexList;
    _pivot_currency = ( *( _forex_list.begin() ) )->GetUnderlyingCurrency()->GetName();

    //! must be a basis, ex: ( EUR_USD, EUR_JPY )
    vector<Forex*>::iterator e;
    for ( e = _forex_list.begin(); e != _forex_list.end(); e++ )
    {
        if ( _pivot_currency != ( *e )->GetUnderlyingCurrency()->GetName() )
        {
            ERR( "_forex_list is not a base" );
        }
    }
    SetSingleList(); //!< rebuild the combined underlying+FX index used by GetValue
}

//! setter
void Correlation::SetUnderlyingList( const vector<string>& UnderlyingList )
{
    _underlying_list = UnderlyingList;
    SetSingleList();
}

//! getter: locate the Forex with this exact (underlying, base) currency pair, or
//! nullptr if absent. The triangle helpers below pass nullptr through as a 0
//! contribution, so a missing pivot leg degrades gracefully.
Forex* Correlation::GetForex( const string& UnderlyingCurrency,
                              const string& BaseCurrency )
{

    vector<Forex*>::iterator e;
    for ( e = _forex_list.begin();
          e != _forex_list.end();
          e++ )
    {
        if ( ( *e )->GetUnderlyingCurrency()->GetName() == UnderlyingCurrency &&
             ( *e )->GetBaseCurrency()->GetName() == BaseCurrency )
        {
            return *e;
        }
    }

    return nullptr;
}

//! the set of pivot legs needed to express the I/J cross rate: A_I and A_J (A =
//! pivot). Either may be absent (e.g. when I or J *is* the pivot), so each is
//! inserted only if found.
ForexSet Correlation::GetForexSet( const string& I,
                                   const string& J )
{
    ForexSet s;

    //! pivot
    string A = _pivot_currency;
    Forex* AI = GetForex( A, I );
    Forex* AJ = GetForex( A, J );
    if ( AI )
        s.insert( AI );
    if ( AJ )
        s.insert( AJ );

    return s;
}

//! build the dense correlation sub-matrix over (underlyings, then FX pairs), in
//! that block order. Owned by the caller (RAII LaMatrix). Each off-diagonal entry
//! is filled symmetrically via the appropriate window-averaged formula (eq/eq,
//! fx/eq, fx/fx — the cross-FX ones going through the pivot triangle), the
//! diagonal is 1. The (0,0) window reproduces today's entries exactly (and any
//! window on a constant matrix). Used by the Cholesky factorisations (t = 0 and
//! per-step) and the basket moment matching (term view).
LaMatrix Correlation::ExtractMatrix( const vector<string>& UnderlyingNameList,
                                     const vector<Forex*>& ForexList,
                                     double a,
                                     double b )
{

    //! sizes
    size_t fx_size = ForexList.size();
    size_t ud_size = UnderlyingNameList.size();

    //! a correlation needs at least two singles to be meaningful
    if ( fx_size + ud_size < 2 )
    {
        ERR( "Can't extract only one single from correlation matrix" );
    }

    //! matrix to fill: layout = [ underlyings | forex ] on both axes
    LaMatrix mat = la_matrix_alloc( fx_size + ud_size, fx_size + ud_size );

    //! eq / eq block: direct pairwise correlations between equity underlyings
    if ( ud_size > 1 )
    {
        for ( size_t i = 0;
              i < ud_size - 1;
              i++ )
        {
            string u = UnderlyingNameList[i];
            for ( size_t j = i + 1;
                  j < ud_size;
                  j++ )
            {
                string v = UnderlyingNameList[j];
                double x = AvgEntryByName( u, v, a, b );
                la_matrix_set( mat, i, j, x );
                la_matrix_set( mat, j, i, x );
            }
        }
    }

    //! fx / eq block: correlation of each FX pair (resolved through its pivot) with
    //! each equity underlying; written at column offset ud_size (the FX block)
    if ( fx_size > 0 && ud_size > 0 )
    {
        for ( size_t i = 0;
              i < ud_size;
              i++ )
        {
            string v = UnderlyingNameList[i];
            for ( size_t j = 0;
                  j < fx_size;
                  j++ )
            {
                Forex* e = ForexList[j];
                double x = AvgValue( e->GetUnderlyingCurrency()->GetName(),
                                     e->GetBaseCurrency()->GetName(),
                                     v,
                                     a,
                                     b );
                la_matrix_set( mat, i, j + ud_size, x );
                la_matrix_set( mat, j + ud_size, i, x );
            }
        }
    }

    //! fx / fx block: cross-correlation between two FX pairs, derived from the four
    //! pivot legs via the 4-argument GetValue triangle formula
    if ( fx_size > 1 )
    {
        for ( size_t i = 0; i < fx_size - 1; i++ )
        {
            Forex* e1 = ForexList[i];
            for ( size_t j = i + 1; j < fx_size; j++ )
            {
                Forex* e2 = ForexList[j];
                double x = AvgValue( e1->GetUnderlyingCurrency()->GetName(),
                                     e1->GetBaseCurrency()->GetName(),
                                     e2->GetUnderlyingCurrency()->GetName(),
                                     e2->GetBaseCurrency()->GetName(),
                                     a,
                                     b );

                //! FX block lives at offset ud_size on BOTH axes (was mis-indexed
                //! at (i, j + fx_size): out of bounds / wrong cell with >= 2 pairs)
                la_matrix_set( mat, ud_size + i, ud_size + j, x );
                la_matrix_set( mat, ud_size + j, ud_size + i, x );
            }
        }
    }

    //! diagonal: a single is perfectly correlated with itself
    for ( size_t i = 0;
          i < fx_size + ud_size;
          i++ )
    {
        la_matrix_set( mat, i, i, 1 );
    }

    // ext_la_matrix_log( mat );

    return mat;
}

//! today's dense sub-matrix (the historic signature): the degenerate (0,0) window
LaMatrix Correlation::ExtractMatrix( const vector<string>& UnderlyingNameList,
                                     const vector<Forex*>& ForexList )
{
    return ExtractMatrix( UnderlyingNameList, ForexList, 0, 0 );
}

//! term view for the basket moment matching: each entry averaged over [0, T],
//! matching the components' TOTAL vols sigma*sqrt(T) the moments are built on
LaMatrix Correlation::ExtractTermMatrix( const vector<string>& UnderlyingNameList,
                                         const vector<Forex*>& ForexList,
                                         double T )
{
    return ExtractMatrix( UnderlyingNameList, ForexList, 0, T );
}

//! true when a `maturities` pillar list makes the matrix time-dependent.
bool Correlation::IsTermStructured() const
{
    return _pillar_list.size() > 1;
}

//! integral_0^t of entry (i, j): the entry is piecewise linear in the pillars —
//! flat at the first pillar's value before it, linear between pillars, flat at the
//! last pillar's value after it — so the primitive accumulates rectangle and
//! trapeze areas segment by segment. Exact (no quadrature).
double Correlation::EntryPrimitive( size_t i, size_t j, double t ) const
{
    const vector<double>& m = _pillar_list;
    auto entry = [&]( size_t p )
    { return la_matrix_get( _matrix_list[p], i, j ); };

    if ( t <= m.front() ) //!< flat before the first pillar
    {
        return entry( 0 ) * t;
    }
    double F = entry( 0 ) * m.front();
    for ( size_t p = 1; p < m.size(); p++ )
    {
        if ( t <= m[p] ) //!< partial trapeze up to t inside [m[p-1], m[p]]
        {
            const double w = ( t - m[p - 1] ) / ( m[p] - m[p - 1] );
            const double e_t = entry( p - 1 ) + w * ( entry( p ) - entry( p - 1 ) );
            return F + ( entry( p - 1 ) + e_t ) / 2 * ( t - m[p - 1] );
        }
        F += ( entry( p - 1 ) + entry( p ) ) / 2 * ( m[p] - m[p - 1] );
    }
    return F + entry( m.size() - 1 ) * ( t - m.back() ); //!< flat after the last pillar
}

//! average of entry (i, j) over the window [a, b] (year fractions); a degenerate
//! window returns the instantaneous value at a. Constant matrices short-circuit to
//! the plain entry, so every legacy call path is bit-for-bit unchanged.
double Correlation::AvgEntry( size_t i, size_t j, double a, double b ) const
{
    if ( !IsTermStructured() )
    {
        return la_matrix_get( _matrix, i, j );
    }
    a = std::max( a, 0.0 );
    b = std::max( b, a );
    if ( b - a < 1e-12 ) //!< degenerate window -> instantaneous value at a
    {
        const vector<double>& m = _pillar_list;
        if ( a <= m.front() )
        {
            return la_matrix_get( _matrix_list.front(), i, j );
        }
        if ( a >= m.back() )
        {
            return la_matrix_get( _matrix_list.back(), i, j );
        }
        for ( size_t p = 1; p < m.size(); p++ )
        {
            if ( a <= m[p] )
            {
                const double w = ( a - m[p - 1] ) / ( m[p] - m[p - 1] );
                return la_matrix_get( _matrix_list[p - 1], i, j ) +
                       w * ( la_matrix_get( _matrix_list[p], i, j ) -
                             la_matrix_get( _matrix_list[p - 1], i, j ) );
            }
        }
    }
    return ( EntryPrimitive( i, j, b ) - EntryPrimitive( i, j, a ) ) / ( b - a );
}

//! named lookup of the window-averaged entry (positions in the _single_list index)
double Correlation::AvgEntryByName( const string& u1, const string& u2,
                                    double a, double b )
{
    size_t i = LookAtPosition( _single_list, u1 );
    size_t j = LookAtPosition( _single_list, u2 );
    return AvgEntry( i, j, a, b );
}

//! direct lookup: correlation between two named singles (underlying or FX-pair
//! name) by their position in the combined _single_list index. Today's (t = 0)
//! instantaneous value; horizon-aware callers use GetTermValue.
double Correlation::GetValue( const string& u1,
                              const string& u2 )
{
    size_t i = LookAtPosition( _single_list, u1 );
    size_t j = LookAtPosition( _single_list, u2 );
    return la_matrix_get( _matrix, i, j );
}

//! correlation between two named singles averaged over [0, T]
double Correlation::GetTermValue( const string& u1,
                                  const string& u2,
                                  double T )
{
    return AvgEntryByName( u1, u2, 0, T );
}

//! correlation between the cross-FX rate I/J and an underlying S, derived through
//! the pivot A. Writing the I/J log-return as a combination of the pivot legs
//! r_{I/J} = r_{A/J} - r_{A/I} (so I/J = A_J / A_I), its covariance with S gives
//!   corr(I/J, S) = ( -rho(A_I,S) vol_{A_I} + rho(A_J,S) vol_{A_J} ) / vol_{I/J},
//! where vol_{I/J} = sqrt(vol_AI^2 + vol_AJ^2 - 2 rho(A_I,A_J) vol_AI vol_AJ) is
//! the implied vol of the cross rate. Missing pivot legs contribute 0.
//!
//! Window semantics: the rho entries are averaged over [a, b] ((0,0) = today's
//! values). Because the FX vols are constant, the PRODUCT corr(I/J,S)*vol_IJ is
//! linear in the instantaneous rho entries, so its consumers (the quanto drift
//! integrals) get the exact window average — the vol_IJ normalisations cancel.
double Correlation::AvgValue( const string& I,
                              const string& J,
                              const string& S,
                              double a,
                              double b )
{

    //! pivot
    string A = _pivot_currency;

    //! distinct currency check
    if ( I == J )
    {
        ERR( "correlation extraction failed, base_currency = underlying_currency" );
    }

    //! introduce pivot legs A_I and A_J (the basis used to build I/J)
    Forex* AI = GetForex( A, I );
    Forex* AJ = GetForex( A, J );

    double rho_AI_S = ( AI ? AvgEntryByName( AI->GetName(), S, a, b ) : 0 );                    //!< corr(A_I, S)
    double rho_AJ_S = ( AJ ? AvgEntryByName( AJ->GetName(), S, a, b ) : 0 );                    //!< corr(A_J, S)
    double rho_AI_AJ = ( AI && AJ ? AvgEntryByName( AI->GetName(), AJ->GetName(), a, b ) : 0 ); //!< corr(A_I, A_J)
    double vol_AI = ( AI ? AI->GetConstantVol() : 0 );
    double vol_AJ = ( AJ ? AJ->GetConstantVol() : 0 );
    //! implied vol of the cross rate I/J = A_J/A_I (variance of a difference)
    double vol_IJ = sqrt( vol_AI * vol_AI + vol_AJ * vol_AJ - 2 * rho_AI_AJ * vol_AI * vol_AJ );

    return ( -rho_AI_S * vol_AI + rho_AJ_S * vol_AJ ) / vol_IJ;
}

//! today's (t = 0) instantaneous (cross-FX, underlying) correlation
double Correlation::GetValue( const string& I,
                              const string& J,
                              const string& S )
{
    return AvgValue( I, J, S, 0, 0 );
}

//! (cross-FX, underlying) correlation averaged over [0, T]
double Correlation::GetTermValue( const string& I,
                                  const string& J,
                                  const string& S,
                                  double T )
{
    return AvgValue( I, J, S, 0, T );
}

//! (cross-FX, underlying) correlation averaged over the forward window [t1, t2]
//! (the PDE's per-step quanto carry)
double Correlation::GetStepValue( const string& I,
                                  const string& J,
                                  const string& S,
                                  double t1,
                                  double t2 )
{
    return AvgValue( I, J, S, t1, t2 );
}

//! correlation between two cross-FX rates I/J and M/N, both reduced to pivot legs.
//! With r_{I/J} = r_{A/J} - r_{A/I} and r_{M/N} = r_{A/N} - r_{A/M}, expanding the
//! bilinear covariance yields the four signed pivot-leg cross terms below, divided
//! by the two cross-rate vols vol_{I/J} and vol_{M/N}. Window semantics as in the
//! 3-argument AvgValue: entries averaged over [a, b], products exact by linearity.
double Correlation::AvgValue( const string& I,
                              const string& J,
                              const string& M,
                              const string& N,
                              double a,
                              double b )
{
    //! pivot
    string A = _pivot_currency;

    //! distinct currency check
    if ( I == J || M == N )
    {
        ERR( "correlation extraction failed, base_currency = underlying_currency" );
    }

    //! introduce the four pivot legs for the two cross rates
    Forex* AI = GetForex( A, I );
    Forex* AJ = GetForex( A, J );
    Forex* AM = GetForex( A, M );
    Forex* AN = GetForex( A, N );

    double rho_AI_AM = ( AI && AM ? AvgEntryByName( AI->GetName(), AM->GetName(), a, b ) : 0 );
    double rho_AI_AN = ( AI && AN ? AvgEntryByName( AI->GetName(), AN->GetName(), a, b ) : 0 );
    double rho_AJ_AM = ( AJ && AM ? AvgEntryByName( AJ->GetName(), AM->GetName(), a, b ) : 0 );
    double rho_AJ_AN = ( AJ && AN ? AvgEntryByName( AJ->GetName(), AN->GetName(), a, b ) : 0 );
    double rho_AI_AJ = ( AI && AJ ? AvgEntryByName( AI->GetName(), AJ->GetName(), a, b ) : 0 );
    double rho_AM_AN = ( AM && AN ? AvgEntryByName( AM->GetName(), AN->GetName(), a, b ) : 0 );
    double vol_AI = ( AI ? AI->GetConstantVol() : 0 );
    double vol_AJ = ( AJ ? AJ->GetConstantVol() : 0 );
    double vol_AM = ( AM ? AM->GetConstantVol() : 0 );
    double vol_AN = ( AN ? AN->GetConstantVol() : 0 );
    //! cross-rate vols (variance of a difference of pivot legs), as in the 3-arg case
    double vol_IJ = sqrt( vol_AI * vol_AI + vol_AJ * vol_AJ - 2 * rho_AI_AJ * vol_AI * vol_AJ );
    double vol_MN = sqrt( vol_AM * vol_AM + vol_AN * vol_AN - 2 * rho_AM_AN * vol_AM * vol_AN );

    //! signs follow from r_{I/J} = r_{A/J} - r_{A/I}, r_{M/N} = r_{A/N} - r_{A/M}:
    //! (+,-,-,+) for the (I&M, I&N, J&M, J&N) cross terms respectively
    return ( +rho_AI_AM * vol_AI * vol_AM +
             -rho_AI_AN * vol_AI * vol_AN +
             -rho_AJ_AM * vol_AJ * vol_AM +
             +rho_AJ_AN * vol_AJ * vol_AN ) /
           vol_IJ / vol_MN;
}

//! today's (t = 0) instantaneous (cross-FX, cross-FX) correlation
double Correlation::GetValue( const string& I,
                              const string& J,
                              const string& M,
                              const string& N )
{
    return AvgValue( I, J, M, N, 0, 0 );
}

//! (cross-FX, cross-FX) correlation averaged over [0, T]
double Correlation::GetTermValue( const string& I,
                                  const string& J,
                                  const string& M,
                                  const string& N,
                                  double T )
{
    return AvgValue( I, J, M, N, 0, T );
}

//! entry (i, j) of the lower-triangular Cholesky factor L (rows = singles in
//! _cholesky_single_list order). L is lower-triangular by construction, so any
//! above-diagonal request (j > i) is exactly 0 and short-circuits the lookup.
//! The MC engine reads these to weight independent normals into correlated ones.
double Correlation::GetCholeskyValue( const string& u1,
                                      const string& u2 )
{
    size_t i = LookAtPosition( _cholesky_single_list, u1 );
    size_t j = LookAtPosition( _cholesky_single_list, u2 );
    return ( j > i ) ? 0 : la_matrix_get( _cholesky_matrix, i, j );
}

//!
void Correlation::ComputeCholeskyMatrix( const vector<string>& SingleNameList )
{
    //! cache: the factor depends only on the requested sub-set and the correlation
    //! matrix (immutable during a run — there is no correlation bump), so skip the
    //! rebuild when the sub-set is unchanged. Bump-and-revalue Greeks call this once
    //! per reprice with the same sub-set; it only recomputes when the sub-set changes
    //! (e.g. another book in a !sequence) or the matrix is reloaded (SetMatrix).
    string key;
    for ( const string& s : SingleNameList )
    {
        key += s;
        key += '\n';
    }
    if ( _cholesky_matrix && key == _cholesky_key )
    {
        return;
    }

    //! rebuilt from scratch: bump-and-revalue Greeks (and any other re-pricing) call
    //! this repeatedly, so the lists must be cleared or they accumulate duplicate
    //! rows and the extracted matrix is no longer SDP
    _cholesky_underlying_list.clear();
    _cholesky_forex_list.clear();
    _cholesky_single_list.clear();

    // cholesky attributes
    for ( const string& s : SingleNameList )
    {
        //! _underlying_list
        for ( const string& u : _underlying_list )
        {
            if ( u == s )
            {
                _cholesky_underlying_list.push_back( u );
            }
        }

        //! _cholesky_forex_list
        for ( Forex* e : _forex_list )
        {
            if ( e->GetName() == s )
            {
                _cholesky_forex_list.push_back( e );
            }
        }
    }

    //! _cholesky_matrix
    _cholesky_matrix = ExtractMatrix( _cholesky_underlying_list,
                                      _cholesky_forex_list );
    SetCholeskySingleList();

    //! positiveness test
    if ( !ext_la_matrix_is_positive( _cholesky_matrix ) )
    {
        ERR( _name + " is not SDP" );
    }

    //! factorise in place (already verified SPD just above, so this should not
    //! fail; check defensively rather than silently proceeding on a bad factor)
    if ( !CholeskyDecomposeLower( _cholesky_matrix ) )
    {
        ERR( _name + " : Cholesky factorisation failed (matrix not positive-definite)" );
    }
    _cholesky_key = key; //!< mark the sub-set this factor is now valid for
}

//! term structure: build one Cholesky factor per diffusion date, of the matrix
//! averaged over the step [t_{i-1}, t_i] (index 0 gets today's instantaneous
//! matrix; its factor multiplies the zero-length first increment, so it is never
//! consumed). Correlating the i-th Gaussian increments with the i-th factor
//! reproduces the exact integrated covariance of the piecewise-linear structure,
//! wherever the diffusion dates fall relative to the pillars. Requires a prior
//! ComputeCholeskyMatrix (it fixes the sub-set and its ordering); cached against
//! both the schedule and that sub-set, so bump-and-revalue Greeks reuse it.
void Correlation::EnsureCholeskySchedule( const vector<double>& TList )
{
    if ( !IsTermStructured() )
    {
        ERR( "correlation '" + _name + "' : EnsureCholeskySchedule on a constant matrix" );
    }
    if ( _cholesky_single_list.empty() )
    {
        ERR( "correlation '" + _name + "' : ComputeCholeskyMatrix must run before the schedule" );
    }
    if ( !_cholesky_schedule.empty() && _cholesky_schedule_key == _cholesky_key &&
         _cholesky_schedule_t == TList )
    {
        return;
    }

    _cholesky_schedule.clear();
    _cholesky_schedule.reserve( TList.size() );
    for ( size_t i = 0; i < TList.size(); i++ )
    {
        const double t1 = ( i == 0 ) ? TList[0] : TList[i - 1];
        const double t2 = TList[i];
        LaMatrix step = ExtractMatrix( _cholesky_underlying_list, _cholesky_forex_list, t1, t2 );
        //! a convex combination of valid correlation matrices is valid, but check
        //! defensively (numerical near-degeneracy) rather than diffuse a bad factor
        if ( !ext_la_matrix_is_positive( step ) || !CholeskyDecomposeLower( step ) )
        {
            ERR( "correlation '" + _name + "' : step-average matrix at index " +
                 std::to_string( i ) + " is not positive-definite" );
        }
        _cholesky_schedule.push_back( std::move( step ) );
    }
    _cholesky_schedule_t = TList;
    _cholesky_schedule_key = _cholesky_key;
}

//! entry (i, j) of the per-date lower-triangular factor (see GetCholeskyValue).
double Correlation::GetCholeskyValue( const string& u1,
                                      const string& u2,
                                      size_t DateIndex )
{
    if ( DateIndex >= _cholesky_schedule.size() )
    {
        ERR( "correlation '" + _name + "' : Cholesky schedule not built for this date" );
    }
    size_t i = LookAtPosition( _cholesky_single_list, u1 );
    size_t j = LookAtPosition( _cholesky_single_list, u2 );
    return ( j > i ) ? 0 : la_matrix_get( _cholesky_schedule[DateIndex], i, j );
}

//! false only when L(u1, u2) is structurally zero at every date. For a constant
//! matrix that is the scalar factor's zero test (the historic node-skipping rule);
//! for a term structure the factor varies by date, so keep every lower-triangle
//! pair wired (an above-diagonal pair stays exactly 0 and is skipped).
bool Correlation::CholeskyMayBeNonZero( const string& u1,
                                        const string& u2 )
{
    if ( !IsTermStructured() )
    {
        return GetCholeskyValue( u1, u2 ) != 0;
    }
    size_t i = LookAtPosition( _cholesky_single_list, u1 );
    size_t j = LookAtPosition( _cholesky_single_list, u2 );
    return j <= i;
}

//! rebuild the full index _single_list = [ underlyings..., forex names... ],
//! matching the row/column order ExtractMatrix and GetValue assume.
void Correlation::SetSingleList()
{

    //! reset _single_list
    _single_list.clear();

    //! mono_list = udls + ...
    vector<string>::iterator u;
    for ( u = _underlying_list.begin();
          u != _underlying_list.end();
          u++ )
    {
        _single_list.push_back( ( *u ) );
    }

    //! ... + fx
    vector<Forex*>::iterator v;
    for ( v = _forex_list.begin();
          v != _forex_list.end();
          v++ )
    {
        _single_list.push_back( ( *v )->GetName() );
    }
}

//! rebuild the sub-set index for the Cholesky factor: same [ underlyings, forex ]
//! order as SetSingleList but restricted to the singles in the current sub-matrix,
//! so GetCholeskyValue can map a name to its row/column in _cholesky_matrix.
void Correlation::SetCholeskySingleList()
{

    //! mono_list = udls
    vector<string>::iterator u;
    for ( u = _cholesky_underlying_list.begin();
          u != _cholesky_underlying_list.end();
          u++ )
    {
        _cholesky_single_list.push_back( *u );
    }

    //! + fx
    vector<Forex*>::iterator v;
    for ( v = _cholesky_forex_list.begin();
          v != _cholesky_forex_list.end();
          v++ )
    {
        _cholesky_single_list.push_back( ( *v )->GetName() );
    }
}

//! spot of I/J. If the pair is quoted the other way (J/I), invert it; a vol on the
//! other hand is quote-direction-invariant (see GetFxVol).
double Correlation::GetFxSpot( const string& I,
                               const string& J )
{
    Forex* IJ = GetForex( I, J );
    Forex* JI = GetForex( J, I );
    if ( IJ )
    {
        return IJ->GetSpot();
    }
    else if ( JI )
    {
        return 1 / JI->GetSpot(); //!< inverse quote: spot(I/J) = 1 / spot(J/I)
    }
    else
    {
        ERR( I + "/" + J + " is not defined" );
    }
}

//! vol of I/J. Unlike the spot, the lognormal vol of a rate equals that of its
//! reciprocal (log(1/x) = -log(x) has the same variance), so no inversion is needed.
double Correlation::GetFxVol( const string& I,
                              const string& J )
{
    Forex* IJ = GetForex( I, J );
    Forex* JI = GetForex( J, I );
    if ( IJ )
    {
        return IJ->GetConstantVol();
    }
    else if ( JI )
    {
        return JI->GetConstantVol(); //!< vol(I/J) == vol(J/I)
    }
    else
    {
        ERR( I + "/" + J + " is not defined" );
    }
}

//! the FX pairs (pivot legs) implied by a set of currencies: for each non-pivot
//! currency C return the pair pivot/C. Empty when there is no pivot or fewer than
//! two currencies (no FX exposure to correlate).
set<string> Correlation::GetForexNameList( const set<string>& currency_name_list )
{
    set<string> s;
    if ( currency_name_list.size() > 1 )
    {
        if ( _pivot_currency != "" )
        {
            set<string>::const_iterator c;
            for ( c = currency_name_list.begin();
                  c != currency_name_list.end();
                  c++ )
            {

                if ( *c != _pivot_currency )
                {
                    Forex* E = GetForex( _pivot_currency, *c );
                    s.insert( E->GetName() );
                }
            }
        }
    }
    return s;
}

//! index of name u within the given list, translating a not-found into a clear
//! "missing from correlation matrix" error (VectorPosition throws on absence).
size_t Correlation::LookAtPosition( const vector<string>& UnderlyingList,
                                    const string& u ) const
{

    size_t i;
    try
    {
        i = VectorPosition( UnderlyingList, u );
    }
    catch ( ... )
    {
        ERR( u + " is missing from correlation matrix " + _name );
    }
    return i;
}

//! MC node carrying the (cross-FX, underlying) correlation; keyed so the
//! collector mutualises it across the tree. Constant matrix: a ConstantNode.
//! Term structure: a per-date node carrying the RUNNING AVERAGE rho_bar(t_i) —
//! its consumer (QuantoAdjustmentNode and the composite algebra) applies the
//! whole-horizon factor exp(-sigma_S sigma_FX rho * t), so the average makes that
//! the exact integral exp(-sigma_S sigma_FX int_0^t rho(u) du).
MonteCarloNode* Correlation::GetCorrelNode( NodeCollector& NC,
                                            const string& UnderlyingCurrency,
                                            const string& BaseCurrency,
                                            const string& Underlying )
{
    const string name =
        UnderlyingCurrency + "_" + BaseCurrency + node_name::SEP + Underlying + node_name::CORREL;
    if ( !IsTermStructured() )
    {
        return NC.GetOrCreate<ConstantNode>(
            name,
            [&]( ConstantNode* C )
            { C->SetConstantValue( GetValue( UnderlyingCurrency, BaseCurrency, Underlying ) ); } );
    }
    return NC.GetOrCreate<TermCorrelNode>(
        name,
        [&]( TermCorrelNode* N )
        {
            N->SetEvaluator(
                [this, UnderlyingCurrency, BaseCurrency, Underlying]( size_t /*idx*/, double t )
                { return GetTermValue( UnderlyingCurrency, BaseCurrency, Underlying, t ); } );
        } );
}

//! MC node for the FX spot (in log space) of UnderlyingCurrency/BaseCurrency,
//! built from the pivot legs. In log-returns an arbitrary rate factors through the
//! pivot I as AB = AI + IB; here one of the two currencies *is* the pivot, so only
//! a single pivot leg is needed (the general non-pivot/non-pivot case is TODO).
//! Memoised under node_name so repeated requests share the node.
MonteCarloNode* Correlation::GetFxNode( NodeCollector& NC,
                                        const string& UnderlyingCurrency,
                                        const string& BaseCurrency )
{
    MonteCarloNode* N;
    string node_name = UnderlyingCurrency + "_" + BaseCurrency + node_name::QUANTO_PREFIX + BaseCurrency + node_name::SPOT;
    if ( !( N = NC.GetNode( node_name ) ) )
    {
        // AB = AI * IB  (log space: log AB = log AI + log IB)

        // underlying currency is the pivot I: AB = IB directly, reuse its node
        if ( UnderlyingCurrency == _pivot_currency )
        {

            // IB
            Forex* IB = GetForex( _pivot_currency, BaseCurrency );
            MonteCarloNode* IB_Node = IB->Single::GetNode( NC );
            N = IB_Node;
        }

        // base currency is the pivot I: AB = AI = 1/IB, i.e. negate IB in log space
        else if ( BaseCurrency == _pivot_currency )
        {
            // IB
            Forex* IB = GetForex( _pivot_currency, UnderlyingCurrency );
            MonteCarloNode* IB_Node = IB->Single::GetNode( NC );

            // AI = -IB  (reciprocal rate -> sign flip on the log-return node)
            ProductNode* AI_node = NC.NewNode<ProductNode>( node_name );
            AI_node->PushNode( IB_Node, -1 );
            N = AI_node;
        }

        // neither leg is the pivot: needs AI + IB, not yet implemented
        else
        {
            ERR( " error " );
        }
    }
    return N;
}

//! MC node for the correlation between two cross-FX rates (the 4-arg read);
//! keyed on both pairs so it is shared across the tree. Constant matrix: a
//! ConstantNode; term structure: the running average rho_bar(t_i) (same
//! whole-horizon-consumer reasoning as the 3-argument GetCorrelNode).
MonteCarloNode* Correlation::GetCorrelNode( NodeCollector& NC,
                                            const string& UnderlyingCurrency1,
                                            const string& BaseCurrency1,
                                            const string& UnderlyingCurrency2,
                                            const string& BaseCurrency2 )
{
    const string name = UnderlyingCurrency1 + "_" + BaseCurrency1 + node_name::SEP +
                        UnderlyingCurrency2 + "_" + BaseCurrency2 + node_name::CORREL;
    if ( !IsTermStructured() )
    {
        return NC.GetOrCreate<ConstantNode>(
            name,
            [&]( ConstantNode* C )
            {
                C->SetConstantValue( GetValue( UnderlyingCurrency1,
                                               BaseCurrency1,
                                               UnderlyingCurrency2,
                                               BaseCurrency2 ) );
            } );
    }
    return NC.GetOrCreate<TermCorrelNode>(
        name,
        [&]( TermCorrelNode* N )
        {
            N->SetEvaluator(
                [this, UnderlyingCurrency1, BaseCurrency1, UnderlyingCurrency2,
                 BaseCurrency2]( size_t /*idx*/, double t )
                {
                    return GetTermValue( UnderlyingCurrency1,
                                         BaseCurrency1,
                                         UnderlyingCurrency2,
                                         BaseCurrency2,
                                         t );
                } );
        } );
}

//! MC node carrying one entry L(u1, u2) of the Cholesky factor; the engine
//! combines these with independent normals to produce correlated noise. Assumes
//! ComputeCholeskyMatrix has already been called. Constant matrix: the cached
//! scalar factor as a ConstantNode. Term structure: a per-date node reading the
//! per-step factors — the prepare hook builds the whole factor schedule once
//! (shared across every entry node) when the first node fills.
MonteCarloNode* Correlation::GetCholeskyNode( NodeCollector& NC,
                                              const string& Underlying1,
                                              const string& Underlying2 )
{
    const string name = Underlying1 + node_name::SEP + Underlying2 + node_name::CHOLESKY;
    if ( !IsTermStructured() )
    {
        return NC.GetOrCreate<ConstantNode>(
            name,
            [&]( ConstantNode* C )
            { C->SetConstantValue( GetCholeskyValue( Underlying1, Underlying2 ) ); } );
    }
    return NC.GetOrCreate<TermCorrelNode>(
        name,
        [&]( TermCorrelNode* N )
        {
            N->SetPrepare( [this]( const vector<double>& t_list )
                           { EnsureCholeskySchedule( t_list ); } );
            N->SetEvaluator( [this, Underlying1, Underlying2]( size_t idx, double /*t*/ )
                             { return GetCholeskyValue( Underlying1, Underlying2, idx ); } );
        } );
}

//! MC node carrying the constant FX vol of UnderlyingCurrency/BaseCurrency
MonteCarloNode* Correlation::GetFxVolNode( NodeCollector& NC,
                                           const string& UnderlyingCurrency,
                                           const string& BaseCurrency )
{
    return NC.GetOrCreate<ConstantNode>(
        UnderlyingCurrency + "_" + BaseCurrency + node_name::VOL,
        [&]( ConstantNode* C )
        { C->SetConstantValue( GetFxVol( UnderlyingCurrency, BaseCurrency ) ); } );
}
