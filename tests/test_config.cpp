#include "yaml_config.hpp"
#include <doctest/doctest.h>
#include <yaml-cpp/yaml.h>

using doctest::Approx;

// YamlConfig is the YAML-backed object store; round-tripping through it must be lossless.
TEST_CASE( "YamlConfig string round-trip: scalars and lists" )
{
    std::string yaml =
        "node: {kind: equity, spot: 123.45, name: foo}\n"
        "curve: {dates: [2000-01-01, 2010-01-01], values: [8, 8.5]}\n";

    YamlConfig cfg( YamlConfig::from_string_t{}, yaml );

    CHECK( cfg.GetDouble( "node.spot" ) == Approx( 123.45 ) );
    CHECK( cfg.GetString( "node.name" ) == "foo" );
    CHECK( cfg.GetString( "node.kind" ) == "equity" );

    auto values = cfg.GetDoubleList( "curve.values" );
    REQUIRE( values.size() == 2 );
    CHECK( values[0] == Approx( 8.0 ) );
    CHECK( values[1] == Approx( 8.5 ) );
}

TEST_CASE( "YamlConfig set then get is consistent" )
{
    YamlConfig cfg( YamlConfig::from_string_t{}, "res: {}\n" );
    cfg.SetDouble( "res.premium", 15.7113 );
    cfg.SetString( "res.label", "atm-call" );
    CHECK( cfg.GetDouble( "res.premium" ) == Approx( 15.7113 ) );
    CHECK( cfg.GetString( "res.label" ) == "atm-call" );

    // Dump must produce a document that parses back to the same value
    std::string dumped = cfg.Dump();
    YAML::Node reparsed = YAML::Load( dumped );
    CHECK( reparsed["res"]["premium"].as<double>() == Approx( 15.7113 ) );
}

TEST_CASE( "GetDouble with default value falls back when absent" )
{
    YamlConfig cfg( YamlConfig::from_string_t{}, "node: {a: 1}\n" );
    CHECK( cfg.GetDouble( "node.a", -1.0 ) == Approx( 1.0 ) );
    CHECK( cfg.GetDouble( "node.missing", -1.0 ) == Approx( -1.0 ) );
}
