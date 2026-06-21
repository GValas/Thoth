#pragma once
#include "thoth.hpp"
#include <type_traits>
#include <yaml-cpp/yaml.h>

inline constexpr char OBJECT_SEPARATOR[] = ".";

//! The YAML configuration tree: parses the input book, resolves dotted object
//! paths with typed getters (scalars / lists, with clear error messages), and
//! writes each task's result block back out (to a file or an in-memory string).
class YamlConfig
{

  private:
    //! config tree (YAML) + file paths
    YAML::Node _root;
    string _out_yml;
    bool _write_file = true; //!< file mode writes output on destruction; string mode does not

    void SplitPath( const string& Path,
                    string& ObjectName,
                    string& AttributeName );

    //! navigate a dotted path; throws if any segment is missing
    YAML::Node LookUp( const string& Path ) const;

    //! create/replace the node addressed by a dotted path and return it
    YAML::Node PathNode( const string& Path );

    //! shared list getter: validate the sequence, convert each element via conv,
    //! and report a single typed error message on any failure
    template <class T, class Conv>
    vector<T> GetList( const string& Path, const char* TypeName, Conv conv )
    {
        try
        {
            YAML::Node s = LookUp( Path );
            if ( !s.IsSequence() )
                throw std::runtime_error( "not a list" );
            vector<T> v;
            v.reserve( s.size() );
            for ( size_t i = 0; i < s.size(); i++ )
                v.push_back( conv( s[i] ) );
            return v;
        }
        catch ( ... )
        {
            ERR( "parsing error : " + Path + " must be a " + TypeName );
        }
    }

    //! every Is* type test is "does the matching GetX parse?" (a throw means no);
    //! this folds their identical try/catch.
    template <class Getter>
    bool Probe( Getter&& get )
    {
        try
        {
            get();
            return true;
        }
        catch ( ... )
        {
            return false;
        }
    }

    //! build a YAML sequence at Path from [first, last), optionally mapping each
    //! element through conv (e.g. date -> ISO string); shared by the list setters.
    template <class It>
    void SetSeq( const string& Path, It first, It last )
    {
        YAML::Node seq( YAML::NodeType::Sequence );
        for ( ; first != last; ++first )
            seq.push_back( *first );
        PathNode( Path ) = seq;
    }
    template <class It, class Conv>
    void SetSeq( const string& Path, It first, It last, Conv conv )
    {
        YAML::Node seq( YAML::NodeType::Sequence );
        for ( ; first != last; ++first )
            seq.push_back( conv( *first ) );
        PathNode( Path ) = seq;
    }

    bool IsPath( const string& Path );

  public:
    //! get methods
    int GetInteger( const string& Path );
    int GetInteger( const string& Path,
                    const int ElseValue );
    long GetLong( const string& Path ); //!< 64-bit integer (e.g. path counts > 2^31)
    long GetLong( const string& Path,
                  const long ElseValue );
    double GetDouble( const string& Path );
    double GetDouble( const string& Path,
                      const double ElseValue );
    date GetDate( const string& Path );
    date GetDate( const string& Path,
                  const date& ElseValue );
    string GetString( const string& Path );
    string GetString( const string& Path,
                      const string& ElseValue );
    bool GetBoolean( const string& Path );
    bool GetBoolean( const string& Path,
                     const bool ElseValue );

    vector<bool> GetBooleanList( const string& Path );
    vector<string> GetStringList( const string& Path );
    vector<double> GetDoubleList( const string& Path );
    la_vector* GetLaVector( const string& Path );
    vector<int> GetIntegerList( const string& Path );
    vector<date> GetDateList( const string& Path );

    //! set methods. One templated writer covers every scalar (double / int / long /
    //! bool / string / date) and value list (vector<double|string|date|bool>),
    //! dispatching on the type (dates emit ISO strings); the raw-buffer and la_*
    //! writers stay named.
    template <class T>
    void Set( const string& Path, const T& Value )
    {
        if constexpr ( std::is_same_v<T, date> )
            PathNode( Path ) = to_iso_extended_string( Value );
        else if constexpr ( std::is_same_v<T, vector<date>> )
            SetSeq( Path, Value.begin(), Value.end(),
                    []( const date& d )
                    { return to_iso_extended_string( d ); } );
        else if constexpr ( std::is_same_v<T, vector<bool>> )
            SetSeq( Path, Value.begin(), Value.end(), []( bool b )
                    { return b; } );
        else if constexpr ( std::is_same_v<T, vector<double>> || std::is_same_v<T, vector<string>> )
            SetSeq( Path, Value.begin(), Value.end() );
        else
            PathNode( Path ) = Value; //!< double / int / long / bool / string scalar
    }

    void SetDoubleList( const string& Path,
                        const double* Value,
                        size_t size );
    void SetLaMatrix( const string& Path,
                      const la_matrix* Value );
    void SetLaVector( const string& Path,
                      const la_vector* Value );

    //! object existence
    bool IsString( const string& Path );
    bool IsDouble( const string& Path );
    bool IsBoolean( const string& Path );
    bool IsInteger( const string& Path );
    bool IsStringList( const string& Path );
    bool IsDoubleList( const string& Path );
    bool IsBooleanList( const string& Path );
    bool IsIntegerList( const string& Path );

    //! local YAML tag of the node at Path, without the leading '!' (e.g. "equity"
    //! for `!equity`); "" if the node is missing or carries no local tag. Objects
    //! declare their kind through this tag.
    string GetTag( const string& Path );

    //! remove path
    void Remove( const string& Path );

    //! deep-copy the top-level node named Key from another config into this one.
    //! The cluster master uses it to gather each sequence sub-task's result block
    //! (a top-level <name>_result map) into a single aggregated response document.
    void CopyTopLevel( const string& Key,
                       YamlConfig& Source );

    //! objects byt kind
    vector<string> GetObjectsByKind( const string& kind );

    //! immediate child keys of the map at Path (in document order); empty if the
    //! path is missing or not a map. Lets a consumer walk a result block without
    //! knowing its field names ahead of time (e.g. the cluster aggregator pooling
    //! every premium/Greek a slave reported, including model Greeks like vega_v0).
    vector<string> GetChildKeys( const string& Path );

    //! tag to disambiguate the in-memory (string) constructor
    struct from_string_t
    {
    };

    //! emit the current config tree (with system_information) as a YAML string
    string Dump();

    //! write the config tree to the output file (file mode); reports I/O errors
    void WriteFile();

    //! constructor & destructor
    YamlConfig( const string& InputCfgFile,
                const string& OutputCfgFile ); //!< file in / file out
    YamlConfig( from_string_t,
                const string& YamlContent ); //!< in-memory YAML (HTTP)
    ~YamlConfig();
};
