#include "misc.hpp"
#include <chrono>
#include <unistd.h>

time_t ref_time;

vector<string> SplitToString( const string& str,
                              const string& sep )
{
    vector<string> vSplit;
    size_t i = 0, j = 0;
    i = str.find( sep, i );
    while ( i != string::npos )
    {
        vSplit.push_back( str.substr( j, i - j ) );
        j = i + 1;
        i = str.find( sep, i + 1 );
    }
    vSplit.push_back( str.substr( j, str.length() - j ) );
    return vSplit;
}

vector<double> SplitToDouble( const string& str,
                              const string& sep )
{
    vector<double> vSplit;
    size_t i = 0, j = 0;
    i = str.find( sep, i );
    while ( i != string::npos )
    {
        vSplit.push_back( atof( str.substr( j, i - j ).c_str() ) );
        j = i + 1;
        i = str.find( sep, i + 1 );
    }
    vSplit.push_back( atof( str.substr( j, str.length() - j ).c_str() ) );
    return vSplit;
}

vector<int> SplitToInt( const string& str,
                        const string& sep )
{
    vector<int> vSplit;
    size_t i = 0, j = 0;
    i = str.find( sep, i );
    while ( i != string::npos )
    {
        vSplit.push_back( atoi( str.substr( j, i - j ).c_str() ) );
        j = i + 1;
        i = str.find( sep, i + 1 );
    }
    vSplit.push_back( atoi( str.substr( j, str.length() - j ).c_str() ) );
    return vSplit;
}

string ToString( const double d )
{
    stringstream ss;
    ss << setprecision( DECIMAL_PRECISION ) << d;
    return ss.str();
}

string ToString( size_t i )
{
    stringstream ss;
    ss << i;
    return ss.str();
}

// change each element of the string to upper case
string ToUpperString( string strToConvert )
{
    for ( unsigned int i = 0; i < strToConvert.length(); i++ )
    {
        strToConvert[i] = toupper( strToConvert[i] );
    }
    return strToConvert;
}

// change each element of the string to lower case
string ToLowerString( string strToConvert )
{
    for ( unsigned int i = 0; i < strToConvert.length(); i++ )
    {
        strToConvert[i] = tolower( strToConvert[i] );
    }
    return strToConvert;
}

//!
string ReplaceString( const string& source,
                      const string& find,
                      const string& replace )
{
    size_t j;
    string s = source;
    for ( ; ( j = s.find( find ) ) != string::npos; )
    {
        s.replace( j, find.length(), replace );
    }
    return s;
}

//!
vector<string> NULL_STRING_VECTOR()
{
    vector<string> v;
    return v;
}

//! error
[[noreturn]] void ERR( const string& ErrMsg )
{
    throw std::runtime_error( "ERR> " + ErrMsg );
}

//! log
void LOG( const string& LogMsg )
{
    LOG( "LOG", LogMsg );
}

//! current local time as "YYYY-MM-DD HH:MM:SS"
string LogTimestamp()
{
    time_t now = time( nullptr );
    std::tm tm_now{};
    localtime_r( &now, &tm_now );
    std::ostringstream oss;
    oss << std::put_time( &tm_now, "%Y-%m-%d %H:%M:%S" );
    return oss.str();
}

//! log
void LOG( const string& Context,
          const string& LogMsg )
{
    LOG( Context, LogMsg, "" );
}

//! coloured log : wrap the whole line in AnsiColor when stdout is a terminal,
//! otherwise emit it plain so captured/redirected logs carry no escape codes.
void LOG( const string& Context,
          const string& LogMsg,
          const string& AnsiColor )
{
    string line = LogTimestamp() + " " + ( Context == "" ? "LOG" : Context ) + "> " + LogMsg;
    if ( !AnsiColor.empty() && isatty( STDOUT_FILENO ) )
    {
        cout << AnsiColor + line + "\033[0m" << endl;
    }
    else
    {
        cout << line << endl;
    }
}

//! check date_list
void CheckDateList( const vector<date>& DateList )
{
    for ( size_t i = 1; i < DateList.size(); i++ )
    {
        if ( DateList[i] < DateList[i - 1] )
        {
            ERR( " date_list must be strictly croissant " );
        }
    }
}

//! monotonic wall-clock seconds (real elapsed time, robust to multi-threading)
double WallClockSeconds()
{
    using namespace std::chrono;
    return duration<double>( steady_clock::now().time_since_epoch() ).count();
}

//!
string ExecTimeLog( double StartSeconds )
{
    double x = WallClockSeconds() - StartSeconds;
    return "exec_time = " + ToString( x ) + " sec";
}

//!
double ExecTime( double StartSeconds )
{
    return WallClockSeconds() - StartSeconds;
}

//! resident set size from /proc/self/status (Linux); "" if unavailable
string CurrentMemoryUsage()
{
    std::ifstream status( "/proc/self/status" );
    if ( !status )
    {
        return "";
    }
    string line;
    while ( std::getline( status, line ) )
    {
        if ( line.rfind( "VmRSS:", 0 ) == 0 ) //!< "VmRSS:   123456 kB"
        {
            std::istringstream iss( line.substr( 6 ) );
            double kb = 0;
            iss >> kb;
            std::ostringstream oss;
            oss << std::fixed << std::setprecision( 1 ) << kb / 1024.0 << " MB";
            return oss.str();
        }
    }
    return "";
}

//!
string GetSysInfoVersion()
{
    string s = "Thoth, compilation date : ";
    s += __DATE__;
    return s;
}

//!
string GetSysInfoLastUpdate()
{
    time_t rawtime;
    struct tm* timeinfo;
    char buffer[80];

    time( &rawtime );
    timeinfo = localtime( &rawtime );

    strftime( buffer, 80, "%a, %d-%b-%Y %H:%M:%S", timeinfo );
    return buffer;
}

//! look at element position in vector
size_t VectorPosition( const vector<string>& v,
                       const string& u )
{
    size_t i = 0;
    size_t n = v.size();
    while ( v[i] != u )
    {
        i++;
        if ( i == n )
        {
            ERR( "Element " + u + " missing from vector" );
        }
    }
    return i;
}
