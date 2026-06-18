#pragma once
#include "thoth.hpp"

vector<string> SplitToString( const string& str, const string& sep );
vector<double> SplitToDouble( const string& str, const string& sep );
vector<int> SplitToInt( const string& str, const string& sep );

// template<typename T>
// string ToString(const T & Src);
string ToString( const double d );
string ToString( size_t i );

string ReplaceString( const string& source,
                      const string& find,
                      const string& replace );

[[noreturn]] void ERR( const string& ErrMsg );
string LogTimestamp(); //!< current local time "YYYY-MM-DD HH:MM:SS" (LOG line prefix)
void LOG( const string& LogMsg );
//! coloured log line, by context: SEQ lines are white, every other context is a
//! dimmed grey — but only when stdout is a terminal (captured / redirected logs
//! stay free of escape codes).
void LOG( const string& Context,
          const string& LogMsg );

void CheckDateList( const vector<date>& DateList );
//! monotonic wall-clock time in seconds (steady_clock); use as the start stamp
//! for ExecTime / ExecTimeLog so timings are real elapsed time, not CPU time.
double WallClockSeconds();
string ExecTimeLog( double StartSeconds );
double ExecTime( double StartSeconds );

//! ACT/365 year fraction between two dates (the single day-count convention)
inline double YearFraction( const date& From, const date& To )
{
    return ( To - From ).days() / NB_OF_DAYS_A_YEAR;
}

string GetSysInfoVersion();
string GetSysInfoLastUpdate();

//! current resident memory of the process, e.g. "142.3 MB" (empty if unavailable)
string CurrentMemoryUsage();

//!

size_t VectorPosition( const vector<string>& v,
                       const string& u );