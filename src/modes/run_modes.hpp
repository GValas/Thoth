#pragma once
#include "thoth.hpp"

//! ----------------------------------------------------------------------------
//! run_modes.hpp : the contract between thoth.cpp's CLI dispatch and the four run
//! modes. Application entry points, one per run mode (see thoth.cpp / main). Each
//! returns a process exit code. Split across run_batch.cpp / run_server.cpp /
//! run_client.cpp / run_cluster.cpp so each mode is a focused translation unit;
//! main() in thoth.cpp only parses the command line and dispatches. ExecuteYaml
//! is the one shared helper (server + cluster master both reuse it).
//! ----------------------------------------------------------------------------

//! launch a single pricing task from files (-batch <input> <output> [task])
int RunBatch( const string& InputFile, const string& OutputFile, const string& TaskName );

//! HTTP pricing service (-server <port>): POST /price, GET /health, GET /progress
int RunHttpServer( int Port );

//! HTTP client (-client <url> <input>): POST a YAML file and print the result
int RunClient( const string& Url, const string& InputFile );

//! cluster master (-cluster <port> <slave...>): path-split MCL books across slaves
int RunClusterMaster( int Port, const vector<string>& Slaves );

//! execute an in-memory YAML request (any task, not only pricing) and return the
//! YAML result (throws on error). Shared by the server and the cluster master.
string ExecuteYaml( const string& YamlRequest, const string& TaskName );
