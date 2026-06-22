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

//! ----------------------------------------------------------------------------
//! Anonymous-namespace helpers : path navigation/splitting and the post-emit text
//! rewriters that make yaml-cpp's output read like the hand-written sample books
//! (short tags, blank lines between top-level blocks, literal blocks for embedded
//! multi-line scalars, alphabetical key order). All are file-local.
//! ----------------------------------------------------------------------------
namespace
{
//! navigate a dotted path with const (non-mutating) access; the returned
//! node is undefined-safe: a missing segment yields an exception below.
//! Recurses one path segment per call; i == parts.size() is the base case.
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

//! split "object.attribute.leaf" into its dot-separated segments. Always returns
//! at least one element (a path with no dot becomes a single segment).
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

//! insert a blank line before each top-level object block (a line starting at
//! column 0), so the emitted YAML reads like the hand-written sample files. The
//! very first block keeps no leading blank line. Scans the text line by line
//! tracking byte offsets (no copying into a vector of lines).
static string SpaceTopLevelBlocks( const string& Yaml )
{
    string out;
    out.reserve( Yaml.size() + Yaml.size() / 16 ); //!< ~one extra newline per 16 bytes
    bool first = true;                             //!< suppress the blank line before block #1
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

//! file constructor : load the book from InputCfgFile and remember OutputCfgFile
//! for WriteFile(). Stays in file mode (_write_file defaults true).
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

//! in-memory constructor (HTTP request body) : parse a YAML string instead of a
//! file. The from_string_t tag disambiguates it from the file constructor (both
//! take a string). String mode never writes to disk.
YamlConfig::YamlConfig( from_string_t,
                        const string& YamlContent )
{
    _write_file = false; //!< no output file in string/HTTP mode
    try
    {
        _root = YAML::Load( YamlContent );
    }
    catch ( const std::exception& p )
    {
        ERR( "Error while parsing YAML request : " + (string)p.what() );
    }
}

//! emit the config tree (with admin info) as a YAML string. Pipeline: stamp the
//! system_information block, sort keys alphabetically, force top-level maps to
//! block style, let yaml-cpp emit, then post-process the text (tags, multi-line
//! scalars, blank lines) — see the nested call at the bottom.
string YamlConfig::Dump()
{
    //! stamp run metadata so every emitted document records when/which build wrote it
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
    //! text post-processing, applied innermost-first: ShortenTags ("!<!eq>"->"!eq"),
    //! then BlockifyMultilineScalars (quoted "\n" scalars -> literal |- blocks),
    //! then SpaceTopLevelBlocks (blank line between top-level objects).
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

//! scalar getters : each is a one-line GetScalar specialisation (resolve + convert,
//! one typed error on failure). The conv lambda is the only per-type bit — the
//! try/catch + error message live once, in GetScalar.
bool YamlConfig::GetBoolean( const string& Path )
{
    return GetScalar<bool>( Path, "boolean (true/false)", []( const YAML::Node& n )
                            { return n.as<bool>(); } ); //!< native YAML bool (also yes/no)
}

string YamlConfig::GetString( const string& Path )
{
    return GetScalar<string>( Path, "string", []( const YAML::Node& n )
                              {
        if ( !n.IsScalar() )                            //!< reject maps/sequences :
            throw std::runtime_error( "not a scalar" ); //!< only a scalar is a string
        return n.as<string>(); } );
}

//! a date is stored as an ISO/simple-string scalar; parse it via boost.gregorian
date YamlConfig::GetDate( const string& Path )
{
    return GetScalar<date>( Path, "date", []( const YAML::Node& n )
                            { return from_simple_string( n.as<string>() ); } );
}

double YamlConfig::GetDouble( const string& Path )
{
    return GetScalar<double>( Path, "double", []( const YAML::Node& n )
                              { return n.as<double>(); } );
}

//! integers auto-convert from a float leaf (libconfig parity), so a "3.0" still
//! reads as 3. GetLong is the same with a 64-bit result (path counts above 2^31).
int YamlConfig::GetInteger( const string& Path )
{
    return GetScalar<int>( Path, "integer", []( const YAML::Node& n )
                           {
        try { return n.as<int>(); }
        catch ( ... ) { return (int)n.as<double>(); } } );
}

long YamlConfig::GetLong( const string& Path )
{
    return GetScalar<long>( Path, "integer", []( const YAML::Node& n )
                            {
        try { return n.as<long>(); }
        catch ( ... ) { return (long)n.as<double>(); } } );
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

//! read a YAML sequence of doubles into a freshly allocated la_vector and hand
//! ownership to the caller (legacy raw-owning contract). Differs from
//! GetDoubleList only in the return container.
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

//! split at the FIRST separator into "object" / "attribute" (only the two-level
//! object.attribute shape the setters use; deeper dotted paths go through LookUp).
void YamlConfig::SplitPath( const string& Path,
                            string& ObjectName,
                            string& AttributeName )
{
    size_t i = Path.find_first_of( OBJECT_SEPARATOR );
    ObjectName = Path.substr( 0, i );
    AttributeName = Path.substr( i + 1 );
}

//! true if Path contains a separator (i.e. addresses an attribute, not a top-level object)
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

//! every top-level object whose local tag equals `kind` (e.g. all "!equity"
//! objects), in document order. Used to enumerate objects of a given type.
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

//! immediate child keys of the map at Path, in document order; empty if Path is
//! missing or not a map. Lets a consumer walk a result block (the cluster
//! aggregator) without knowing its field names up front.
vector<string> YamlConfig::GetChildKeys( const string& Path )
{
    vector<string> s;
    try
    {
        YAML::Node n = LookUp( Path );
        if ( !n.IsMap() ) //!< scalars/sequences have no named children -> empty
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

//! get-with-default overloads : probe with the matching Is* test, return the
//! parsed value if present/well-typed, else ElseValue. The Is* probe is what
//! makes these non-throwing for optional fields.
int YamlConfig::GetInteger( const string& Path,
                            const int ElseValue )
{
    return IsInteger( Path ) ? GetInteger( Path ) : ElseValue;
}

long YamlConfig::GetLong( const string& Path,
                          const long ElseValue )
{
    return IsInteger( Path ) ? GetLong( Path ) : ElseValue; //!< IsInteger gates both 32/64-bit reads
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
