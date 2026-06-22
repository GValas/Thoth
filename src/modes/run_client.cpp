//! ----------------------------------------------------------------------------
//! run_client.cpp : the `-client` run mode — a thin HTTP client. It reads a YAML
//! book from disk, POSTs it to a running thoth `-server` (or `-cluster` master)
//! and prints the YAML result to stdout. The remote does all the pricing; this
//! is just a convenience front end (curl-equivalent) over the /price endpoint.
//! ----------------------------------------------------------------------------

#include "run_modes.hpp"

#include <fstream>
#include <httplib.h>
#include <sstream>

//! HTTP client : POST a YAML file to a thoth server and print the result.
//!   Url       : base URL of the server (e.g. http://host:port)
//!   InputFile : YAML book to send as the request body
//! Returns 0 on success, 1 if the file can't be read, the request fails, or the
//! server reports an error in the response body.
int RunClient( const string& Url,
               const string& InputFile )
{
    //! slurp the whole input file into memory as the request body
    std::ifstream file( InputFile );
    if ( !file )
    {
        cerr << "error: cannot open " << InputFile << endl;
        return 1;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();

    httplib::Client client( Url );
    client.set_read_timeout( 3600 ); //!< pricing can take a while (big MC books)
    client.set_write_timeout( 60 );

    //! POST the book to /price ; content type matches what the server expects
    auto res = client.Post( "/price", buffer.str(), "application/x-yaml" );
    if ( !res ) //!< transport-level failure (connect refused, timeout, ...)
    {
        cerr << "error: request to " << Url << " failed ("
             << httplib::to_string( res.error() ) << ")" << endl;
        return 1;
    }
    cout << res->body;
    //! the server streams errors as a body starting with "error: " (chunked
    //! responses are always 200), so detect failures from the payload too.
    bool failed = res->status != 200 || res->body.rfind( "error: ", 0 ) == 0;
    return failed ? 1 : 0;
}
