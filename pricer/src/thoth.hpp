#pragma once

//! ----------------------------------------------------------------------------
//! thoth.hpp : the project's umbrella / precompiled-style header. Almost every
//! translation unit includes this first, so it gathers the common third-party
//! and standard-library includes, the curated `using` imports that let the code
//! write bare names (string, vector, date, ...), and the in-repo headers that
//! define the shared vocabulary (constants, enums, RAII helpers, math/finance
//! utilities). Keeping this single header authoritative means individual .cpp
//! files declare only what is genuinely local to them.
//! ----------------------------------------------------------------------------

//! C standard library (math + process/IO primitives used throughout)
#include <cmath>
#include <cstdio>
#include <cstdlib>

//! C++ standard library : containers, streams, smart pointers, algorithms — the
//! full set the engine leans on, included once here for every TU.
#include <algorithm>
#include <climits>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <queue>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <vector>

//! dense vector/matrix containers (in-repo; GSL has been fully removed)
#include "linalg.hpp"

//! boost
#include <boost/algorithm/string.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>

//! Targeted using-declarations instead of blanket `using namespace std/boost`,
//! which would pollute every translation unit and risk name collisions (e.g.
//! boost::shared_ptr vs std::shared_ptr). Only the names actually used as bare
//! identifiers across the codebase are imported here.
using std::cerr;
using std::cout;
using std::endl;
using std::exception;
using std::find;
using std::function;
using std::ifstream;
using std::ios;
using std::list;
using std::make_unique;
using std::map;
using std::max;
using std::min;
using std::move;
using std::ofstream;
using std::ostringstream;
using std::pair;
using std::queue;
using std::runtime_error;
using std::set;
using std::setprecision;
using std::sort;
using std::stack;
using std::string;
using std::stringstream;
using std::to_string;
using std::unique_ptr;
using std::vector;

//! boost helpers used as bare identifiers
using boost::algorithm::to_lower_copy;

//! boost.gregorian calendar types/functions used as bare identifiers
using boost::gregorian::date;
using boost::gregorian::days;
using boost::gregorian::from_simple_string;
using boost::gregorian::to_iso_extended_string;
using boost::gregorian::to_simple_string;

//! in-repo shared vocabulary, pulled in for every TU via this umbrella header:
//!   constants    : numeric/financial constants and global tags (e.g. ROOT_NODE)
//!   enums        : engine-wide enumerations (option type, method, ...)
//!   raii         : scope-guard / resource wrappers
//!   object_sets  : the registry of object kinds the YAML books can declare
//!   finance      : day-count, discounting and other market conventions
//!   maths        : numerical helpers (interpolation, roots, distributions, ...)
//!   misc         : logging (LOG/ERR), timing (WallClockSeconds/TaskTime), ToString
#include "constants.hpp"
#include "enums.hpp"
#include "raii.hpp"
#include "object_sets.hpp"
#include "finance.hpp"
#include "maths.hpp"
#include "misc.hpp"
