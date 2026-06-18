#include "run_modes.hpp"

#include <fstream>
#include <httplib.h>
#include <sstream>

//! HTTP client : POST a YAML file to a thoth server and print the result
int RunClient( const string& Url,
               const string& InputFile )
{
    std::ifstream file( InputFile );
    if ( !file )
    {
        cerr << "error: cannot open " << InputFile << endl;
        return 1;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();

    httplib::Client client( Url );
    client.set_read_timeout( 3600 ); //!< pricing can take a while
    client.set_write_timeout( 60 );

    auto res = client.Post( "/price", buffer.str(), "application/x-yaml" );
    if ( !res )
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
