#pragma once

#include "nodes.hpp"

#include <utility>

//! path_recorder.hpp — per-path value recorder for the American / Longstaff-Schwartz
//! pass. Snapshots selected nodes' values at chosen diffusion dates on every MC draw
//! into a [draws x dates] matrix the LSM regression reads back. Opt-in: a book with no
//! American contracts never starts a recording, so this stays inert.
//!
//! Extracted from NodeCollector so the graph owner / evaluator stays free of LSM
//! policy — the recorder needs only node observer pointers plus the diffusion-date
//! list (passed in by the owning pricer), not the graph internals.
class PathRecorder
{

  private:
    //! per-path recording : snapshot a node's values at chosen dates, every draw
    struct PathRecord
    {
        MonteCarloNode* node = nullptr;
        vector<size_t> date_index; //!< columns: diffusion-date indices
        vector<double> tau;        //!< year fraction of each column from today
        vector<date> dates;        //!< calendar date of each column (curve reads: LSM discounting)
        LaMatrix paths;            //!< [ nb_draws x date_index.size() ]
        size_t row = 0;            //!< next draw to fill
    };
    vector<PathRecord> _records;

  public:
    //! drop all recordings — call when the owning pricer rebuilds its node graph
    //! (e.g. the bump-and-revalue Greeks rebuild the tree per scenario).
    void Clear() { _records.clear(); }

    //! register a node to snapshot at the given dates; allocate the path matrix.
    //! DateIndices are the exercise-grid columns (into DateList); NbDraws is the
    //! number of MC paths (matrix rows). DateList supplies the tau grid for the LSM
    //! regression. Idempotent per node so several contracts sharing an underlying
    //! record it once.
    void StartRecording( MonteCarloNode* Node,
                         const vector<size_t>& DateIndices,
                         size_t NbDraws,
                         const vector<date>& DateList );

    //! snapshot the current draw into the next row of each recorded matrix. Call once
    //! per path, after the node values for this draw are evaluated (live).
    void RecordPath();

    bool IsRecording() const { return !_records.empty(); }

    //! recorded [ nb_draws x nb_exercise_dates ] matrix for a node, or nullptr. Looked
    //! up by name (the LSM pass holds names, not pointers); linear scan is fine since
    //! only the handful of recorded underlyings are held.
    const la_matrix* RecordedPaths( const string& NodeName ) const;

    //! year fractions of the recorded columns for a node (the tau grid for the LSM
    //! regression); empty vector if the node was not recorded.
    vector<double> RecordedTau( const string& NodeName ) const;

    //! calendar dates of the recorded columns for a node (so the LSM pass can read
    //! zero rates off the term-structured curve per exercise date); empty if not recorded.
    vector<date> RecordedDates( const string& NodeName ) const;

    //! (node, date-index) pairs the topological sort must force-evaluate so every
    //! recorded column is computed: a diffusion spot is already scheduled at all dates
    //! via its self-dependency, but a derived (composite / basket) spot is otherwise
    //! only pulled in where the contract flow reads it (maturity), leaving the interior
    //! path columns uncomputed. Handed to NodeCollector::SortNodes by the pricer.
    vector<std::pair<MonteCarloNode*, size_t>> SchedulePoints() const;
};
