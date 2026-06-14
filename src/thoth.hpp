#pragma once

//! standard libs
#include <cmath>
#include <cstdio>
#include <cstdlib>

//! stl libs
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

//! gsl libs
#include <gsl/gsl_blas.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_complex.h>
#include <gsl/gsl_complex_math.h>
#include <gsl/gsl_eigen.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_interp.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_monte.h>
#include <gsl/gsl_monte_miser.h>
#include <gsl/gsl_monte_plain.h>
#include <gsl/gsl_monte_vegas.h>
#include <gsl/gsl_qrng.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_sort.h>
#include <gsl/gsl_spline.h>
#include <gsl/gsl_statistics.h>
#include <gsl/gsl_vector.h>

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

//! constants
#include "constants.hpp"
#include "enums.hpp"
#include "gsl_raii.hpp"
#include "object_sets.hpp"
#include "finance.hpp"
#include "maths.hpp"
#include "misc.hpp"
