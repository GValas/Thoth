#include "thoth.hpp"
#include "correlation.hpp"

//!
Correlation::Correlation( const string& ObjectName ) : Object( ObjectName, KIND_CORRELATION_MATRIX )
{
}

//!
Correlation::~Correlation() = default;

//! setter (takes ownership of Matrix)
void Correlation::SetMatrix( LaVector Matrix )
{
    size_t n = Matrix->size;
    double n_sqrt = sqrt( (double)n );
    if ( n_sqrt == (int)n_sqrt )
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

//! setter
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
    SetSingleList();
}

//! setter
void Correlation::SetUnderlyingList( const vector<string>& UnderlyingList )
{
    _underlying_list = UnderlyingList;
    SetSingleList();
}

//! getter
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

//!
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

// get matrix (owned by the caller, RAII LaMatrix)
// udl, then fx
LaMatrix Correlation::ExtractMatrix( vector<string> UnderlyingNameList,
                                     vector<Forex*> ForexList )
{

    //! sizes
    size_t fx_size = ForexList.size();
    size_t ud_size = UnderlyingNameList.size();

    //! empty matrix
    if ( fx_size + ud_size < 2 )
    {
        ERR( "Can't extract only one single from correlation matrix" );
    }

    //! matrix to fill
    LaMatrix mat = la_matrix_alloc( fx_size + ud_size, fx_size + ud_size );

    //! eq / eq
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

    //! fx / eq
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

    //! fx / fx
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

    //! diagonal
    for ( size_t i = 0;
          i < fx_size + ud_size;
          i++ )
    {
        la_matrix_set( mat, i, i, 1 );
    }

    // ext_la_matrix_log( mat );

    return mat;
}

//! get value
double Correlation::GetValue( const string& u1,
                              const string& u2 )
{
    size_t i = LookAtPosition( _single_list, u1 );
    size_t j = LookAtPosition( _single_list, u2 );
    return la_matrix_get( _matrix, i, j );
}

//!
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

    //! introduce pivot
    Forex* AI = GetForex( A, I );
    Forex* AJ = GetForex( A, J );

    double rho_AI_S = ( AI ? GetValue( AI->GetName(), S ) : 0 );
    double rho_AJ_S = ( AJ ? GetValue( AJ->GetName(), S ) : 0 );
    double rho_AI_AJ = ( AI && AJ ? GetValue( AI->GetName(), AJ->GetName() ) : 0 );
    double vol_AI = ( AI ? AI->GetConstantVol() : 0 );
    double vol_AJ = ( AJ ? AJ->GetConstantVol() : 0 );
    double vol_IJ = sqrt( vol_AI * vol_AI + vol_AJ * vol_AJ - 2 * rho_AI_AJ * vol_AI * vol_AJ );

    return ( -rho_AI_S * vol_AI + rho_AJ_S * vol_AJ ) / vol_IJ;
}

//!
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

    //! introduce pivot
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
    double vol_IJ = sqrt( vol_AI * vol_AI + vol_AJ * vol_AJ - 2 * rho_AI_AJ * vol_AI * vol_AJ );
    double vol_MN = sqrt( vol_AM * vol_AM + vol_AN * vol_AN - 2 * rho_AM_AN * vol_AM * vol_AN );

    return ( +rho_AI_AM * vol_AI * vol_AM +
             -rho_AI_AN * vol_AI * vol_AN +
             -rho_AJ_AM * vol_AJ * vol_AM +
             +rho_AJ_AN * vol_AJ * vol_AN ) /
           vol_IJ / vol_MN;
}

//!
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

    CholeskyDecomposeLower( _cholesky_matrix );
    _cholesky_key = key; //!< mark the sub-set this factor is now valid for
}

//!
la_matrix* Correlation::ExtractCholeskyMatrix( const vector<string> /*UnderlyingNames*/ )
{
    //! not implemented (and currently unused)
    ERR( "correlation '" + _name + "' : ExtractCholeskyMatrix not implemented" );
}

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

//! set _cholesky_single_list
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

//! fx spot
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
        return 1 / JI->GetSpot();
    }
    else
    {
        ERR( I + "/" + J + " is not defined" );
    }
}

//! fx spot
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
        return JI->GetConstantVol();
    }
    else
    {
        ERR( I + "/" + J + " is not defined" );
    }
}

// list of forex list used
set<string> Correlation::GetForexNameList( set<string> currency_name_list )
{
    set<string> s;
    if ( currency_name_list.size() > 1 )
    {
        if ( _pivot_currency != "" )
        {
            set<string>::iterator c;
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

//!
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

MonteCarloNode* Correlation::GetCorrelNode( NodeCollector& NC,
                                            const string& UnderlyingCurrency,
                                            const string& BaseCurrency,
                                            const string& Underlying )
{
    return NC.GetOrCreate<ConstantNode>(
        UnderlyingCurrency + "_" + BaseCurrency + "#" + Underlying + "#correl",
        [&]( ConstantNode* C )
        { C->SetConstantValue( GetValue( UnderlyingCurrency, BaseCurrency, Underlying ) ); } );
}

MonteCarloNode* Correlation::GetFxNode( NodeCollector& NC,
                                        const string& UnderlyingCurrency,
                                        const string& BaseCurrency )
{
    MonteCarloNode* N;
    string node_name = UnderlyingCurrency + "_" + BaseCurrency + "#quanto_" + BaseCurrency + "#spot";
    if ( !( N = NC.GetNode( node_name ) ) )
    {
        // AB = AI * IB

        // IB
        if ( UnderlyingCurrency == _pivot_currency )
        {

            // IB
            Forex* IB = GetForex( _pivot_currency, BaseCurrency );
            MonteCarloNode* IB_Node = IB->Single::GetNode( NC );
            N = IB_Node;
        }

        // AI
        else if ( BaseCurrency == _pivot_currency )
        {
            // IB
            Forex* IB = GetForex( _pivot_currency, UnderlyingCurrency );
            MonteCarloNode* IB_Node = IB->Single::GetNode( NC );

            // AI = -IB
            ProductNode* AI_node = NC.NewNode<ProductNode>( node_name );
            AI_node->PushNode( IB_Node, -1 );
            N = AI_node;
        }

        // to do
        else
        {
            ERR( " error " );
        }
    }
    return N;
}

MonteCarloNode* Correlation::GetCorrelNode( NodeCollector& NC,
                                            const string& UnderlyingCurrency1,
                                            const string& BaseCurrency1,
                                            const string& UnderlyingCurrency2,
                                            const string& BaseCurrency2 )
{
    return NC.GetOrCreate<ConstantNode>(
        UnderlyingCurrency1 + "_" + BaseCurrency1 + "#" +
            UnderlyingCurrency2 + "_" + BaseCurrency2 + "#correl",
        [&]( ConstantNode* C )
        {
            C->SetConstantValue( GetValue( UnderlyingCurrency1,
                                           BaseCurrency1,
                                           UnderlyingCurrency2,
                                           BaseCurrency2 ) );
        } );
}

//!
MonteCarloNode* Correlation::GetCholeskyNode( NodeCollector& NC,
                                              const string& Underlying1,
                                              const string& Underlying2 )
{
    return NC.GetOrCreate<ConstantNode>(
        Underlying1 + "#" + Underlying2 + "#cholesky",
        [&]( ConstantNode* C )
        { C->SetConstantValue( GetCholeskyValue( Underlying1, Underlying2 ) ); } );
}

MonteCarloNode* Correlation::GetFxVolNode( NodeCollector& NC,
                                           const string& UnderlyingCurrency,
                                           const string& BaseCurrency )
{
    return NC.GetOrCreate<ConstantNode>(
        UnderlyingCurrency + "_" + BaseCurrency + "#vol",
        [&]( ConstantNode* C )
        { C->SetConstantValue( GetFxVol( UnderlyingCurrency, BaseCurrency ) ); } );
}
