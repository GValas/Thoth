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
#include "correlation.hpp"
#include "object_reader.hpp"

//!
Correlation::Correlation( const string& ObjectName ) : MarketData( ObjectName, KIND_CORRELATION_MATRIX )
{
}

//!
Correlation::~Correlation() = default;

//! read the matrix (full "matrix" or symmetric/lower-triangular "symmetric_matrix"
//! form — the latter not yet supported) and the optional "underlyings" / "forexs"
//! name lists that index its rows and columns.
void Correlation::Configure( ObjectReader& reader )
{
    if ( reader.Has<vector<double>>( "matrix" ) )
    {
        SetMatrix( reader.LaVector( "matrix" ) );
    }
    else if ( reader.Has<vector<double>>( "symmetric_matrix" ) )
    {
        SetSymmetricMatrix( reader.LaVector( "symmetric_matrix" ) );
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
        _matrix = ext_la_vector_to_matrix( Matrix, (size_t)n_sqrt );
        //! Matrix (the source la_vector) is freed by its RAII LaVector at return
        _cholesky_key.clear(); //!< invalidate any cached Cholesky factor

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

//! setter
void Correlation::SetSymmetricMatrix( la_vector* /*SymmetricMatrix*/ )
{
    //! not implemented; fail loudly rather than silently ignoring the matrix
    ERR( "correlation '" + _name + "' : symmetric_matrix is not supported (use matrix)" );
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
//! is filled symmetrically via the appropriate GetValue overload (eq/eq, fx/eq,
//! fx/fx — the cross-FX ones going through the pivot triangle), the diagonal is 1.
//! Used to feed the Cholesky factorisation in ComputeCholeskyMatrix.
LaMatrix Correlation::ExtractMatrix( const vector<string>& UnderlyingNameList,
                                     const vector<Forex*>& ForexList )
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
                double x = GetValue( u, v );
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
                Forex* a = ForexList[j];
                double x = GetValue( a->GetUnderlyingCurrency()->GetName(),
                                     a->GetBaseCurrency()->GetName(),
                                     v );
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
            Forex* a = ForexList[i];
            for ( size_t j = i + 1; j < fx_size; j++ )
            {
                Forex* b = ForexList[j];
                double x = GetValue( ( a )->GetUnderlyingCurrency()->GetName(),
                                     ( a )->GetBaseCurrency()->GetName(),
                                     ( b )->GetUnderlyingCurrency()->GetName(),
                                     ( b )->GetBaseCurrency()->GetName() );

                la_matrix_set( mat, i, j + fx_size, x );
                la_matrix_set( mat, j + fx_size, i, x );
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

//! direct lookup: correlation between two named singles (underlying or FX-pair
//! name) by their position in the combined _single_list index.
double Correlation::GetValue( const string& u1,
                              const string& u2 )
{
    size_t i = LookAtPosition( _single_list, u1 );
    size_t j = LookAtPosition( _single_list, u2 );
    return la_matrix_get( _matrix, i, j );
}

//! correlation between the cross-FX rate I/J and an underlying S, derived through
//! the pivot A. Writing the I/J log-return as a combination of the pivot legs
//! r_{I/J} = r_{A/J} - r_{A/I} (so I/J = A_J / A_I), its covariance with S gives
//!   corr(I/J, S) = ( -rho(A_I,S) vol_{A_I} + rho(A_J,S) vol_{A_J} ) / vol_{I/J},
//! where vol_{I/J} = sqrt(vol_AI^2 + vol_AJ^2 - 2 rho(A_I,A_J) vol_AI vol_AJ) is
//! the implied vol of the cross rate. Missing pivot legs contribute 0.
double Correlation::GetValue( const string& I,
                              const string& J,
                              const string& S )
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

    double rho_AI_S = ( AI ? GetValue( AI->GetName(), S ) : 0 );                    //!< corr(A_I, S)
    double rho_AJ_S = ( AJ ? GetValue( AJ->GetName(), S ) : 0 );                    //!< corr(A_J, S)
    double rho_AI_AJ = ( AI && AJ ? GetValue( AI->GetName(), AJ->GetName() ) : 0 ); //!< corr(A_I, A_J)
    double vol_AI = ( AI ? AI->GetConstantVol() : 0 );
    double vol_AJ = ( AJ ? AJ->GetConstantVol() : 0 );
    //! implied vol of the cross rate I/J = A_J/A_I (variance of a difference)
    double vol_IJ = sqrt( vol_AI * vol_AI + vol_AJ * vol_AJ - 2 * rho_AI_AJ * vol_AI * vol_AJ );

    return ( -rho_AI_S * vol_AI + rho_AJ_S * vol_AJ ) / vol_IJ;
}

//! correlation between two cross-FX rates I/J and M/N, both reduced to pivot legs.
//! With r_{I/J} = r_{A/J} - r_{A/I} and r_{M/N} = r_{A/N} - r_{A/M}, expanding the
//! bilinear covariance yields the four signed pivot-leg cross terms below, divided
//! by the two cross-rate vols vol_{I/J} and vol_{M/N}.
double Correlation::GetValue( const string& I,
                              const string& J,
                              const string& M,
                              const string& N )
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

    double rho_AI_AM = ( AI && AM ? GetValue( AI->GetName(), AM->GetName() ) : 0 );
    double rho_AI_AN = ( AI && AN ? GetValue( AI->GetName(), AN->GetName() ) : 0 );
    double rho_AJ_AM = ( AJ && AM ? GetValue( AJ->GetName(), AM->GetName() ) : 0 );
    double rho_AJ_AN = ( AJ && AN ? GetValue( AJ->GetName(), AN->GetName() ) : 0 );
    double rho_AI_AJ = ( AI && AJ ? GetValue( AI->GetName(), AJ->GetName() ) : 0 );
    double rho_AM_AN = ( AM && AN ? GetValue( AM->GetName(), AN->GetName() ) : 0 );
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

//! MC node carrying the (cross-FX, underlying) correlation as a constant; keyed
//! so the collector mutualises it across the tree.
MonteCarloNode* Correlation::GetCorrelNode( NodeCollector& NC,
                                            const string& UnderlyingCurrency,
                                            const string& BaseCurrency,
                                            const string& Underlying )
{
    return NC.GetOrCreate<ConstantNode>(
        UnderlyingCurrency + "_" + BaseCurrency + "#" + Underlying + node_name::CORREL,
        [&]( ConstantNode* C )
        { C->SetConstantValue( GetValue( UnderlyingCurrency, BaseCurrency, Underlying ) ); } );
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

//! MC node for the correlation between two cross-FX rates (the 4-arg GetValue),
//! as a constant; keyed on both pairs so it is shared across the tree.
MonteCarloNode* Correlation::GetCorrelNode( NodeCollector& NC,
                                            const string& UnderlyingCurrency1,
                                            const string& BaseCurrency1,
                                            const string& UnderlyingCurrency2,
                                            const string& BaseCurrency2 )
{
    return NC.GetOrCreate<ConstantNode>(
        UnderlyingCurrency1 + "_" + BaseCurrency1 + "#" +
            UnderlyingCurrency2 + "_" + BaseCurrency2 + node_name::CORREL,
        [&]( ConstantNode* C )
        {
            C->SetConstantValue( GetValue( UnderlyingCurrency1,
                                           BaseCurrency1,
                                           UnderlyingCurrency2,
                                           BaseCurrency2 ) );
        } );
}

//! MC node carrying one entry L(u1, u2) of the cached Cholesky factor as a
//! constant; the engine combines these with independent normals to produce
//! correlated noise. Assumes ComputeCholeskyMatrix has already been called.
MonteCarloNode* Correlation::GetCholeskyNode( NodeCollector& NC,
                                              const string& Underlying1,
                                              const string& Underlying2 )
{
    return NC.GetOrCreate<ConstantNode>(
        Underlying1 + "#" + Underlying2 + node_name::CHOLESKY,
        [&]( ConstantNode* C )
        { C->SetConstantValue( GetCholeskyValue( Underlying1, Underlying2 ) ); } );
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
