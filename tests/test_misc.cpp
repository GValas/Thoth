#include "misc.hpp"
#include <doctest/doctest.h>

using doctest::Approx;

// Tools_Misc pulls Thoth.h, which brings `using namespace std/boost/gregorian`,
// so string / vector / date / from_simple_string are available unqualified.

TEST_CASE( "SplitToString / SplitToDouble / SplitToInt" )
{
    auto s = SplitToString( "a,bb,ccc", "," );
    REQUIRE( s.size() == 3 );
    CHECK( s[0] == "a" );
    CHECK( s[2] == "ccc" );

    auto d = SplitToDouble( "1.5,2.5,3", "," );
    REQUIRE( d.size() == 3 );
    CHECK( d[1] == Approx( 2.5 ) );

    auto i = SplitToInt( "4|5|6", "|" );
    REQUIRE( i.size() == 3 );
    CHECK( i[2] == 6 );
}

TEST_CASE( "ReplaceString replaces every occurrence" )
{
    CHECK( ReplaceString( "a.b.c", ".", "/" ) == "a/b/c" );
    CHECK( ReplaceString( "noop", "x", "y" ) == "noop" );
}

TEST_CASE( "VectorPosition finds an element or throws" )
{
    vector<string> v = { "x", "y", "z" };
    CHECK( VectorPosition( v, "y" ) == 1 );
    CHECK_THROWS_AS( VectorPosition( v, "absent" ), std::runtime_error );
}

TEST_CASE( "CheckDateList enforces strictly increasing dates" )
{
    vector<date> ok = { from_simple_string( "2000-01-01" ),
                        from_simple_string( "2000-06-01" ) };
    CHECK_NOTHROW( CheckDateList( ok ) );

    vector<date> bad = { from_simple_string( "2000-06-01" ),
                         from_simple_string( "2000-01-01" ) };
    CHECK_THROWS_AS( CheckDateList( bad ), std::runtime_error );
}

TEST_CASE( "ERR throws a runtime_error with the ERR> prefix" )
{
    try
    {
        ERR( "boom" );
        FAIL( "ERR did not throw" );
    }
    catch ( const std::runtime_error& e )
    {
        CHECK( std::string( e.what() ) == "ERR> boom" );
    }
}
