#pragma once
#include "thoth.hpp"

//! ----------------------------------------------------------------------
//! Small cross-cutting utilities: string splitting/parsing, number formatting,
//! the project's error/log facilities, wall-clock timing, the ACT/365 day-count
//! and process/system introspection. Used everywhere, deliberately dependency-free.
//! ----------------------------------------------------------------------

//! split `str` on every occurrence of the single-char separator `sep` into tokens
//! (always returns at least one element; empty fields are kept)
vector<string> SplitToString( const string& str, const string& sep );
//! split and parse each token as a double (strict parse: bad tokens throw via ERR)
vector<double> SplitToDouble( const string& str, const string& sep );
//! split and parse each token as an int (strict parse: bad tokens throw via ERR)
vector<int> SplitToInt( const string& str, const string& sep );

// template<typename T>
// string ToString(const T & Src);
//! format a double with the project DECIMAL_PRECISION significant digits
string ToString( const double d );
//! format a size_t as decimal
string ToString( size_t i );

//! return `source` with every occurrence of `find` replaced by `replace`
string ReplaceString( const string& source,
                      const string& find,
                      const string& replace );

//! throw a std::runtime_error prefixed with "ERR> " — the single error channel
//! ([[noreturn]]: callers/static analysis know control does not return)
[[noreturn]] void ERR( const string& ErrMsg );

string LogTimestamp(); //!< current local time "YYYY-MM-DD HH:MM:SS" (LOG line prefix)
//! log a line under the default "LOG" context
void LOG( const string& LogMsg );
//! coloured log line, by context: SEQ lines are white, every other context is a
//! dimmed grey — but only when stdout is a terminal (captured / redirected logs
//! stay free of escape codes).
void LOG( const string& Context,
          const string& LogMsg );

//! validate that a schedule is non-decreasing (throws via ERR on an out-of-order date)
void CheckDateList( const vector<date>& DateList );
//! monotonic wall-clock time in seconds (steady_clock); use as the start stamp
//! for TaskTime / TaskTimeLog so timings are real elapsed time, not CPU time.
double WallClockSeconds();
//! elapsed time since StartSeconds, formatted as "task_time = <x> sec"
string TaskTimeLog( double StartSeconds );
//! elapsed seconds since StartSeconds (a WallClockSeconds() stamp)
double TaskTime( double StartSeconds );

//! ACT/365 year fraction between two dates (the single day-count convention)
inline double YearFraction( const date& From, const date& To )
{
    return ( To - From ).days() / NB_OF_DAYS_A_YEAR;
}

//! build banner "Thoth, compilation date : <__DATE__>"
string GetSysInfoVersion();
//! current local time, formatted "Day, DD-Mon-YYYY HH:MM:SS" (HTTP-style stamp)
string GetSysInfoLastUpdate();

//! current resident memory of the process, e.g. "142.3 MB" (empty if unavailable)
string CurrentMemoryUsage();

//! index of the first element of `v` equal to `u`; throws via ERR if absent
size_t VectorPosition( const vector<string>& v,
                       const string& u );