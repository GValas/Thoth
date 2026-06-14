#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <gsl/gsl_errno.h>

//! The engine disables the GSL abort-on-error handler in its own main
//! (Thoth.cpp). The test binary has its own entry point, so mirror that here —
//! otherwise routines that probe matrices via Cholesky (e.g. is_positive on a
//! deliberately non-PSD matrix) would abort instead of returning a status.
int main( int argc, char** argv )
{
    gsl_set_error_handler_off();
    return doctest::Context( argc, argv ).run();
}
