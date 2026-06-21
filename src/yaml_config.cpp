#include "thoth.hpp"
#include "yaml_config.hpp"
#include <fstream>
#include <regex>
#include <algorithm>

//! ----------------------------------------------------------------------
//! YAML-backed configuration.
//!
//! The on-disk format is YAML (see tools/cfg2yaml for migrating legacy
//! libconfig .cfg files).  Paths are dot-separated: "object.attribute".
//! Booleans use the native YAML type (emitted as true/false; yaml-cpp also reads yes/no) and
//! dates as ISO strings, exactly as in the original format.  Integers and
//! doubles interconvert (mirroring libconfig's setAutoConvert(true)).
//! ----------------------------------------------------------------------

namespace
{
//! navigate a dotted path with const (non-mutating) access; the returned
//! node is undefined-safe: a missing segment yields an exception below.
YAML::Node Descend( const YAML::Node& node,
                    const vector<string>& parts,
                    size_t i )
{
    if ( i == parts.size() )
    {
        return node;
    }
    const YAML::Node child = node[parts[i]]; //!< const operator[] never inserts
    if ( !child )
    {
        throw std::runtime_error( "missing path segment" );
    }
    return Descend( child, parts, i + 1 );
}

vector<string> SplitDots( const string& Path )
{
    vector<string> parts;
    size_t start = 0;
    while ( true )
    {
        size_t dot = Path.find( '.', start );
        if ( dot == string::npos )
        {
            parts.push_back( Path.substr( start ) );
            break;
        }
        parts.push_back( Path.substr( start, dot - start ) );
        start = dot + 1;
    }
    return parts;
}

//! insert a blank line before each top-level object block (a line starting at
//! column 0), so the emitted YAML reads like the hand-written sample files.
//! The very first block keeps no leading blank line.
//! yaml-cpp emits local tags verbatim as "!<!equity>"; rewrite to "!equity"
static string ShortenTags( const string& Yaml )
{
    static const std::regex verbose( R"(!<!([A-Za-z0-9_]+)>)" );
    return std::regex_replace( Yaml, verbose, "!$1" );
}

//! return a deep copy of the node with every mapping's keys sorted
//! alphabetically (sequences keep their order; scalars unchanged). The local
//! "!kind" tag and node style are preserved so the emitted YAML is identical
//! except for field order.
YAML::Node SortKeysAlpha( const YAML::Node& Node )
{
    if ( Node.IsMap() )
    {
        vector<string> keys;
        for ( const auto& kv : Node )
        {
            keys.push_back( kv.first.Scalar() );
        }
        std::sort( keys.begin(), keys.end() );

        YAML::Node out( YAML::NodeType::Map );
        out.SetStyle( Node.Style() );                        //!< keep flow/block layout
        if ( Node.Tag().size() > 1 && Node.Tag()[0] == '!' ) //!< keep "!kind"
        {
            out.SetTag( Node.Tag() );
        }
        for ( const string& k : keys )
        {
            out[k] = SortKeysAlpha( Node[k] );
        }
        return out;
    }
    if ( Node.IsSequence() )
    {
        YAML::Node out( YAML::NodeType::Sequence );
        out.SetStyle( Node.Style() ); //!< keep inline [..] vs block layout
        if ( Node.Tag().size() > 1 && Node.Tag()[0] == '!' )
        {
            out.SetTag( Node.Tag() );
        }
        for ( const auto& e : Node )
        {
            out.push_back( SortKeysAlpha( e ) );
        }
        return out;
    }
    return Node; //!< scalar (tag/style preserved by copy)
}

static string SpaceTopLevelBlocks( const string& Yaml )
{
    string out;
    out.reserve( Yaml.size() + Yaml.size() / 16 );
    bool first = true;
    size_t start = 0;
    while ( start <= Yaml.size() )
    {
        size_t nl = Yaml.find( '\n', start );
        size_t end = ( nl == string::npos ) ? Yaml.size() : nl;
        //! a top-level key starts at column 0 with a non-space, non-comment char
        bool top_level = ( end > start ) && Yaml[start] != ' ' && Yaml[start] != '#';
        if ( top_level && !first )
        {
            out += '\n';
        }
        if ( top_level )
        {
            first = false;
        }
        out.append( Yaml, start, end - start );
        if ( nl == string::npos )
        {
            break;
        }
        out += '\n';
        start = nl + 1;
    }
    return out;
}

//! emit any multi-line double-quoted scalar (e.g. a node-graph .dot) as a YAML
//! literal block, so the value stays human-readable instead of one long
//! \n / \"-escaped line. yaml-cpp's node emitter has no literal-block style, so
//! this rewrites the already-emitted text: a line
//!   <indent>key: "....\n...."
//! becomes
//!   <indent>key: |-
//!   <indent>  <unescaped line 1>
//!   <indent>  <unescaped line 2>
static string BlockifyMultilineScalars( const string& Yaml )
{
    //! <indent>key: "<value containing an escaped newline>"
    static const std::regex multiline( R"RE(^( *)([A-Za-z0-9_.\-]+): "(.*\\n.*)" *$)RE" );

    string out;
    out.reserve( Yaml.size() );
    size_t start = 0;
    bool first = true;
    while ( start <= Yaml.size() )
    {
        size_t nl = Yaml.find( '\n', start );
        size_t end = ( nl == string::npos ) ? Yaml.size() : nl;
        const string line = Yaml.substr( start, end - start );

        if ( !first )
        {
            out += '\n';
        }
        first = false;

        std::smatch m;
        if ( !std::regex_match( line, m, multiline ) )
        {
            out += line;
        }
        else
        {
            const string indent = m[1].str();
            const string key = m[2].str();
            const string raw = m[3].str();

            //! unescape the double-quoted scalar (\n \t \" \\) into real characters
            string text;
            text.reserve( raw.size() );
            for ( size_t i = 0; i < raw.size(); i++ )
            {
                if ( raw[i] == '\\' && i + 1 < raw.size() )
                {
                    const char c = raw[++i];
                    text += ( c == 'n' ) ? '\n' : ( c == 't' ) ? '\t'
                                                               : c; //!< \" \\ -> the char itself
                }
                else
                {
                    text += raw[i];
                }
            }
            while ( !text.empty() && text.back() == '\n' )
            {
                text.pop_back(); //!< |- strips the trailing newline anyway
            }

            //! key: |-  then each content line indented two spaces under the key
            out += indent + key + ": |-";
            const string child = indent + "  ";
            size_t pos = 0;
            while ( true )
            {
                size_t lnl = text.find( '\n', pos );
                size_t lend = ( lnl == string::npos ) ? text.size() : lnl;
                out += "\n" + child + text.substr( pos, lend - pos );
                if ( lnl == string::npos )
                {
                    break;
                }
                pos = lnl + 1;
            }
        }

        if ( nl == string::npos )
        {
            break;
        }
        start = nl + 1;
    }
    return out;
}
} // namespace

//! contructor
YamlConfig::YamlConfig( const string& InputCfgFile,
                        const string& OutputCfgFile )
    : _out_yml( OutputCfgFile )
{

    // load config
    try
    {
        _root = YAML::LoadFile( InputCfgFile );
    }
    catch ( const std::exception& p )
    {
        ERR( "Error while opening cfg file : " + (string)p.what() );
    }
}

//! in-memory constructor (HTTP request body)
YamlConfig::YamlConfig( from_string_t,
                        const string& YamlContent )
{
    _write_file = false;
    try
    {
        _root = YAML::Load( YamlContent );
    }
    catch ( const std::exception& p )
    {
        ERR( "Error while parsing YAML request : " + (string)p.what() );
    }
}

//! emit the config tree (with admin info) as a YAML string
string YamlConfig::Dump()
{
    Set( "system_information.last_update", GetSysInfoLastUpdate() );
    Set( "system_information.version", GetSysInfoVersion() );

    //! fields are written in alphabetical order (stable across runs, easy to diff)
    YAML::Node root = SortKeysAlpha( _root );

    //! emit each top-level object as a block mapping so its kind tag lands on
    //! its own line; sequences stay inline (flow), keeping matrices/lists compact.
    if ( root.IsMap() )
    {
        for ( auto kv : root )
        {
            if ( kv.second.IsMap() )
            {
                kv.second.SetStyle( YAML::EmitterStyle::Block );
            }
        }
    }

    YAML::Emitter emitter;
    emitter << root;
    return SpaceTopLevelBlocks( BlockifyMultilineScalars( ShortenTags( emitter.c_str() ) ) );
}

//! write the config tree to the output file (file mode only). Explicit, so I/O
//! errors are reported here rather than swallowed in the destructor.
void YamlConfig::WriteFile()
{
    if ( !_write_file || _out_yml.empty() )
    {
        return;
    }
    std::ofstream out( _out_yml );
    if ( !out )
    {
        ERR( "cannot open output file : " + _out_yml );
    }
    out << Dump() << "\n";
    if ( !out )
    {
        ERR( "error while writing output file : " + _out_yml );
    }
}

//! destructor
YamlConfig::~YamlConfig() = default;

//! navigate a dotted path; throws if missing
YAML::Node YamlConfig::LookUp( const string& Path ) const
{
    return Descend( _root, SplitDots( Path ), 0 );
}

//! ----------------------------------------------------------------------
//! getters
//! ----------------------------------------------------------------------

bool YamlConfig::GetBoolean( const string& Path )
{
    try
    {
        return LookUp( Path ).as<bool>(); //!< native YAML boolean (true/false, also yes/no)
    }
    catch ( ... )
    {
        ERR( "parsing error : " + Path + " must be true/false" );
    }
}

string YamlConfig::GetString( const string& Path )
{
    try
    {
        YAML::Node n = LookUp( Path );
        if ( !n.IsScalar() )
            throw std::runtime_error( "not a scalar" );
        return n.as<string>();
    }
    catch ( ... )
    {
        ERR( "parsing error : " + Path + " must be a string" );
    }
}

date YamlConfig::GetDate( const string& Path )
{
    try
    {
        return from_simple_string( GetString( Path ) );
    }
    catch ( ... )
    {
        ERR( "parsing error : " + Path + " must be a date" );
    }
}

double YamlConfig::GetDouble( const string& Path )
{
    try
    {
        return LookUp( Path ).as<double>();
    }
    catch ( ... )
    {
        ERR( "parsing error : " + Path + " must be a double" );
    }
}

int YamlConfig::GetInteger( const string& Path )
{
    try
    {
        YAML::Node n = LookUp( Path );
        try
        {
            return n.as<int>();
        }
        catch ( ... )
        {
            return (int)n.as<double>(); //!< autoConvert float -> int
        }
    }
    catch ( ... )
    {
        ERR( "parsing error : " + Path + " must be an integer" );
    }
}

//! like GetInteger but 64-bit, so values above 2^31 (e.g. billions of MC paths)
//! are not truncated.
long YamlConfig::GetLong( const string& Path )
{
    try
    {
        YAML::Node n = LookUp( Path );
        try
        {
            return n.as<long>();
        }
        catch ( ... )
        {
            return (long)n.as<double>(); //!< autoConvert float -> long
        }
    }
    catch ( ... )
    {
        ERR( "parsing error : " + Path + " must be an integer" );
    }
}

vector<string> YamlConfig::GetStringList( const string& Path )
{
    return GetList<string>( Path, "string vector",
                            []( const YAML::Node& n )
                            { return n.as<string>(); } );
}

vector<date> YamlConfig::GetDateList( const string& Path )
{
    return GetList<date>( Path, "date vector",
                          []( const YAML::Node& n )
                          { return from_simple_string( n.as<string>() ); } );
}

vector<double> YamlConfig::GetDoubleList( const string& Path )
{
    return GetList<double>( Path, "double vector",
                            []( const YAML::Node& n )
                            { return n.as<double>(); } );
}

vector<int> YamlConfig::GetIntegerList( const string& Path )
{
    return GetList<int>( Path, "integer vector",
                         []( const YAML::Node& n )
                         {
                             try
                             {
                                 return n.as<int>();
                             }
                             catch ( ... )
                             {
                                 return (int)n.as<double>(); //!< autoconvert float -> int
                             }
                         } );
}

vector<bool> YamlConfig::GetBooleanList( const string& Path )
{
    return GetList<bool>( Path, "boolean vector",
                          []( const YAML::Node& n )
                          { return n.as<bool>(); } );
}

la_vector* YamlConfig::GetLaVector( const string& Path )
{
    YAML::Node s;
    try
    {
        s = LookUp( Path );
        if ( !s.IsSequence() )
            throw std::runtime_error( "not a list" );
    }
    catch ( ... )
    {
        ERR( "parsing error : " + Path + " must be a double vector" );
    }

    //! own the vector while filling it so a bad element does not leak it
    LaVector v = la_vector_alloc( s.size() );
    try
    {
        for ( size_t i = 0; i < s.size(); i++ )
        {
            la_vector_set( v, i, s[i].as<double>() );
        }
    }
    catch ( ... )
    {
        ERR( "parsing error : " + Path + " must be a double vector" );
    }
    return v.release(); //!< hand ownership to the caller (legacy raw-owning contract)
}

//! ----------------------------------------------------------------------
//! existence / type tests (absent or wrong-typed -> false)
//! ----------------------------------------------------------------------

bool YamlConfig::IsString( const string& Path )
{
    return Probe( [&]
                  { GetString( Path ); } );
}
bool YamlConfig::IsStringList( const string& Path )
{
    return Probe( [&]
                  { GetStringList( Path ); } );
}
bool YamlConfig::IsDouble( const string& Path )
{
    return Probe( [&]
                  { GetDouble( Path ); } );
}
bool YamlConfig::IsDoubleList( const string& Path )
{
    return Probe( [&]
                  { GetDoubleList( Path ); } );
}
bool YamlConfig::IsInteger( const string& Path )
{
    return Probe( [&]
                  { GetInteger( Path ); } );
}
bool YamlConfig::IsIntegerList( const string& Path )
{
    return Probe( [&]
                  { GetIntegerList( Path ); } );
}
bool YamlConfig::IsBoolean( const string& Path )
{
    return Probe( [&]
                  { GetBoolean( Path ); } );
}
bool YamlConfig::IsBooleanList( const string& Path )
{
    return Probe( [&]
                  { GetBooleanList( Path ); } );
}

//! ----------------------------------------------------------------------
//! setters
//! ----------------------------------------------------------------------

//! create/replace the leaf node addressed by "object.attribute"
YAML::Node YamlConfig::PathNode( const string& Path )
{
    string object_name, attribute_name;
    SplitPath( Path, object_name, attribute_name );
    return _root[object_name][attribute_name];
}

//! raw-buffer double list, behind SetLaMatrix / SetLaVector
void YamlConfig::SetDoubleList( const string& Path,
                                const double* Value,
                                size_t size )
{
    SetSeq( Path, Value, Value + size );
}

void YamlConfig::SetLaMatrix( const string& Path,
                              const la_matrix* Value )
{
    SetDoubleList( Path, Value->data, Value->size1 * Value->size2 );
}

void YamlConfig::SetLaVector( const string& Path,
                              const la_vector* Value )
{
    SetDoubleList( Path, Value->data, Value->size );
}

//! ----------------------------------------------------------------------
//! paths / misc
//! ----------------------------------------------------------------------

void YamlConfig::SplitPath( const string& Path,
                            string& ObjectName,
                            string& AttributeName )
{
    size_t i = Path.find_first_of( OBJECT_SEPARATOR );
    ObjectName = Path.substr( 0, i );
    AttributeName = Path.substr( i + 1 );
}

bool YamlConfig::IsPath( const string& Path )
{
    return Path.find_first_of( OBJECT_SEPARATOR ) != string::npos;
}

//! remove object or attribute
void YamlConfig::Remove( const string& Path )
{
    if ( IsPath( Path ) )
    {
        string object_name, attribute_name;
        SplitPath( Path, object_name, attribute_name );
        _root[object_name].remove( attribute_name );
    }
    else
    {
        _root.remove( Path );
    }
}

void YamlConfig::CopyTopLevel( const string& Key,
                               YamlConfig& Source )
{
    //! YAML::Clone deep-copies the subtree so the destination owns its own nodes
    //! (yaml-cpp node assignment is otherwise a shared reference into Source).
    _root[Key] = YAML::Clone( Source._root[Key] );
}

//! local tag of a node, without the leading '!' ("" if missing / untagged)
string YamlConfig::GetTag( const string& Path )
{
    try
    {
        const string tag = LookUp( Path ).Tag();
        if ( !tag.empty() && tag[0] == '!' )
        {
            return tag.substr( 1 );
        }
    }
    catch ( ... )
    {
        // missing path : fall through to ""
    }
    return "";
}

//! get objects by kind
vector<string> YamlConfig::GetObjectsByKind( const string& kind )
{
    vector<string> s;
    if ( !_root.IsMap() )
    {
        return s;
    }
    for ( const auto& kv : _root )
    {
        string object_name = kv.first.as<string>();
        try
        {
            if ( GetTag( object_name ) == kind )
            {
                s.push_back( object_name );
            }
        }
        catch ( ... )
        {
        }
    }
    return s;
}

vector<string> YamlConfig::GetChildKeys( const string& Path )
{
    vector<string> s;
    try
    {
        YAML::Node n = LookUp( Path );
        if ( !n.IsMap() )
        {
            return s;
        }
        for ( const auto& kv : n )
        {
            s.push_back( kv.first.as<string>() );
        }
    }
    catch ( ... )
    {
    }
    return s;
}

//! ----------------------------------------------------------------------
//! get-with-default overloads
//! ----------------------------------------------------------------------

int YamlConfig::GetInteger( const string& Path,
                            const int ElseValue )
{
    return IsInteger( Path ) ? GetInteger( Path ) : ElseValue;
}

long YamlConfig::GetLong( const string& Path,
                          const long ElseValue )
{
    return IsInteger( Path ) ? GetLong( Path ) : ElseValue;
}

double YamlConfig::GetDouble( const string& Path,
                              const double ElseValue )
{
    return IsDouble( Path ) ? GetDouble( Path ) : ElseValue;
}

date YamlConfig::GetDate( const string& Path,
                          const date& ElseValue )
{
    return IsString( Path ) ? GetDate( Path ) : ElseValue;
}

string YamlConfig::GetString( const string& Path,
                              const string& ElseValue )
{
    return IsString( Path ) ? GetString( Path ) : ElseValue;
}

bool YamlConfig::GetBoolean( const string& Path,
                             const bool ElseValue )
{
    return IsBoolean( Path ) ? GetBoolean( Path ) : ElseValue;
}
